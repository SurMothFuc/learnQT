#include "learnQT.h"
#include "WorkspaceUi.h"
#include "BackgroundTestSession.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <QImage>
#include <QOpenGLContext>
#include <QPainter>
#include <QSet>
#include <QTimer>
#include <algorithm>
#include <functional>

namespace
{
// 截图模式只接受应用自己的页名或页序号，未知标记直接判为用法错误，避免写出看似成功的结果。
bool pageFromToken(const QString &token, learnQT::WorkspacePage &page)
{
    static const QMap<QString, learnQT::WorkspacePage> names = {
        {QStringLiteral("home"), learnQT::WorkspacePage::Home},
        {QStringLiteral("scene"), learnQT::WorkspacePage::Scene},
        {QStringLiteral("material"), learnQT::WorkspacePage::Material},
        {QStringLiteral("lighting"), learnQT::WorkspacePage::Lighting},
        {QStringLiteral("lights"), learnQT::WorkspacePage::Lights},
        {QStringLiteral("camera"), learnQT::WorkspacePage::Camera},
        {QStringLiteral("environment"), learnQT::WorkspacePage::Environment},
        {QStringLiteral("render"), learnQT::WorkspacePage::Render},
        {QStringLiteral("resources"), learnQT::WorkspacePage::Resources},
        {QStringLiteral("settings"), learnQT::WorkspacePage::Settings},
        {QStringLiteral("首页"), learnQT::WorkspacePage::Home},
        {QStringLiteral("场景"), learnQT::WorkspacePage::Scene},
        {QStringLiteral("材质"), learnQT::WorkspacePage::Material},
        {QStringLiteral("照明"), learnQT::WorkspacePage::Lighting},
        {QStringLiteral("灯光"), learnQT::WorkspacePage::Lights},
        {QStringLiteral("相机"), learnQT::WorkspacePage::Camera},
        {QStringLiteral("环境"), learnQT::WorkspacePage::Environment},
        {QStringLiteral("渲染"), learnQT::WorkspacePage::Render},
        {QStringLiteral("资源"), learnQT::WorkspacePage::Resources},
        {QStringLiteral("设置"), learnQT::WorkspacePage::Settings}};
    const QString key = token.trimmed().toLower();
    if (names.contains(key))
    {
        page = names.value(key);
        return true;
    }
    bool valid = false;
    const int index = key.toInt(&valid);
    if (valid && index >= 0 && index <= int(learnQT::WorkspacePage::Settings))
    {
        page = learnQT::WorkspacePage(index);
        return true;
    }
    return false;
}

QString pageFileName(learnQT::WorkspacePage page)
{
    if (page == learnQT::WorkspacePage::Lights) return "lights";
    if (page == learnQT::WorkspacePage::Lighting) return "lighting";
    static const char *keys[] = {"home", "scene", "material", "lights", "camera",
                                 "environment", "render", "resources", "settings"};
    return QString::fromLatin1(keys[int(page)]);
}

QString describe(learnQT::WorkspacePage page)
{
    if (page == learnQT::WorkspacePage::Lights) return "灯光";
    if (page == learnQT::WorkspacePage::Lighting) return "照明";
    static const char *names[] = {"首页", "场景", "材质", "灯光", "相机", "环境", "渲染", "资源", "设置"};
    return QString::fromUtf8(names[int(page)]);
}
} // namespace

