#pragma once
#include <QString>
#include <QJsonObject>
#include <assimp/IOSystem.hpp>

// All dependent model reads and external texture reads share this resolver.
// Dependency keys refer to authored names, values to physical files; export
// rebases values without rewriting or damaging the original model.
struct SceneAssets {
    QString modelPath;
    QString strictRoot;
    QJsonObject dependencies;
    QString resolve(const QString& request, bool record = false);
    Assimp::IOSystem* createIO();
};
