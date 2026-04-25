#include "Mesh.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <sstream>

#include <QDir>
#include <QFileInfo>
#include <QVector4D>

namespace {

constexpr float kEpsilon = 1.0e-8f;

struct ObjMaterialLibraryInfo {
    bool isObj = false;
    bool hasMaterialLibrary = false;
    bool hasExistingMaterialLibrary = false;
};

QVector3D toQVector3D(const aiVector3D& v)
{
    return QVector3D(v.x, v.y, v.z);
}

QVector2D readUV0(const aiMesh* mesh, unsigned int vertexIndex)
{
    if (mesh == nullptr || !mesh->HasTextureCoords(0) || vertexIndex >= mesh->mNumVertices) {
        return QVector2D(0.0f, 0.0f);
    }

    const aiVector3D& uv = mesh->mTextureCoords[0][vertexIndex];
    return QVector2D(uv.x, uv.y);
}

QVector3D transformPosition(const aiVector3D& source, float invScale, const QMatrix4x4& transform)
{
    QVector3D p = toQVector3D(source) * invScale;
    QVector4D hp(p.x(), p.y(), p.z(), 1.0f);
    hp = transform * hp;
    return QVector3D(hp.x(), hp.y(), hp.z());
}

QVector3D transformNormal(const aiVector3D& source, const QMatrix4x4& normalTransform)
{
    const QVector3D n = toQVector3D(source);
    QVector4D hn(n.x(), n.y(), n.z(), 0.0f);
    hn = normalTransform * hn;
    return QVector3D(hn.x(), hn.y(), hn.z()).normalized();
}

QVector3D calculateFaceNormal(const QVector3D& p1, const QVector3D& p2, const QVector3D& p3)
{
    return QVector3D::crossProduct(p2 - p1, p3 - p1).normalized();
}

QString normalizeTexturePath(const std::string& modelFilepath, const aiString& texturePath)
{
    QString rawPath = QString::fromUtf8(texturePath.C_Str()).trimmed();
    if (rawPath.isEmpty()) {
        return QString();
    }

    rawPath.replace('\\', '/');
    if (rawPath.startsWith('*')) {
        std::cout << "Warning: embedded texture is not supported in this CPU texture path: "
                  << rawPath.toStdString() << std::endl;
        return QString();
    }

    const QFileInfo modelInfo(QString::fromStdString(modelFilepath));
    const QDir modelDir(modelInfo.absolutePath());
    QString resolved = QDir::isAbsolutePath(rawPath) ? rawPath : modelDir.absoluteFilePath(rawPath);
    resolved = QDir::cleanPath(resolved);

    const QFileInfo textureInfo(resolved);
    const QString canonicalPath = textureInfo.exists() ? textureInfo.canonicalFilePath() : textureInfo.absoluteFilePath();
    return QDir::cleanPath(canonicalPath);
}

bool samePath(const std::string& left, const QString& right)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif
    return QString::fromStdString(left).compare(right, caseSensitivity) == 0;
}

int findTexture(const std::vector<TextureAsset>& textures, const QString& normalizedPath)
{
    for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
        if (samePath(textures[i].sourcePath, normalizedPath)) {
            return i;
        }
    }
    return -1;
}

int loadTexture(const std::string& modelFilepath, const aiString& texturePath, std::vector<TextureAsset>& textures, const char* slotName)
{
    const QString normalizedPath = normalizeTexturePath(modelFilepath, texturePath);
    if (normalizedPath.isEmpty()) {
        return -1;
    }

    const int existingIndex = findTexture(textures, normalizedPath);
    if (existingIndex >= 0) {
        return existingIndex;
    }

    QImage image(normalizedPath);
    if (image.isNull()) {
        std::cout << "Warning: failed to load " << slotName << " texture: "
                  << normalizedPath.toStdString() << std::endl;
        return -1;
    }

    TextureAsset asset;
    asset.sourcePath = normalizedPath.toStdString();
    asset.image = image;
    asset.width = image.width();
    asset.height = image.height();
    textures.push_back(asset);
    return static_cast<int>(textures.size()) - 1;
}

