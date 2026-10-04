#include "learnQT.h"
#include <QApplication>
#include <QDebug>
#include <QTimer>
#include <array>
#include <cmath>
void learnQT::configureRegressionCapture()
{
    const QStringList arguments = QCoreApplication::arguments();
    const int outputArgument = arguments.indexOf(QStringLiteral("--render-regression"));
    if (outputArgument < 0 || outputArgument + 1 >= arguments.size())
    {
        return;
    }

    m_regressionOutputPath = QFileInfo(arguments[outputArgument + 1]).absoluteFilePath();
    m_validateLanternRegression = arguments.contains(QStringLiteral("--regression-lantern"));
    const int frameArgument = arguments.indexOf(QStringLiteral("--regression-frames"));
    if (frameArgument >= 0 && frameArgument + 1 < arguments.size())
    {
        bool valid = false;
        const int requestedFrames = arguments[frameArgument + 1].toInt(&valid);
        if (valid)
        {
            m_regressionTargetFrames = std::max(1, std::min(requestedFrames, 512));
        }
    }

    DenoiseMode mode=arguments.contains(QStringLiteral("--regression-denoise")) ? DenoiseMode::OIDN : DenoiseMode::None;
    const int modeArgument=arguments.indexOf("--denoiser");
    if (modeArgument>=0) {
        const QString name=arguments.value(modeArgument+1);
        if (name!="none" && name!="oidn" && name!="realtime") {
            qCritical() << "--denoiser requires none, oidn or realtime"; QTimer::singleShot(0, [] { QCoreApplication::exit(2); }); return;
        }
        mode=readDenoiseMode(QJsonObject{{"denoiseMode",name}});
    }
    const bool regressionDenoise=mode!=DenoiseMode::None;
    RenderParams::instance().setDenoise(regressionDenoise);
    auto captureSettings = editor->document.settings();
    captureSettings.denoise = regressionDenoise;
    captureSettings.denoiseMode=mode;
    if(arguments.contains("--aa")) captureSettings.antialiasing=true;
    if(arguments.contains("--no-aa")) captureSettings.antialiasing=false;
    captureSettings.useTileRendering = false;
    captureSettings.renderLow = false;
    captureSettings.maxRenderFrames = m_regressionTargetFrames;
    editor->document.captureSettings(captureSettings);
    RenderParams::instance().applySnapshot(captureSettings);
    viewport->setFixedSize(qRound(800 / viewport->devicePixelRatioF()),
                           qRound(600 / viewport->devicePixelRatioF()));

    connect(viewport, &GLWidget::renderThreadReady, this, [this, regressionDenoise] {
        auto completedVersion = std::make_shared<quint64>(0);
        connect(viewport->renderThread(), &RenderThread::statsReady, this,
                [this, regressionDenoise, completedVersion](RenderStats s) {
                    if (m_regressionCaptureQueued || s.samples < m_regressionTargetFrames ||
                        (regressionDenoise && s.denoisedVersion != s.accumulationVersion))
                        return;
                    // A second observation includes the final GPU boundary and display publication.
                    if (*completedVersion != s.accumulationVersion)
                    {
                        *completedVersion = s.accumulationVersion;
                        return;
                    }
                    m_regressionCaptureQueued = true;
                    QTimer::singleShot(0, this, [this] { captureRegressionFrame(); });
                });
    });
}

void learnQT::captureRegressionFrame()
{
    const QImage image = viewport->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
    const qint64 pixelCount = static_cast<qint64>(image.width()) * image.height();
    double sum = 0.0;
    double sumSquared = 0.0;
    qint64 nonBlackPixels = 0;
    qint64 nonWhitePixels = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const double luminance = static_cast<double>(qGray(row[x]));
            sum += luminance;
            sumSquared += luminance * luminance;
            nonBlackPixels += luminance >= 4.0 ? 1 : 0;
            nonWhitePixels += luminance <= 251.0 ? 1 : 0;
        }
    }

    const double mean = pixelCount > 0 ? sum / pixelCount : 0.0;
    const double variance = pixelCount > 0 ? std::max(0.0, sumSquared / pixelCount - mean * mean) : 0.0;
    const double standardDeviation = std::sqrt(variance);
    const double nonBlackRatio = pixelCount > 0 ? static_cast<double>(nonBlackPixels) / pixelCount : 0.0;
    const double nonWhiteRatio = pixelCount > 0 ? static_cast<double>(nonWhitePixels) / pixelCount : 0.0;
    double lanternBrightYellowRatio = 1.0;
    if (m_validateLanternRegression && !image.isNull())
    {
        const int x0 = static_cast<int>(0.50 * image.width());
        const int x1 = static_cast<int>(0.80 * image.width());
        const int y0 = static_cast<int>(0.15 * image.height());
        const int y1 = static_cast<int>(0.60 * image.height());
        qint64 brightYellowPixels = 0;
        qint64 sampledPixels = 0;
        for (int y = y0; y < y1; ++y)
        {
            const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = x0; x < x1; ++x)
            {
                const int red = qRed(row[x]);
                const int green = qGreen(row[x]);
                const int blue = qBlue(row[x]);
                brightYellowPixels += red > 150 && green > 130 && blue * 10 < green * 7 ? 1 : 0;
                ++sampledPixels;
            }
        }
        lanternBrightYellowRatio =
            sampledPixels > 0 ? static_cast<double>(brightYellowPixels) / sampledPixels : 0.0;
    }
    const std::array<QPointF, 9> keyLocations = {QPointF(0.2, 0.2), QPointF(0.5, 0.2), QPointF(0.8, 0.2),
                                                 QPointF(0.2, 0.5), QPointF(0.5, 0.5), QPointF(0.8, 0.5),
                                                 QPointF(0.2, 0.8), QPointF(0.5, 0.8), QPointF(0.8, 0.8)};
    int keyMin = 255;
    int keyMax = 0;
    int keyNonBlack = 0;
    for (const QPointF &location : keyLocations)
    {
        const int x = std::min(image.width() - 1, static_cast<int>(location.x() * image.width()));
        const int y = std::min(image.height() - 1, static_cast<int>(location.y() * image.height()));
        const int luminance = image.isNull() ? 0 : qGray(image.pixel(x, y));
        keyMin = std::min(keyMin, luminance);
        keyMax = std::max(keyMax, luminance);
        keyNonBlack += luminance >= 4 ? 1 : 0;
    }

    QDir().mkpath(QFileInfo(m_regressionOutputPath).absolutePath());
    const bool saved = !image.isNull() && image.save(m_regressionOutputPath);
    const bool validImage = image.width() >= 64 && image.height() >= 64 && mean > 2.0 && mean < 253.0 &&
                            standardDeviation > 2.0 && nonBlackRatio > 0.02 && nonWhiteRatio > 0.02 &&
                            keyNonBlack >= 2 && keyMax - keyMin >= 2 &&
                            (!m_validateLanternRegression || lanternBrightYellowRatio > 0.002);

    qInfo() << "Render regression metrics:"
            << "size" << image.size() << "mean" << mean << "stddev" << standardDeviation << "nonBlack"
            << nonBlackRatio << "nonWhite" << nonWhiteRatio << "keyRange" << keyMin << keyMax << "keyNonBlack"
            << keyNonBlack << "lanternBrightYellow" << lanternBrightYellowRatio << "saved" << saved;
    if (!saved || !validImage)
    {
        qCritical() << "Render regression failed: output is missing, black, white, or effectively constant";
        QCoreApplication::exit(2);
        return;
    }
    QCoreApplication::exit(0);
}
