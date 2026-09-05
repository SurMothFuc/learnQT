#include "Mesh.h"
#include "SceneAssets.h"
#include <QFile>
#include <stdexcept>

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/GltfMaterial.h>
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
thread_local SceneAssets* activeAssets = nullptr;

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

QVector3D transformDirection(const aiVector3D& source, const QMatrix4x4& transform)
{
    const QVector4D transformed = transform * QVector4D(toQVector3D(source), 0.0f);
    return QVector3D(transformed.x(), transformed.y(), transformed.z());
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

struct TextureSamplingInfo {
    QVector2D uvOffset = QVector2D(0.0f, 0.0f);
    QVector2D uvScale = QVector2D(1.0f, 1.0f);
    float uvRotation = 0.0f;
    int wrapS = static_cast<int>(aiTextureMapMode_Wrap);
    int wrapT = static_cast<int>(aiTextureMapMode_Wrap);
    int minFilter = 9987; // GL_LINEAR_MIPMAP_LINEAR
    int magFilter = 9729; // GL_LINEAR
};

bool sameSamplingInfo(const TextureAsset& texture, const TextureSamplingInfo& sampling)
{
    return (texture.uvOffset - sampling.uvOffset).lengthSquared() < kEpsilon &&
           (texture.uvScale - sampling.uvScale).lengthSquared() < kEpsilon &&
           std::abs(texture.uvRotation - sampling.uvRotation) < kEpsilon &&
           texture.wrapS == sampling.wrapS &&
           texture.wrapT == sampling.wrapT &&
           texture.minFilter == sampling.minFilter &&
           texture.magFilter == sampling.magFilter;
}

int findTexture(const std::vector<TextureAsset>& textures,
                const QString& normalizedPath,
                const TextureSamplingInfo& sampling)
{
    for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
        if (samePath(textures[i].sourcePath, normalizedPath) &&
            sameSamplingInfo(textures[i], sampling)) {
            return i;
        }
    }
    return -1;
}

QImage decodeEmbeddedTexture(const aiTexture* texture)
{
    if (texture == nullptr || texture->pcData == nullptr || texture->mWidth == 0) {
        return QImage();
    }

    if (texture->mHeight == 0) {
        return QImage::fromData(
            reinterpret_cast<const uchar*>(texture->pcData),
            static_cast<int>(texture->mWidth));
    }

    QImage image(
        static_cast<int>(texture->mWidth),
        static_cast<int>(texture->mHeight),
        QImage::Format_RGBA8888);
    for (unsigned int y = 0; y < texture->mHeight; ++y) {
        uchar* destination = image.scanLine(static_cast<int>(y));
        for (unsigned int x = 0; x < texture->mWidth; ++x) {
            const aiTexel& source = texture->pcData[y * texture->mWidth + x];
            destination[x * 4 + 0] = source.r;
            destination[x * 4 + 1] = source.g;
            destination[x * 4 + 2] = source.b;
            destination[x * 4 + 3] = source.a;
        }
    }
    return image;
}

float srgbChannelToLinear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

QVector3D averageLinearColor(const QImage& source)
{
    if (source.isNull()) {
        return QVector3D(1.0f, 1.0f, 1.0f);
    }

    const int sampleWidth = std::min(source.width(), 64);
    const int sampleHeight = std::min(source.height(), 64);
    const QImage image = source
        .scaled(sampleWidth, sampleHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGBA8888);

    QVector3D sum(0.0f, 0.0f, 0.0f);
    for (int y = 0; y < image.height(); ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const uchar* pixel = row + x * 4;
            sum += QVector3D(
                srgbChannelToLinear(pixel[0] / 255.0f),
                srgbChannelToLinear(pixel[1] / 255.0f),
                srgbChannelToLinear(pixel[2] / 255.0f));
        }
    }

    const float sampleCount = static_cast<float>(image.width() * image.height());
    return sampleCount > 0.0f ? sum / sampleCount : QVector3D(1.0f, 1.0f, 1.0f);
}

int appendTexture(const QString& sourceKey,
                  const QImage& image,
                  std::vector<TextureAsset>& textures,
                  const char* slotName,
                  const TextureSamplingInfo& sampling)
{
    const int existingIndex = findTexture(textures, sourceKey, sampling);
    if (existingIndex >= 0) {
        return existingIndex;
    }

    if (image.isNull()) {
        if (activeAssets) throw std::runtime_error(("Cannot decode texture: " + sourceKey).toStdString());
        std::cout << "Warning: failed to load " << slotName << " texture: "
                  << sourceKey.toStdString() << std::endl;
        return -1;
    }

    TextureAsset asset;
    asset.sourcePath = sourceKey.toStdString();
    asset.image = image;
    asset.width = image.width();
    asset.height = image.height();
    asset.averageLinearColor = averageLinearColor(image);
    asset.uvOffset = sampling.uvOffset;
    asset.uvScale = sampling.uvScale;
    asset.uvRotation = sampling.uvRotation;
    asset.wrapS = sampling.wrapS;
    asset.wrapT = sampling.wrapT;
    asset.minFilter = sampling.minFilter;
    asset.magFilter = sampling.magFilter;
    textures.push_back(asset);
    return static_cast<int>(textures.size()) - 1;
}

