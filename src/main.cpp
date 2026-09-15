#include "learnQT.h"
#include "BackgroundTestSession.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTextCodec>
#include <QSettings>
#include <QTemporaryDir>
#include <QtWidgets/QApplication>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
int main(int argc, char *argv[])
{
    const int backgroundResult = BackgroundTestSession::launchIfNeeded(argc, argv);
    if (backgroundResult >= 0)
        return backgroundResult;
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QSurfaceFormat fmt;
    fmt.setSwapInterval(0); // 0 = 关闭 VSync
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);
    std::unique_ptr<QTemporaryDir> backgroundPreferences;
    if (BackgroundTestSession::active())
    {
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &message) {
            const auto text = qFormatLogMessage(type, context, message).toUtf8();
            std::fprintf(stderr, "%s\n", text.constData());
        });
        backgroundPreferences.reset(new QTemporaryDir);
        if (!backgroundPreferences->isValid())
        {
            std::cerr << "Cannot create isolated background preferences.\n";
            return 125;
        }
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, backgroundPreferences->path());
    }
    const QStringList arguments = a.arguments();
    // 界面截图模式把偏好重定向到输出目录：既不读取个人的页面布局，也不写入个人配置。
    const int captureArgument = arguments.indexOf(QStringLiteral("--capture-ui"));
    if (captureArgument >= 0 && captureArgument + 1 < arguments.size() &&
        !arguments[captureArgument + 1].startsWith(QStringLiteral("--")))
    {
        const QString directory = QFileInfo(arguments[captureArgument + 1]).absoluteFilePath() + "/preferences";
        QDir().mkpath(directory);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory);
    }
#ifdef SCENE_TESTING
    // UI regressions must not read or overwrite a person's workspace preferences.
    for (int i = 1; i + 1 < arguments.size(); ++i)
        if (arguments[i].contains("-regression") && !arguments[i].startsWith("--regression-"))
        {
            QFileInfo output(arguments[i + 1]);
            auto directory = output.suffix() == "png" ? output.absolutePath() : output.absoluteFilePath();
            QDir().mkpath(directory + "/preferences");
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory + "/preferences");
            break;
        }