int loadTextureFromTypes(const aiMaterial* material,
                         const std::string& modelFilepath,
                         std::vector<TextureAsset>& textures,
                         std::initializer_list<aiTextureType> types,
                         const char* slotName)
{
    if (material == nullptr) {
        return -1;
    }

    for (aiTextureType type : types) {
        if (material->GetTextureCount(type) == 0) {
            continue;
        }

        aiString path;
        unsigned int uvIndex = 0;
        if (material->GetTexture(type, 0, &path, nullptr, &uvIndex) != AI_SUCCESS) {
            continue;
        }

        if (uvIndex != 0) {
            std::cout << "Warning: " << slotName << " texture requests UV channel "
                      << uvIndex << "; only UV0 is stored" << std::endl;
        }

        return loadTexture(modelFilepath, path, textures, slotName);
    }

    return -1;
}

bool materialHasAnyTexture(const aiMaterial* material)
{
    if (material == nullptr) {
        return false;
    }

    for (int type = aiTextureType_DIFFUSE; type <= AI_TEXTURE_TYPE_MAX; ++type) {
        if (material->GetTextureCount(static_cast<aiTextureType>(type)) > 0) {
            return true;
        }
    }

    return false;
}

bool isSyntheticAssimpMaterial(const aiMaterial* material)
{
    if (material == nullptr || materialHasAnyTexture(material)) {
        return false;
    }

    aiString name;
    if (material->Get(AI_MATKEY_NAME, name) != AI_SUCCESS) {
        return false;
    }

    return QString::fromUtf8(name.C_Str()) == QStringLiteral(AI_DEFAULT_MATERIAL_NAME);
}

bool getColor(const aiMaterial* material, const char* key, unsigned int type, unsigned int index, aiColor4D& value)
{
    return material != nullptr && material->Get(key, type, index, value) == AI_SUCCESS;
}

bool getFloat(const aiMaterial* material, const char* key, unsigned int type, unsigned int index, float& value)
{
    return material != nullptr && material->Get(key, type, index, value) == AI_SUCCESS;
}

void applyAssimpScalars(const aiMaterial* source, Material& target)
{
    aiColor4D color;
    if (getColor(source, AI_MATKEY_BASE_COLOR, color) ||
        getColor(source, AI_MATKEY_COLOR_DIFFUSE, color)) {
        target.baseColor = QVector3D(color.r, color.g, color.b);
        if (color.a < 0.999f) {
            target.alphaMode = static_cast<int>(AlphaMode::Transparent);
        }
    }

    if (getColor(source, AI_MATKEY_COLOR_EMISSIVE, color)) {
        float intensity = 1.0f;
        getFloat(source, AI_MATKEY_EMISSIVE_INTENSITY, intensity);
        target.emissive = QVector3D(color.r, color.g, color.b) * intensity;
    }

    float value = 0.0f;
    if (getFloat(source, AI_MATKEY_METALLIC_FACTOR, value)) {
        target.metallic = value;
    }
    if (getFloat(source, AI_MATKEY_ROUGHNESS_FACTOR, value)) {
        target.roughness = value;
    }
    if (getFloat(source, AI_MATKEY_ANISOTROPY_FACTOR, value)) {
        target.anisotropic = value;
    }
    if (getFloat(source, AI_MATKEY_CLEARCOAT_FACTOR, value)) {
        target.clearcoat = value;
    }
    if (getFloat(source, AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, value)) {
        target.clearcoatGloss = std::max(0.0f, 1.0f - value);
    }
    if (getFloat(source, AI_MATKEY_REFRACTI, value)) {
        target.IOR = value;
    }
    if (getFloat(source, AI_MATKEY_TRANSMISSION_FACTOR, value)) {
        target.transmission = value;
    }
    if (getFloat(source, AI_MATKEY_OPACITY, value) && value < 0.999f) {
        target.alphaMode = static_cast<int>(AlphaMode::Transparent);
    }
}

Material buildMaterial(const aiMaterial* source,
                       const Material& fallback,
                       const std::string& modelFilepath,
                       std::vector<TextureAsset>& textures)
{
    if (isSyntheticAssimpMaterial(source)) {
        return fallback;
    }

    Material result = fallback;
    applyAssimpScalars(source, result);

    const int baseColorTex = loadTextureFromTypes(
        source, modelFilepath, textures, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, "baseColor");
    if (baseColorTex >= 0) {
        result.baseColorTex = baseColorTex;
    }

    const int normalTex = loadTextureFromTypes(
        source, modelFilepath, textures, { aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS }, "normal");
    if (normalTex >= 0) {
        result.normalTex = normalTex;
    }

    const int metallicTex = loadTextureFromTypes(
        source, modelFilepath, textures, { aiTextureType_METALNESS }, "metallic");
    if (metallicTex >= 0) {
        result.metallicTex = metallicTex;
    }

    const int roughnessTex = loadTextureFromTypes(
        source, modelFilepath, textures, { aiTextureType_DIFFUSE_ROUGHNESS }, "roughness");
    if (roughnessTex >= 0) {
        result.roughnessTex = roughnessTex;
    }

    const int emissiveTex = loadTextureFromTypes(
        source, modelFilepath, textures, { aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE }, "emissive");
    if (emissiveTex >= 0) {
        result.emissiveTex = emissiveTex;
    }

    const int opacityTex = loadTextureFromTypes(
        source, modelFilepath, textures, { aiTextureType_OPACITY }, "opacity");
    if (opacityTex >= 0) {
        result.opacityTex = opacityTex;
    }

    return result;
}