int loadTexture(const aiScene* scene,
                const std::string& modelFilepath,
                const aiString& texturePath,
                std::vector<TextureAsset>& textures,
                const char* slotName,
                const TextureSamplingInfo& sampling)
{
    const aiTexture* embedded = scene != nullptr
        ? scene->GetEmbeddedTexture(texturePath.C_Str())
        : nullptr;
    if (embedded != nullptr) {
        const QString sourceKey = QFileInfo(QString::fromStdString(modelFilepath)).absoluteFilePath()
            + QStringLiteral("::")
            + QString::fromUtf8(texturePath.C_Str());
        return appendTexture(sourceKey, decodeEmbeddedTexture(embedded), textures, slotName, sampling);
    }

    const QString normalizedPath = activeAssets
        ? activeAssets->resolve(QString::fromUtf8(texturePath.C_Str()), true)
        : normalizeTexturePath(modelFilepath, texturePath);
    if (normalizedPath.isEmpty()) {
        if (activeAssets) throw std::runtime_error(std::string("Missing texture: ") + texturePath.C_Str());
        return -1;
    }
    return appendTexture(normalizedPath, QImage(normalizedPath), textures, slotName, sampling);
}

int loadTextureFromTypes(const aiMaterial* material,
                         const aiScene* scene,
                         const std::string& modelFilepath,
                         std::vector<TextureAsset>& textures,
                         std::initializer_list<aiTextureType> types,
                         const char* slotName,
                         aiTextureType* matchedType = nullptr)
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
        aiTextureMapMode mapModes[3] = {
            aiTextureMapMode_Wrap,
            aiTextureMapMode_Wrap,
            aiTextureMapMode_Wrap
        };
        if (material->GetTexture(type, 0, &path, nullptr, &uvIndex, nullptr, nullptr, mapModes) != AI_SUCCESS) {
            continue;
        }

        if (uvIndex != 0) {
            std::cout << "Warning: " << slotName << " texture requests UV channel "
                      << uvIndex << "; texture skipped because only UV0 is stored" << std::endl;
            continue;
        }

        TextureSamplingInfo sampling;
        sampling.wrapS = static_cast<int>(mapModes[0]);
        sampling.wrapT = static_cast<int>(mapModes[1]);
        material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(type, 0), sampling.minFilter);
        material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(type, 0), sampling.magFilter);

        aiUVTransform uvTransform;
        if (material->Get(AI_MATKEY_UVTRANSFORM(type, 0), uvTransform) == AI_SUCCESS) {
            sampling.uvOffset = QVector2D(uvTransform.mTranslation.x, uvTransform.mTranslation.y);
            sampling.uvScale = QVector2D(uvTransform.mScaling.x, uvTransform.mScaling.y);
            sampling.uvRotation = uvTransform.mRotation;
        }

        const int textureIndex = loadTexture(scene, modelFilepath, path, textures, slotName, sampling);
        if (textureIndex >= 0) {
            if (matchedType != nullptr) {
                *matchedType = type;
            }
            return textureIndex;
        }
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
        target.opacity *= color.a;
        if (color.a < 0.999f) {
            target.alphaMode = static_cast<int>(AlphaMode::Blend);
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
    if (getFloat(source, AI_MATKEY_OPACITY, value)) {
        target.opacity *= value;
        if (value < 0.999f) {
            target.alphaMode = static_cast<int>(AlphaMode::Blend);
        }
    }

    aiString alphaMode;
    if (source != nullptr && source->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
        const QString mode = QString::fromUtf8(alphaMode.C_Str()).trimmed().toUpper();
        if (mode == QStringLiteral("MASK")) {
            target.alphaMode = static_cast<int>(AlphaMode::Mask);
        }
        else if (mode == QStringLiteral("BLEND")) {
            target.alphaMode = static_cast<int>(AlphaMode::Blend);
        }
        else {
            target.alphaMode = static_cast<int>(AlphaMode::Opaque);
        }
    }
    if (getFloat(source, AI_MATKEY_GLTF_ALPHACUTOFF, value)) {
        target.alphaCutoff = value;
    }
}