#endif
    const int modelArgument = arguments.indexOf(QStringLiteral("--model"));
    const int sceneArgument = arguments.indexOf(QStringLiteral("--scene"));
    if ((modelArgument >= 0 && sceneArgument >= 0) ||
        (modelArgument >= 0 &&
         (modelArgument + 1 >= arguments.size() || arguments[modelArgument + 1].startsWith("--"))) ||
        (sceneArgument >= 0 &&
         (sceneArgument + 1 >= arguments.size() || arguments[sceneArgument + 1].startsWith("--"))))
    {
        std::cerr << "Specify either --scene <file> or --model <file>, not both." << std::endl;
        return 1;
    }
    if (sceneArgument >= 0)
        Scene::setStartupScenePath(arguments[sceneArgument + 1]);
    // 回归入口可用它关闭交互回退（光栅化），专心验证路径追踪预览。
    if (arguments.contains(QStringLiteral("--no-interaction-fallback")))
    {
        RenderParams::Snapshot fallback = RenderParams::instance().snapshot();
        fallback.interactionMode = RenderParams::InteractionKeepPathtrace;
        fallback.rasterLocked = false;
        RenderParams::instance().applySnapshot(fallback);
    }
    if (modelArgument >= 0 && modelArgument + 1 < arguments.size())
    {
        Scene::setStartupModelPath(arguments[modelArgument + 1].toStdString());
    }
    try
    {
        const int saveArgument = arguments.indexOf("--save-scene");
        const int exportArgument = arguments.indexOf("--export-scene-package");
        if (saveArgument >= 0 || exportArgument >= 0)
        {
            QString error;
            auto &scene = Scene::getInstance();
            bool ok = true;
            if (saveArgument >= 0)
                ok = saveArgument + 1 < arguments.size() &&
                     scene.saveScene(arguments[saveArgument + 1], error);
            if (ok && exportArgument >= 0)
                ok = exportArgument + 1 < arguments.size() &&
                     scene.exportScenePackage(arguments[exportArgument + 1], error);
            if (!ok)
                std::cerr << error.toStdString() << std::endl;
            return ok ? 0 : 4;
        }
        if (arguments.contains(QStringLiteral("--validate-model-import")) ||
            arguments.contains("--validate-scene"))
        {
            const Scene &scene = Scene::getInstance();
            const int embeddedTextureCount = static_cast<int>(
                std::count_if(scene.textures.begin(), scene.textures.end(), [](const TextureAsset &texture) {
                    return texture.sourcePath.find("::") != std::string::npos && !texture.image.isNull();
                }));
            const int tangentTriangleCount = static_cast<int>(
                std::count_if(scene.triangles.begin(), scene.triangles.end(), [](const Triangle &triangle) {
                    return !triangle.tangent1.toVector3D().isNull() ||
                           !triangle.tangent2.toVector3D().isNull() ||
                           !triangle.tangent3.toVector3D().isNull();
                }));
            const bool hasTextureTransform =
                std::any_of(scene.textures.begin(), scene.textures.end(), [](const TextureAsset &texture) {
                    return !texture.uvOffset.isNull() ||
                           (texture.uvScale - QVector2D(1.0f, 1.0f)).lengthSquared() > 1.0e-8f ||
                           std::abs(texture.uvRotation) > 1.0e-8f;
                });
            const bool hasNonDefaultWrap =
                std::any_of(scene.textures.begin(), scene.textures.end(), [](const TextureAsset &texture) {
                    return texture.wrapS != 0 || texture.wrapT != 0;
                });
            const bool hasNonDefaultFilter =
                std::any_of(scene.textures.begin(), scene.textures.end(), [](const TextureAsset &texture) {
                    return texture.minFilter != 9987 || texture.magFilter != 9729;
                });

            int expectedEmbeddedTextures = 0;
            const int expectedArgument = arguments.indexOf(QStringLiteral("--expect-embedded-textures"));
            if (expectedArgument >= 0 && expectedArgument + 1 < arguments.size())
            {
                expectedEmbeddedTextures = arguments[expectedArgument + 1].toInt();
            }
            qInfo() << "Model import validation:"
                    << "triangles" << scene.triangles.size() << "textures" << scene.textures.size()
                    << "embeddedTextures" << embeddedTextureCount << "tangentTriangles"
                    << tangentTriangleCount << "textureTransform" << hasTextureTransform << "nonDefaultWrap"
                    << hasNonDefaultWrap << "nonDefaultFilter" << hasNonDefaultFilter;
            for (int i = 0; i < static_cast<int>(scene.textures.size()); ++i)
            {
                const TextureAsset &texture = scene.textures[i];
                std::cout << "Texture " << i << ": " << texture.width << "x" << texture.height << ", offset=("
                          << texture.uvOffset.x() << ", " << texture.uvOffset.y() << ")"
                          << ", scale=(" << texture.uvScale.x() << ", " << texture.uvScale.y() << ")"
                          << ", rotation=" << texture.uvRotation << ", wrap=(" << texture.wrapS << ", "
                          << texture.wrapT << ")"
                          << ", filter=(" << texture.minFilter << ", " << texture.magFilter << ")"
                          << std::endl;
            }
            const bool failed =
                scene.triangles.empty() || embeddedTextureCount < expectedEmbeddedTextures ||
                (arguments.contains(QStringLiteral("--expect-tangents")) && tangentTriangleCount == 0) ||
                (arguments.contains(QStringLiteral("--expect-texture-transform")) && !hasTextureTransform) ||
                (arguments.contains(QStringLiteral("--expect-non-default-wrap")) && !hasNonDefaultWrap) ||
                (arguments.contains(QStringLiteral("--expect-non-default-filter")) && !hasNonDefaultFilter);
            return failed ? 3 : 0;
        }
        learnQT w;
        BackgroundTestSession::startFramePump(&w);
        w.show();
        return a.exec();
    }
    catch (const std::exception &error)
    {
        std::cerr << "Scene initialization failed: " << error.what() << std::endl;
        return 1;
    }
}