ObjMaterialLibraryInfo inspectObjMaterialLibraries(const std::string& filepath)
{
    ObjMaterialLibraryInfo info;
    const QFileInfo modelInfo(QString::fromStdString(filepath));
    if (modelInfo.suffix().compare("obj", Qt::CaseInsensitive) != 0) {
        return info;
    }

    info.isObj = true;
    std::ifstream input(filepath);
    if (!input.is_open()) {
        return info;
    }

    const QDir modelDir(modelInfo.absolutePath());
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string token;
        stream >> token;
        if (token != "mtllib") {
            continue;
        }

        std::string materialLibrary;
        while (stream >> materialLibrary) {
            info.hasMaterialLibrary = true;
            QString materialPath = QString::fromStdString(materialLibrary);
            materialPath.replace('\\', '/');
            const QString resolvedPath = QDir::cleanPath(
                QDir::isAbsolutePath(materialPath) ? materialPath : modelDir.absoluteFilePath(materialPath));
            if (QFileInfo::exists(resolvedPath)) {
                info.hasExistingMaterialLibrary = true;
            }
            else {
                std::cout << "Warning: OBJ material library is missing: "
                          << resolvedPath.toStdString() << std::endl;
            }
        }
    }

    return info;
}

bool computeSceneBounds(const aiScene* scene, QVector3D& minBounds, QVector3D& maxBounds)
{
    bool hasVertex = false;
    const float maxFloat = std::numeric_limits<float>::max();
    minBounds = QVector3D(maxFloat, maxFloat, maxFloat);
    maxBounds = QVector3D(-maxFloat, -maxFloat, -maxFloat);

    if (scene == nullptr) {
        return false;
    }

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr) {
            continue;
        }

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            const QVector3D p = toQVector3D(mesh->mVertices[i]);
            minBounds.setX(std::min(minBounds.x(), p.x()));
            minBounds.setY(std::min(minBounds.y(), p.y()));
            minBounds.setZ(std::min(minBounds.z(), p.z()));
            maxBounds.setX(std::max(maxBounds.x(), p.x()));
            maxBounds.setY(std::max(maxBounds.y(), p.y()));
            maxBounds.setZ(std::max(maxBounds.z(), p.z()));
            hasVertex = true;
        }
    }

    return hasVertex;
}

} // namespace

