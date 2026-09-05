#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include "Material.h"
#include "Mesh.h"
#include "Camera.h"
#include "RenderParams.h"

// JSON is the authoritative, versioned CPU description. Paths are absolute only
// in memory; disk documents are rebased against their own directory.
class SceneDocument {
public:
    QJsonObject root;
    QString filePath;
    static SceneDocument model(const QString& path);
    static bool loadScene(const QString& path, SceneDocument& result, QString& error);
    bool saveScene(const QString& path, QString& error) const;
    bool exportScenePackage(const QString& directory, QString& error) const;
    bool validate(QString& error, bool checkFiles = true) const;
    QString packageRoot() const;
    void captureCamera(const Camera& camera);
    void restoreCamera(Camera& camera) const;
    void captureSettings(const RenderParams::Snapshot& settings);
    RenderParams::Snapshot settings() const;
    static QJsonObject materialJson(const Material& material);
    static Material materialFromJson(const QJsonObject& json);
    static QJsonObject textureJson(const TextureAsset& texture);
    static void applySampling(const QJsonObject& json, TextureAsset& texture);
};

QJsonArray jsonVector(const QVector3D& v);
QVector3D sceneVector(const QJsonValue& v, QVector3D fallback = QVector3D());