// `--capture-ui <目录>`：按页截取真实窗口图像后退出。
// Runs on the isolated test desktop; image/state checks do not depend on compositor exposure.
void learnQT::configureUiCapture()
{
    const QStringList arguments = QCoreApplication::arguments();
    const int outputArgument = arguments.indexOf(QStringLiteral("--capture-ui"));
    if (outputArgument < 0)
    {
        return;
    }
    if (outputArgument + 1 >= arguments.size() || arguments[outputArgument + 1].startsWith(QStringLiteral("--")))
    {
        qCritical() << "--capture-ui requires an output directory";
        QTimer::singleShot(0, this, [] { QCoreApplication::exit(2); });
        return;
    }

    const QString outputDirectory = QFileInfo(arguments[outputArgument + 1]).absoluteFilePath();
    QList<WorkspacePage> pages;
    const int pageArgument = arguments.indexOf(QStringLiteral("--capture-pages"));
    if (pageArgument >= 0 && pageArgument + 1 < arguments.size())
    {
        const QStringList tokens = arguments[pageArgument + 1].split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &token : tokens)
        {
            WorkspacePage page = WorkspacePage::Home;
            if (!pageFromToken(token, page))
            {
                qCritical() << "--capture-pages has an unknown page:" << token;
                QTimer::singleShot(0, this, [] { QCoreApplication::exit(2); });
                return;
            }
            if (!pages.contains(page))
            {
                pages.append(page);
            }
        }
    }
    if (pages.isEmpty())
    {
        // 默认覆盖：欢迎首页与带 OpenGL 视口的场景页。
        pages = {WorkspacePage::Home, WorkspacePage::Scene};
    }

    int delayMs = 800;
    const int delayArgument = arguments.indexOf(QStringLiteral("--capture-delay"));
    if (delayArgument >= 0 && delayArgument + 1 < arguments.size())
    {
        bool valid = false;
        const int requested = arguments[delayArgument + 1].toInt(&valid);
        if (valid)
        {
            delayMs = std::max(0, std::min(requested, 30000));
        }
    }
    // GL 页的视口要从帧缓冲取内容，需要给 Render Thread 时间产出首批采样；
    // 期间持续强制重绘，否则隐藏/刚显示过的 QOpenGLWidget 没有可抓的帧。
    int warmupMs = 2000;
    const int warmupArgument = arguments.indexOf(QStringLiteral("--capture-warmup"));
    if (warmupArgument >= 0 && warmupArgument + 1 < arguments.size())
    {
        bool valid = false;
        const int requested = arguments[warmupArgument + 1].toInt(&valid);
        if (valid)
        {
            warmupMs = std::max(0, std::min(requested, 60000));
        }
    }

    if (!QDir().mkpath(outputDirectory))
    {
        qCritical() << "Cannot create capture output directory:" << outputDirectory;
        QTimer::singleShot(0, this, [] { QCoreApplication::exit(2); });
        return;
    }

    // --capture-raster：强制用光栅化交互预览截图，便于人工比对两种预览的观感差异。
    if (arguments.contains(QStringLiteral("--capture-raster")))
    {
        if (viewport && viewport->renderThread())
            viewport->renderThread()->setForceRaster(true);
        else
            connect(viewport, &GLWidget::renderThreadReady, this,
                    [this] { viewport->renderThread()->setForceRaster(true); });
    }

    // 逐页截图是异步序列：每次回到事件循环，让布局、样式与实际绘制都完成后再抓取。
    struct CaptureState
    {
        QList<WorkspacePage> pages;
        QString directory;
        int delayMs = 0;
        int warmupMs = 0;
        int index = 0;
        QStringList failures;
        QJsonArray captures;
        QElapsedTimer waitClock;
        RenderStats stats;
        bool statsReady = false;
        quint64 freshFrames = 0, pageStartFrame = 0;
        // grabPage：抓取当前页并写盘；warmup：GL 页在等待期间反复强制重绘。
        std::function<void()> grabPage;
        std::function<void(int)> warmup;
    };
    auto state = std::make_shared<CaptureState>();
    state->pages = pages;
    state->directory = outputDirectory;
    state->delayMs = delayMs;
    state->warmupMs = warmupMs;
    connect(viewport, &GLWidget::freshFramePresented, this, [state] { ++state->freshFrames; });
    auto attachStats = [this, state] {
        connect(viewport->renderThread(), &RenderThread::statsReady, this, [state](RenderStats stats) {
            state->stats = stats;
            state->statsReady = true;
        });
    };
    if (viewport->renderThread())
        attachStats();
    else
        connect(viewport, &GLWidget::renderThreadReady, this, attachStats);

    setWindowState(windowState() & ~Qt::WindowMinimized);
    showNormal();
    auto captureDimension = [&](const QString &option, int fallback) {
        const int index = arguments.indexOf(option);
        bool valid = false;
        const int value = index >= 0 && index + 1 < arguments.size() ? arguments[index + 1].toInt(&valid) : 0;
        return valid && value >= 320 && value <= 8192 ? value : fallback;
    };
    resize(captureDimension("--capture-width", 1600), captureDimension("--capture-height", 900));

    // 用 shared_ptr 包住递归闭包，避免链条最后一步访问已析构的 std::function。
    auto captureNext = std::make_shared<std::function<void()>>();
    *captureNext = [this, state, captureNext] {
        if (state->index >= state->pages.size())
        {
            QTimer::singleShot(0, this, [state] {
                QFile report(state->directory + "/capture-report.json");
                const bool opened = report.open(QIODevice::WriteOnly);
                if (opened)
                    report.write(QJsonDocument(QJsonObject{
                        {"passed", state->failures.isEmpty()}, {"background", BackgroundTestSession::active()},
                        {"captures", state->captures}, {"failures", QJsonArray::fromStringList(state->failures)}}).toJson());
                QCoreApplication::exit(opened && state->failures.isEmpty() ? 0 : 3);
            });
            return;
        }

        const WorkspacePage page = state->pages[state->index];
        if (page == WorkspacePage::Render && QCoreApplication::arguments().contains("--capture-render-results"))
            setRenderPreviewMode(false);
        state->pageStartFrame = state->freshFrames;
        state->waitClock.restart();
        if (workspace && workspace->page == int(page))
        {
            refreshWorkspace();
        }
        else
        {
            navigateWorkspace(page);
        }
        QTimer::singleShot(state->delayMs, this, [this, state, captureNext] {
            const WorkspacePage page = state->pages[state->index];
            if (viewport && viewport->isVisible())
            {
                state->warmup(state->warmupMs);
                return;
            }
            state->grabPage();
        });
    };
    state->grabPage = [this, state, captureNext] {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QImage image = grab().toImage().convertToFormat(QImage::Format_RGB32);
        const WorkspacePage current = state->pages[state->index];
        bool compositedViewport = false;
        QString viewportFile;
        if (viewport && viewport->isVisible() && viewport->context() &&
            viewport->context()->isValid())
        {
            // QWidget::grab() 不含 OpenGL 表面，视口内容单独从帧缓冲取回后合成。
            const QImage frame = viewport->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
            if (!frame.isNull() && !image.isNull())
            {
                const QPoint origin = viewport->mapTo(this, QPoint(0, 0));
                QPainter painter(&image);
                painter.drawImage(QRect(origin, viewport->size()), frame);
                painter.end();
                compositedViewport = true;
                viewportFile = state->directory + "/" + pageFileName(current) + "-viewport.png";
                if (!frame.save(viewportFile))
                    state->failures.append(viewportFile);
            }
        }
        const QString file = state->directory + QLatin1Char('/') + pageFileName(current) + QStringLiteral(".png");
        const QSize size = image.size();
        if ((viewport->isVisible() && !compositedViewport) || image.isNull() || !image.save(file))
        {
            state->failures.append(file);
            qCritical() << "UI capture failed:" << describe(current) << file;
        }
        else
        {
            qInfo() << "UI capture:" << describe(current) << file << size << "viewport" << compositedViewport;
        }
        state->captures.append(QJsonObject{{"page", pageFileName(current)}, {"file", file},
            {"width", size.width()}, {"height", size.height()}, {"viewportCaptured", compositedViewport},
            {"viewportFile", viewportFile},
            {"raster", state->stats.rasterActive}, {"samples", state->stats.samples},
            {"freshFrames", double(state->freshFrames - state->pageStartFrame)}});
        ++state->index;
        (*captureNext)();
    };
    // Wait for a real completed image as well as the requested warmup. Slow compilation/load
    // must produce a timeout failure rather than a successful screenshot with an empty viewport.
    state->warmup = [this, state, captureNext](int remainingMs) {
        if (viewport && viewport->isVisible())
        {
            if (!viewport->context())
                viewport->grabFramebuffer();
        }
        const bool ready = viewport->context() && viewport->context()->isValid() && state->statsReady &&
            state->stats.version == viewport->sceneVersion() && state->freshFrames > 0 &&
            (state->stats.rasterActive || state->stats.samples > 0);
        if (remainingMs <= 0 && ready)
        {
            state->grabPage();
            return;
        }
        if (state->waitClock.elapsed() > 90000)
        {
            state->failures.append(pageFileName(state->pages[state->index]) + ": no completed viewport image");
            ++state->index;
            (*captureNext)();
            return;
        }
        QTimer::singleShot(100, this, [state, remainingMs] { state->warmup(std::max(0, remainingMs - 100)); });
    };
    QTimer::singleShot(0, this, [captureNext] { (*captureNext)(); });
}