Material buildMaterial(const aiMaterial* source,
                       const aiScene* scene,
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
        source, scene, modelFilepath, textures, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, "baseColor");
    if (baseColorTex >= 0) {
        result.baseColorTex = baseColorTex;
    }

    aiTextureType normalTextureType = aiTextureType_NONE;
    const int normalTex = loadTextureFromTypes(
        source, scene, modelFilepath, textures, { aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS }, "normal", &normalTextureType);
    if (normalTex >= 0) {
        result.normalTex = normalTex;
        float normalScale = 1.0f;
        if (getFloat(source, AI_MATKEY_GLTF_TEXTURE_SCALE(normalTextureType, 0), normalScale)) {
            result.normalScale = normalScale;
        }
    }

    aiTextureType metallicTextureType = aiTextureType_NONE;
    const int metallicTex = loadTextureFromTypes(
        source, scene, modelFilepath, textures,
        { aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_METALNESS },
        "metallic", &metallicTextureType);
    if (metallicTex >= 0) {
        result.metallicTex = metallicTex;
        result.metallicChannel = metallicTextureType == aiTextureType_GLTF_METALLIC_ROUGHNESS ? 2 : 0;
    }

    aiTextureType roughnessTextureType = aiTextureType_NONE;
    const int roughnessTex = loadTextureFromTypes(
        source, scene, modelFilepath, textures,
        { aiTextureType_GLTF_METALLIC_ROUGHNESS, aiTextureType_DIFFUSE_ROUGHNESS },
        "roughness", &roughnessTextureType);
    if (roughnessTex >= 0) {
        result.roughnessTex = roughnessTex;
        result.roughnessChannel = roughnessTextureType == aiTextureType_GLTF_METALLIC_ROUGHNESS ? 1 : 0;
    }

    const int emissiveTex = loadTextureFromTypes(
        source, scene, modelFilepath, textures, { aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE }, "emissive");
    if (emissiveTex >= 0) {
        result.emissiveTex = emissiveTex;
    }

    const int opacityTex = loadTextureFromTypes(
        source, scene, modelFilepath, textures, { aiTextureType_OPACITY }, "opacity");
    if (opacityTex >= 0) {
        result.opacityTex = opacityTex;
        if (result.alphaMode == static_cast<int>(AlphaMode::Opaque)) {
            result.alphaMode = static_cast<int>(AlphaMode::Blend);
        }
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
    QFile source(QString::fromStdString(filepath));
    if (!source.open(QIODevice::ReadOnly)) {
        return info;
    }
    std::istringstream input(source.readAll().toStdString());

    const QDir modelDir(modelInfo.absolutePath());
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        std::string token;
        stream >> token;
        if (token != "mtllib") {
            continue;
        }

        // OBJ library names can contain spaces. Prefer the complete remainder
        // when it names a real file, then fall back to multiple library tokens.
        const auto remainder=line.substr(line.find(token)+token.size());
        const QString whole=QString::fromStdString(remainder).trimmed();
        if(activeAssets && !whole.isEmpty() && !activeAssets->resolve(whole).isEmpty()) {
            info.hasMaterialLibrary=true; info.hasExistingMaterialLibrary=true; continue;
        }

        std::string materialLibrary;
        while (stream >> materialLibrary) {
            info.hasMaterialLibrary = true;
            QString materialPath = QString::fromStdString(materialLibrary);
            materialPath.replace('\\', '/');
            const QString resolvedPath = QDir::cleanPath(
                QDir::isAbsolutePath(materialPath) ? materialPath : modelDir.absoluteFilePath(materialPath));
            if (activeAssets ? !activeAssets->resolve(materialPath).isEmpty() : QFileInfo::exists(resolvedPath)) {
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
                           bool enableNormalization, SceneAssets* assets, bool useFileMaterials)
{
    struct Scope { SceneAssets* old; Scope(SceneAssets* a):old(activeAssets) { activeAssets=a; } ~Scope() { activeAssets=old; } } scope(assets);
    const ObjMaterialLibraryInfo objMaterialInfo = inspectObjMaterialLibraries(filepath);
    if (assets && useFileMaterials && objMaterialInfo.hasMaterialLibrary && !objMaterialInfo.hasExistingMaterialLibrary)
        throw std::runtime_error("The model references a missing material library.");
    const bool readFileMaterials = useFileMaterials && (!objMaterialInfo.isObj || objMaterialInfo.hasExistingMaterialLibrary);

    Assimp::Importer importer;
    if (assets) importer.SetIOHandler(assets->createIO());
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);

    unsigned int flags = aiProcess_Triangulate |
                         aiProcess_CalcTangentSpace |
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
            ? buildMaterial(scene->mMaterials[i], scene, material, filepath, textures)
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
            t.sourceMaterialIndex = static_cast<int>(mesh->mMaterialIndex);
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

            // Preserve authored glTF/MikkTSpace tangent handedness. Assimp's
            // CalcTangentSpace output is used as a fallback when the asset did
            // not carry a tangent attribute.
            const auto importedTangent = [&](unsigned int vertexIndex, const QVector3D& normal) {
                if (!mesh->HasTangentsAndBitangents()) {
                    return QVector4D(0.0f, 0.0f, 0.0f, 1.0f);
                }

                QVector3D tangent = transformDirection(mesh->mTangents[vertexIndex], trans);
                const QVector3D bitangent = transformDirection(mesh->mBitangents[vertexIndex], trans);
                tangent -= normal * QVector3D::dotProduct(normal, tangent);
                if (tangent.lengthSquared() <= kEpsilon || bitangent.lengthSquared() <= kEpsilon) {
                    return QVector4D(0.0f, 0.0f, 0.0f, 1.0f);
                }
                tangent.normalize();
                const float handedness = QVector3D::dotProduct(
                    QVector3D::crossProduct(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                return QVector4D(tangent, handedness);
            };
            t.tangent1 = importedTangent(i0, t.n1);
            t.tangent2 = importedTangent(i1, t.n2);
            t.tangent3 = importedTangent(i2, t.n3);

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