void MeshLoader::readModel(std::string filepath,
                           std::vector<Triangle>& triangles,
                           std::vector<TextureAsset>& textures,
                           Material material,
                           QMatrix4x4 trans,
                           bool smoothNormal,
                           bool enableNormalization)
{
    const ObjMaterialLibraryInfo objMaterialInfo = inspectObjMaterialLibraries(filepath);
    const bool readFileMaterials = !objMaterialInfo.isObj || objMaterialInfo.hasExistingMaterialLibrary;

    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);

    unsigned int flags = aiProcess_Triangulate |
                         aiProcess_SortByPType |
                         aiProcess_PreTransformVertices;
    if (smoothNormal) {
        flags |= aiProcess_GenSmoothNormals;
    }

    const aiScene* scene = importer.ReadFile(filepath, flags);
    if (scene == nullptr || scene->mRootNode == nullptr || scene->mNumMeshes == 0) {
        std::cout << "Warning: failed to import model " << filepath << ": "
                  << importer.GetErrorString() << std::endl;
        return;
    }

    QVector3D minBounds;
    QVector3D maxBounds;
    if (!computeSceneBounds(scene, minBounds, maxBounds)) {
        std::cout << "Warning: model contains no vertices: " << filepath << std::endl;
        return;
    }

    float invScale = 1.0f;
    if (enableNormalization) {
        const QVector3D length = maxBounds - minBounds;
        const float maxAxis = std::max(length.x(), std::max(length.y(), length.z()));
        if (maxAxis > kEpsilon) {
            invScale = 1.0f / maxAxis;
        }
        else {
            std::cout << "Warning: normalization skipped for degenerate model: "
                      << filepath << std::endl;
        }
    }

    std::vector<Material> materials(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        materials[i] = readFileMaterials
            ? buildMaterial(scene->mMaterials[i], material, filepath, textures)
            : material;
    }

    const QMatrix4x4 normalTransform = trans.inverted().transposed();

    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh* mesh = scene->mMeshes[meshIndex];
        if (mesh == nullptr || (mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) {
            continue;
        }

        std::vector<QVector3D> positions(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            positions[i] = transformPosition(mesh->mVertices[i], invScale, trans);
        }

        std::vector<QVector3D> importedNormals;
        if (smoothNormal && mesh->HasNormals()) {
            importedNormals.resize(mesh->mNumVertices);
            for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
                importedNormals[i] = transformNormal(mesh->mNormals[i], normalTransform);
            }
        }

        std::vector<QVector3D> generatedSmoothNormals;
        if (smoothNormal && importedNormals.empty()) {
            generatedSmoothNormals.assign(mesh->mNumVertices, QVector3D(0.0f, 0.0f, 0.0f));
            for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
                const aiFace& face = mesh->mFaces[faceIndex];
                if (face.mNumIndices != 3) {
                    continue;
                }

                const unsigned int i0 = face.mIndices[0];
                const unsigned int i1 = face.mIndices[1];
                const unsigned int i2 = face.mIndices[2];
                if (i0 >= mesh->mNumVertices || i1 >= mesh->mNumVertices || i2 >= mesh->mNumVertices) {
                    continue;
                }

                const QVector3D n = calculateFaceNormal(positions[i0], positions[i1], positions[i2]);
                generatedSmoothNormals[i0] += n;
                generatedSmoothNormals[i1] += n;
                generatedSmoothNormals[i2] += n;
            }
        }

        Material meshMaterial = material;
        if (mesh->mMaterialIndex < materials.size()) {
            meshMaterial = materials[mesh->mMaterialIndex];
        }

        for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            const aiFace& face = mesh->mFaces[faceIndex];
            if (face.mNumIndices != 3) {
                continue;
            }

            const unsigned int i0 = face.mIndices[0];
            const unsigned int i1 = face.mIndices[1];
            const unsigned int i2 = face.mIndices[2];
            if (i0 >= mesh->mNumVertices || i1 >= mesh->mNumVertices || i2 >= mesh->mNumVertices) {
                continue;
            }

            Triangle t;
            t.p1 = positions[i0];
            t.p2 = positions[i1];
            t.p3 = positions[i2];
            t.uv1 = readUV0(mesh, i0);
            t.uv2 = readUV0(mesh, i1);
            t.uv3 = readUV0(mesh, i2);

            if (!smoothNormal) {
                const QVector3D n = calculateFaceNormal(t.p1, t.p2, t.p3);
                t.n1 = n;
                t.n2 = n;
                t.n3 = n;
            }
            else if (!importedNormals.empty()) {
                t.n1 = importedNormals[i0];
                t.n2 = importedNormals[i1];
                t.n3 = importedNormals[i2];
            }
            else {
                t.n1 = generatedSmoothNormals[i0].normalized();
                t.n2 = generatedSmoothNormals[i1].normalized();
                t.n3 = generatedSmoothNormals[i2].normalized();
            }

            if (t.n1.isNull() || t.n2.isNull() || t.n3.isNull()) {
                std::cout << "zero normal Tri id: "
                          << triangles.size()
                          << std::endl;
            }

            t.material = meshMaterial;
            triangles.push_back(t);
        }
    }
}

QMatrix4x4 MeshLoader::getTransformMatrix(QVector3D rotateCtrl, QVector3D translateCtrl, QVector3D scaleCtrl)
{
    QMatrix4x4 model;
    model.translate(translateCtrl);
    model.rotate(rotateCtrl.x(), QVector3D(1.0f, 0.0f, 0.0f));
    model.rotate(rotateCtrl.y(), QVector3D(0.0f, 1.0f, 0.0f));
    model.rotate(rotateCtrl.z(), QVector3D(0.0f, 0.0f, 1.0f));
    model.scale(scaleCtrl);
    return model;
}
