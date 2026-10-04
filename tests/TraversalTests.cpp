#include "SceneGraph.h"
#include "common.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>

namespace
{
void require(bool ok, const std::string &message)
{
    if (!ok)
        throw std::runtime_error(message);
}
QString shaderSource(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), path.toStdString());
    auto source = QString::fromUtf8(file.readAll());
    QRegularExpression include("#include\\s+\"([^\"]+)\"");
    for (auto m = include.match(source); m.hasMatch(); m = include.match(source))
        source.replace(m.capturedStart(), m.capturedLength(),
                       shaderSource(QFileInfo(path).dir().filePath(m.captured(1))));
    return source;
}
std::shared_ptr<MeshGeometry> plane(int divisions = 1, bool positive = true)
{
    auto mesh = std::make_shared<MeshGeometry>();
    for (int y = 0; y < divisions; ++y)
        for (int x = 0; x < divisions; ++x)
        {
            const QVector2D uv[] = {{float(x) / divisions, float(y) / divisions},
                                    {float(x + 1) / divisions, float(y) / divisions},
                                    {float(x + 1) / divisions, float(y + 1) / divisions},
                                    {float(x) / divisions, float(y + 1) / divisions}};
            for (auto indices : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}})
            {
                if (!positive)
                    std::swap(indices[1], indices[2]);
                Triangle t;
                t.uv1 = uv[indices[0]];
                t.uv2 = uv[indices[1]];
                t.uv3 = uv[indices[2]];
                auto position = [](QVector2D v) { return QVector3D(v.x() * 2 - 1, v.y() * 2 - 1, 0); };
                t.p1 = position(t.uv1);
                t.p2 = position(t.uv2);
                t.p3 = position(t.uv3);
                t.n1 = t.n2 = t.n3 = QVector3D(0, 0, positive ? 1 : -1);
                t.tangent1 = t.tangent2 = t.tangent3 = QVector4D(1, 0, 0, positive ? 1 : -1);
                mesh->triangles.push_back(t);
            }
        }
    mesh->build();
    return mesh;
}
struct Fixture
{
    std::vector<std::shared_ptr<const MeshGeometry>> meshes;
    std::vector<SceneInstance> instances;
    std::vector<Material> materials{Material()};
    std::vector<BVHNode> tlas;
    std::vector<unsigned> surfaceInstances;
    void prepare()
    {
        for (auto &instance : instances)
        {
            instance.inverse = instance.transform.inverted();
            instance.bounds = meshes[instance.mesh]->bounds.transformed(instance.transform);
        }
        tlas = buildInstanceBvh(instances, meshes);
        std::vector<bool> seen(tlas.size()), leaves(instances.size());
        std::function<void(int)> visit = [&](int index) {
            require(index > 0 && index < int(tlas.size()) && !seen[index], "TLAS cycle or invalid child");
            seen[index] = true;
            const auto &node = tlas[index];
            if (node.n)
            {
                require(node.n == 1 && node.index >= 0 && node.index < int(instances.size()) &&
                            !leaves[node.index],
                        "TLAS duplicate/invalid leaf");
                leaves[node.index] = true;
            }
            else
            {
                visit(node.left);
                visit(node.right);
                for (int child : {node.left, node.right})
                    for (int axis = 0; axis < 3; ++axis)
                        require(tlas[child].AA[axis] >= node.AA[axis] &&
                                    tlas[child].BB[axis] <= node.BB[axis],
                                "TLAS parent misses child bounds");
            }
        };
        if (tlas.size() > 1)
            visit(1);
        for (size_t i = 1; i < seen.size(); ++i)
            require(seen[i], "Unreachable TLAS node");
        for (size_t i = 0; i < instances.size(); ++i)
            require(leaves[i] == (instances[i].visible && instances[i].bounds.valid),
                    "TLAS lost an instance");
    }
};
struct TestRay
{
    QVector3D origin, direction;
};
class Gpu : public QOpenGLFunctions_3_3_Core
{
  public:
    static constexpr int size = 64;
    QOpenGLContext context;
    QOffscreenSurface surface;
    std::unique_ptr<QOpenGLFramebufferObject> target;
    std::array<GLuint, 13> buffers{}, textures{};
    GLuint vao = 0, images = 0;
    QString modules;
    QString extraDefines;
    bool shadowAny=false,shadowEligible=false;
    std::map<QString, std::unique_ptr<QOpenGLShaderProgram>> programs;
    int triangles = 0, nodes = 0, tops = 0;
    Gpu()
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setSwapInterval(0);
        context.setFormat(format);
        require(context.create(), "GL context");
        surface.setFormat(context.format());
        surface.create();
        require(context.makeCurrent(&surface), "GL current");
        initializeOpenGLFunctions();
        QOpenGLFramebufferObjectFormat f;
        f.setInternalTextureFormat(GL_RGBA32F);
        target.reset(new QOpenGLFramebufferObject(size, size, f));
        require(target->isValid(), "Float framebuffer");
        glGenVertexArrays(1, &vao);
        const auto override = qEnvironmentVariable("TRAVERSAL_SHADER_DIR");
        for (const char *module : {"defines", "structs", "uniforms", "utils", "bvh_material", "hdr_utils",
                                   "light_sampling", "medium"})
        {
            const auto relative = QString("include/%1.glsl").arg(module);
            modules += shaderSource(override.isEmpty()
                                        ? QString::fromStdString(getShaderPath(relative.toStdString()))
                                        : QDir(override).filePath(relative)) +
                       "\n";
        }
        std::vector<QVector4D> texels;
        for (int layer = 0; layer < 2; ++layer)
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    texels.emplace_back(layer ? 1 : .5f, layer ? .5f : .25f, layer ? 1 : .1f,
                                        layer ? 1 : float(x));
        glGenTextures(1, &images);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D_ARRAY, images);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA32F, 2, 2, 2, 0, GL_RGBA, GL_FLOAT, texels.data());
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        upload(6, GL_RGBA32F,
               std::vector<QVector4D>{
                   {1, 1, 0, 0}, {0, 1, 1, 0}, {0, 9728, 0, 0}, {1, 1, 0, 0}, {0, 1, 1, 0}, {0, 9728, 0, 0}});
        upload(4, GL_RGBA32F, std::vector<QVector4D>(4));
    }
    ~Gpu()
    {
        programs.clear();
        target.reset();
        glDeleteBuffers(int(buffers.size()), buffers.data());
        glDeleteTextures(int(textures.size()), textures.data());
        glDeleteTextures(1, &images);
        glDeleteVertexArrays(1, &vao);
        context.doneCurrent();
    }
    template <class T> void upload(int unit, GLenum format, const std::vector<T> &data)
    {
        if (!buffers[unit])
            glGenBuffers(1, &buffers[unit]);
        if (!textures[unit])
            glGenTextures(1, &textures[unit]);
        const std::vector<T> dummy(16);
        const auto &values = data.empty() ? dummy : data;
        glBindBuffer(GL_TEXTURE_BUFFER, buffers[unit]);
        glBufferData(GL_TEXTURE_BUFFER, values.size() * sizeof(T), values.data(), GL_STATIC_DRAW);
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_BUFFER, textures[unit]);
        glTexBuffer(GL_TEXTURE_BUFFER, format, buffers[unit]);
    }
    void uploadNodes(int unit, const std::vector<BVHNode> &tree)
    {
        std::vector<QVector3D> data;
        for (const auto &n : tree)
            for (const auto &v : {QVector3D(n.left, n.right, 0), QVector3D(n.n, n.index, 0), n.AA, n.BB})
                data.push_back(v);
        upload(unit, GL_RGB32F, data);
    }
    void scene(Fixture &fixture)
    {
        std::vector<QVector4D> geometry, materials, instances;
        std::vector<BVHNode> blas(1);
        std::vector<int> bases, roots;
        int triangleCount = 0;
        for (const auto &mesh : fixture.meshes)
        {
            const int base = triangleCount, nodeBase = int(blas.size()) - 1;
            bases.push_back(base);
            roots.push_back(nodeBase + 1);
            for (const auto &t : mesh->triangles)
            {
                for (const auto &v :
                     {QVector4D(t.p1), QVector4D(t.p2), QVector4D(t.p3), QVector4D(t.n1), QVector4D(t.n2),
                      QVector4D(t.n3), QVector4D(t.uv1.x(), t.uv1.y(), t.uv2.x(), t.uv2.y()),
                      QVector4D(t.uv3.x(), t.uv3.y(), 0, 0), t.tangent1, t.tangent2, t.tangent3})
                    geometry.push_back(v);
                ++triangleCount;
            }
            for (size_t i = 1; i < mesh->nodes.size(); ++i)
            {
                auto n = mesh->nodes[i];
                if (n.n)
                    n.index += base;
                else
                {
                    n.left += nodeBase;
                    n.right += nodeBase;
                }
                blas.push_back(n);
            }
        }
        for (const auto &m : fixture.materials)
            for (const auto &v : {QVector4D(m.emissive, m.sheenTint), QVector4D(m.baseColor, m.clearcoat),
                                  QVector4D(m.mediumColor, m.mediumAnisotropy),
                                  QVector4D(m.clearcoatGloss, m.IOR, m.transmission, m.alphaMode),
                                  QVector4D(m.mediumtype, m.mediumDensity, m.subsurface, m.metallic),
                                  QVector4D(m.specularTint, m.roughness, m.anisotropic, m.sheen),
                                  QVector4D(0, 0, m.baseColorTex, m.normalTex),
                                  QVector4D(m.metallicTex, m.roughnessTex, m.emissiveTex, m.opacityTex),
                                  QVector4D(m.opacity, m.alphaCutoff, m.normalScale, m.normalMapFlipY),
                                  QVector4D(m.metallicChannel, m.roughnessChannel, 0, 0)})
                materials.push_back(v);
        std::vector<unsigned> surfaces;
        fixture.surfaceInstances.clear();
        for (int i = 0; i < int(fixture.instances.size()); ++i)
        {
            const auto &instance = fixture.instances[i];
            for (int c = 0; c < 4; ++c)
                instances.push_back(instance.transform.column(c));
            for (int c = 0; c < 4; ++c)
                instances.push_back(instance.inverse.column(c));
            instances.emplace_back(instance.material, roots[instance.mesh],
                                   int(surfaces.size() / 2) - bases[instance.mesh], instance.visible ? 1 : 0);
            for (int t = 0; t < int(fixture.meshes[instance.mesh]->triangles.size()); ++t)
            {
                surfaces.push_back(unsigned(bases[instance.mesh] + t));
                surfaces.push_back(unsigned(i));
                fixture.surfaceInstances.push_back(unsigned(i));
            }
        }
        upload(2, GL_RGBA32F, geometry);
        uploadNodes(3, blas);
        upload(7, GL_RGBA32F, instances);
        upload(8, GL_RGBA32F, materials);
        uploadNodes(9, fixture.tlas);
        upload(10, GL_RG32UI, surfaces);
        upload(11, GL_R32F, std::vector<float>(surfaces.size() / 2));
        triangles = triangleCount;
        nodes = int(blas.size());
        tops = int(fixture.tlas.size());
    }
    std::vector<float> run(const std::vector<TestRay> &rays, const QString &body, bool picking = false)
    {
        std::vector<QVector4D> data;
        for (const auto &r : rays)
        {
            data.emplace_back(r.origin, 0);
            data.emplace_back(r.direction, 0);
        }
        upload(12, GL_RGBA32F, data);
        // Fixtures change uniforms/buffers, not GLSL. Compile each body once so
        // the transform matrix exercises traversal rather than shader compilation.
        auto &cached = programs[extraDefines+"\n"+body];
        if (!cached)
        {
            cached = std::make_unique<QOpenGLShaderProgram>();
            auto &program = *cached;
            const char *vertex =
                "#version 330 core\nout vec3 pix;void main(){vec2 "
                "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2-1;pix=vec3(p,0);gl_Position=vec4(p,0,1);}";
            require(program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex),
                    program.log().toStdString());
            const auto fragment =
                QString("#version 330 core\n#define INSTANCED_SCENE\nin vec3 pix;layout(location=0)out vec4 "
                        "outputColor;\nuniform samplerBuffer testRays;uniform int testRayCount;\n") +
                extraDefines+"\n"+modules +
                "\nvoid main(){int id=(int(gl_FragCoord.y)*64+int(gl_FragCoord.x))%testRayCount;Ray "
                "r;r.startPoint=texelFetch(testRays,id*2).xyz;r.direction=texelFetch(testRays,id*2+1).xyz;" +
                body + "}\n";
            require(program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment),
                    program.log().toStdString());
            require(program.link(), program.log().toStdString());
        }
        auto &program = *cached;
        program.bind();
        const char *samplers[] = {
            "hdrMap",        "hdrCache",         "triangles",           "nodes",
            "lights",        "materialTextures", "materialTextureInfo", "instanceTable",
            "materialTable", "topNodes",         "surfaceTable",        "surfacePdfTable",
            "testRays"};
        for (int i = 0; i < 13; ++i)
            program.setUniformValue(samplers[i], i);
        program.setUniformValue("width", size);
        program.setUniformValue("height", size);
        program.setUniformValue("testRayCount", int(rays.size()));
        program.setUniformValue("nTriangles", triangles);
        program.setUniformValue("nNodes", nodes);
        program.setUniformValue("nTopNodes", tops);
        program.setUniformValue("nLights", 0);
        program.setUniformValue("nAnalyticLights", 0);
        program.setUniformValue("materialTextureCount", 2);
        program.setUniformValue("picking", picking);
        program.setUniformValue("shadowAnyHit",shadowAny);
        program.setUniformValue("shadowBinaryScene",shadowEligible);
        glUniform1ui(program.uniformLocation("frameCounter"), 7);
        const auto sobol = getSobelRandomNumber(7, 60);
        program.setUniformValueArray("sobelNumber", sobol.data(), int(sobol.size()), 1);
        target->bind();
        glViewport(0, 0, size, size);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::vector<float> result(size * size * 4);
        glReadPixels(0, 0, size, size, GL_RGBA, GL_FLOAT, result.data());
        require(glGetError() == GL_NO_ERROR, "Traversal GPU draw/readback error");
        for (float v : result)
            require(std::isfinite(v), "Traversal returned NaN/Inf");
        program.release();
        target->release();
        return result;
    }
};
const QString hitBody =
    "HitResult h=hitBVH(r);outputColor=vec4(h.triangleIndex+1,h.hitDistance,h.isInside?1:0,0);";
void randomized(Gpu &gpu)
{
    Fixture f;
    f.meshes = {plane(8)};
    for (int i = 0; i < 64; ++i)
    {
        SceneInstance instance;
        instance.mesh = instance.material = 0;
        instance.visible = i % 9 != 0;
        instance.transform.translate((i % 8 - 4) * 1.1f, (i / 8 - 4) * 1.2f, i * .035f);
        instance.transform.rotate(i * 11, QVector3D(0, 1, 0));
        instance.transform.rotate(i * 7, QVector3D(1, 0, 0));
        instance.transform.scale(i % 2 ? -.7f : .8f, .5f + .01f * i, 1.7f);
        f.instances.push_back(instance);
    }
    f.prepare();
    gpu.scene(f);
    std::mt19937 random(12873);
    std::uniform_real_distribution<float> position(-6, 6), direction(-1, 1);
    std::vector<TestRay> rays;
    for (int i = 0; i < Gpu::size * Gpu::size; ++i)
    {
        TestRay r{{position(random), position(random), position(random)},
                  QVector3D(direction(random), direction(random), direction(random)).normalized()};
        if (i < 6)
        {
            r.origin = QVector3D(0, 0, 5);
            r.direction = QVector3D();
            r.direction[i / 2] = i % 2 ? -1 : 1;
        }
        rays.push_back(r);
    }
    auto hits = gpu.run(rays, hitBody);
    int hitCount = 0;
    for (size_t i = 0; i < rays.size(); ++i)
    {
        const auto expected =
            intersectScene(f.meshes, f.instances, f.tlas, rays[i].origin, rays[i].direction, true);
        const int surface = int(hits[i * 4]) - 1;
        const int actual = surface < 0 ? -1 : int(f.surfaceInstances.at(surface));
        require(actual == expected.instance, "GPU/CPU instance mismatch for ray " + std::to_string(i));
        if (actual >= 0)
        {
            ++hitCount;
            require(std::abs(hits[i * 4 + 1] - expected.distance) < 2e-4f * std::max(1.f, expected.distance),
                    "GPU distance changed under mirrored/nonuniform transform");
        }
    }
    require(hitCount > 300, "Random fixture did not exercise enough intersections");
    std::cout << "GPU/brute-force: 4096 rays, " << hitCount << " hits, shared BLAS and mirrored transforms\n";
}
void tiesAndAlpha(Gpu &gpu)
{
    Fixture f;
    f.meshes = {plane()};
    SceneInstance first;
    first.mesh = first.material = 0;
    f.instances = {first, first};
    f.prepare();
    gpu.scene(f);
    const std::vector<TestRay> rays = {{{-.4f, 0, 4}, {0, 0, -1}}, {{.4f, 0, 4}, {0, 0, -1}}};
    const auto before = gpu.run(rays, hitBody);
    for (auto &n : f.tlas)
        std::swap(n.left, n.right);
    gpu.scene(f);
    require(gpu.run(rays, hitBody) == before, "Equal-distance hits depend on TLAS child order");
    for (int i = 0; i < Gpu::size * Gpu::size; ++i)
        require(f.surfaceInstances.at(int(before[i * 4]) - 1) == 0,
                "Tie did not prefer first logical surface");
    f.materials.resize(2);
    f.materials[0].baseColorTex = 0;
    f.instances[0].transform.translate(0, 0, 1);
    f.instances[1].material = 1;
    for (int mode : {2, 3})
    {
        f.materials[0].alphaMode = mode;
        f.prepare();
        gpu.scene(f);
        for (bool picking : {false, true})
        {
            const auto pixels = gpu.run(rays, hitBody, picking);
            for (int i = 0; i < Gpu::size * Gpu::size; ++i)
            {
                int surface = int(pixels[i * 4]) - 1;
                require(surface >= 0 && f.surfaceInstances.at(surface) == (i % 2 ? 0u : 1u),
                        "Mask/Blend alpha texture picked wrong surface");
            }
        }
    }
    for (float opacity : {.4f, .6f})
    {
        f.materials[0].opacity = opacity;
        gpu.scene(f);
        const auto pixels = gpu.run(rays, hitBody, true);
        require(f.surfaceInstances.at(int(pixels[4]) - 1) == (opacity < .5f ? 1u : 0u),
                "Blend picking threshold changed");
        const auto stochastic = gpu.run(rays, hitBody);
        int accepted = 0;
        for (int i = 1; i < Gpu::size * Gpu::size; i += 2)
            accepted += f.surfaceInstances.at(int(stochastic[i * 4]) - 1) == 0;
        const float fraction = float(accepted) / (Gpu::size * Gpu::size / 2);
        require(std::abs(fraction - opacity) < .05f, "Blend coverage no longer follows opacity");
    }
    // Rungholt's water atlas has alpha 136/255. Applying its duplicate map_d
    // again and using Mask removes the surface from beauty and picking.
    const float waterAlpha = 136.f / 255.f;
    f.materials[0].opacity = waterAlpha * waterAlpha;
    f.materials[0].alphaMode = 2;
    gpu.scene(f);
    for (bool picking : {false, true})
        require(f.surfaceInstances.at(int(gpu.run(rays, hitBody, picking)[4]) - 1) == 1u,
                "Water alpha-cutout reproduction did not skip the front surface");
    f.materials[0].opacity = waterAlpha;
    f.materials[0].alphaMode = 3;
    gpu.scene(f);
    require(f.surfaceInstances.at(int(gpu.run(rays, hitBody, true)[4]) - 1) == 0u,
            "Corrected partially transparent water cannot be picked");
    const auto water = gpu.run(rays, hitBody);
    int waterHits = 0;
    for (int i = 1; i < Gpu::size * Gpu::size; i += 2)
        waterHits += f.surfaceInstances.at(int(water[i * 4]) - 1) == 0;
    require(std::abs(float(waterHits) / (Gpu::size * Gpu::size / 2) - waterAlpha) < .05f,
            "Corrected water disappeared or lost its partial coverage in beauty");
    std::cout << "GPU equal-distance ordering and textured Mask/Blend passed\n";
}
void normalsAndMedia(Gpu &gpu)
{
    Fixture f;
    f.meshes = {plane(), plane(1, false)};
    SceneInstance instance;
    instance.mesh = instance.material = 0;
    instance.transform.rotate(34, QVector3D(0, 1, 0));
    instance.transform.scale(-2, .6f, 1.7f);
    f.instances = {instance};
    f.materials[0].normalTex = 1;
    f.materials[0].anisotropic=.9f;
    f.prepare();
    gpu.scene(f);
    const auto normal = f.instances[0].inverse.transposed().mapVector(QVector3D(0, 0, 1)).normalized();
    const auto tangent = instance.transform.mapVector(QVector3D(1, 0, 0)).normalized();
    const auto expected = (normal + tangent).normalized();
    const auto mapped =
        gpu.run({{normal * 4, -normal}}, "HitResult h=hitBVH(r);outputColor=vec4(h.normal,1);");
    require(QVector3D::dotProduct(QVector3D(mapped[0], mapped[1], mapped[2]), expected) > .9999f,
            "Mirrored nonuniform normal-map tangent frame changed");
    const auto authored=gpu.run({{normal*4,-normal}},
        "HitResult h=hitBVH(r);outputColor=h.material.tangent;");
    const auto projected=(tangent-expected*QVector3D::dotProduct(expected,tangent)).normalized();
    require(QVector3D::dotProduct(QVector3D(authored[0],authored[1],authored[2]),projected)>.9999f &&
            authored[3]==-1,"BSDF tangent did not follow mirrored nonuniform normal-mapped instance");
    for(float rotation:{0.f,45.f,90.f})for(bool mirror:{false,true})for(bool mappedNormal:{false,true})
    {
        Fixture axes;axes.meshes={plane()};SceneInstance object;object.mesh=object.material=0;
        object.transform.rotate(rotation,0,0,1);object.transform.rotate(27,0,1,0);
        object.transform.scale(mirror?-2.f:2.f,.6f,1.7f);axes.instances={object};
        axes.materials[0].anisotropic=.9f;axes.materials[0].normalTex=mappedNormal?1:-1;
        axes.prepare();gpu.scene(axes);
        const auto n=axes.instances[0].inverse.transposed().mapVector({0,0,1}).normalized();
        const auto t=object.transform.mapVector({1,0,0}).normalized();
        const auto shading=mappedNormal?(n+t).normalized():n;
        const auto axis=(t-shading*QVector3D::dotProduct(shading,t)).normalized();
        const auto values=gpu.run({{n*4,-n}},"HitResult h=hitBVH(r);outputColor=h.material.tangent;");
        require(QVector3D::dotProduct({values[0],values[1],values[2]},axis)>.9999f &&
                values[3]==(mirror?-1.f:1.f),"Rotated/nonuniform/normal-mapped BSDF author frame mismatch");
    }
    gpu.scene(f);
    f.instances.clear();
    f.materials[0].alphaMode = 1;
    f.materials[0].mediumtype = 1;
    f.materials[0].mediumDensity = 2;
    f.materials[0].mediumColor = QVector3D(.5f, .5f, .5f);
    for (int i = 0; i < 2; ++i)
    {
        SceneInstance boundary;
        boundary.mesh = i ? 0 : 1;
        boundary.material = 0;
        boundary.transform.translate(0, 0, i ? 3 : 1);
        boundary.transform.scale(10, 10, 1);
        f.instances.push_back(boundary);
    }
    f.prepare();
    gpu.scene(f);
    const auto shadow =
        gpu.run({{{0, 0, 0}, {0, 0, 1}}}, "MediumStack "
                                          "media;media.size=0;outputColor=vec4(ShadowTransmittance(r."
                                          "startPoint,vec3(0),r.direction,4,media),1);");
    for (int c = 0; c < 3; ++c)
        require(std::abs(shadow[c] - std::exp(-2.f)) < 1e-4f,
                "Instanced shadow material lost medium extinction");
    std::cout << "GPU normal maps and Beer-Lambert shadow through instanced absorber passed\n";
}
void grazingShadingNormals(Gpu &gpu)
{
    for (bool mapped : {false, true})
        for (bool mirrored : {false, true})
        {
            Fixture f;
            auto mesh = plane();
            const auto authored = QVector3D(1, 0, .1f).normalized();
            if (!mapped)
                for (auto &t : mesh->triangles)
                    t.n1 = t.n2 = t.n3 = authored;
            f.meshes = {mesh};
            SceneInstance instance;
            instance.mesh = instance.material = 0;
            instance.transform.rotate(34, QVector3D(0, 1, 0));
            instance.transform.scale(mirrored ? -2.f : 2.f, .6f, 1.7f);
            f.instances = {instance};
            if (mapped)
                f.materials[0].normalTex = 1;
            f.prepare();
            gpu.scene(f);
            const auto geometry = instance.transform.inverted().transposed().mapVector(QVector3D(0, 0, 1)).normalized();
            const auto tangent = instance.transform.mapVector(QVector3D(1, 0, 0)).normalized();
            const auto grazing = geometry * .2f - tangent * 4;
            // The authored tangent keeps its direction on the back face, unlike
            // the interpolated normal. Aim against the mapped tangent on both sides.
            const auto rearGrazing = mapped ? -geometry * .2f - tangent * 4 : -grazing;
            const std::vector<TestRay> rays = {{grazing, -grazing.normalized()},
                                               {rearGrazing, -rearGrazing.normalized()},
                                               {geometry * 4, -geometry}};
            const auto pixels = gpu.run(rays,
                "HitResult h=hitBVH(r);outputColor=vec4(h.normal,h.isInside?1:0);");
            for (int side = 0; side < 2; ++side)
            {
                const QVector3D actual(pixels[4 * side], pixels[4 * side + 1], pixels[4 * side + 2]);
                require(QVector3D::dotProduct(actual, side ? -geometry : geometry) > .9999f,
                        "Grazing normal did not fall back: mapped=" + std::to_string(mapped) +
                            " mirrored=" + std::to_string(mirrored) + " side=" + std::to_string(side));
                require(pixels[4 * side + 3] == float(side), "Normal repair changed medium side");
            }
            const auto expected = mapped ? (geometry + tangent).normalized()
                : instance.transform.inverted().transposed().mapVector(authored).normalized();
            require(QVector3D::dotProduct(QVector3D(pixels[8], pixels[9], pixels[10]), expected) > .9999f,
                    "Valid smooth/mapped normal was flattened");
        }
    std::cout << "GPU grazing smooth/mapped normals preserve valid shading and front/back medium sides\n";
}
void surfaceReconstruction(Gpu &gpu)
{
    Fixture f;
    f.meshes = {plane()};
    SceneInstance instance;
    instance.mesh = instance.material = 0;
    instance.transform.translate(.123456f, -.234567f, .345678f);
    instance.transform.rotate(37, QVector3D(1, 2, 3));
    instance.transform.scale(-.7f, 1.3f, .4f);
    f.instances = {instance};
    f.prepare();
    gpu.scene(f);
    const auto geometry = instance.transform.inverted().transposed().mapVector(QVector3D(0, 0, 1)).normalized();
    const auto tangent = instance.transform.mapVector(QVector3D(1, 0, 0)).normalized();
    std::vector<TestRay> rays;
    for (float distance : {20.f, 200.f, 2000.f})
        for (float side : {-1.f, 1.f})
            for (int i = 0; i < 16; ++i)
            {
                const auto point = instance.transform.map(QVector3D(-.7f + .09f * i, .123f, 0));
                const auto direction = (geometry * side + tangent * .7f).normalized();
                rays.push_back({point + direction * distance, -direction});
            }
    const auto pixels = gpu.run(rays,
        "HitResult h=hitBVH(r);vec3 n=h.isInside?-h.geometricNormal:h.geometricNormal;"
        "Ray secondary;secondary.startPoint=OffsetRayOrigin(h.hitPoint,h.geometricNormal,n);secondary.direction=n;"
        "HitResult again=hitBVH(secondary);"
        "vec3 local=(InstanceMatrix(0,4)*vec4(h.hitPoint,1)).xyz;"
        "outputColor=vec4(h.isHit?1:0,abs(local.z),again.isHit?1:0,h.hitDistance);");
    for (size_t i = 0; i < rays.size(); ++i)
    {
        require(pixels[i * 4] == 1, "Precision fixture missed its plane");
        require(pixels[i * 4 + 1] < 2e-6f, "Reconstructed hit left the triangle plane");
        require(pixels[i * 4 + 2] == 0, "Outward secondary ray self-intersected after reconstruction");
        require(pixels[i * 4 + 3] > 0, "Reconstruction lost ray distance");
    }
    std::cout << "GPU long-ray surface reconstruction: 96 front/back rays, no self-intersections\n";
}
void islandEdgeHits(Gpu &gpu)
{
    // Minimal front-surface triangles/rays captured from the two remaining island
    // black pixels. Moller-Trumbore rejected them and exposed the boat interior.
    auto mesh = std::make_shared<MeshGeometry>();
    const QVector3D vertices[][3] = {
        {{-.107321f,.202763f,.050457f},{-.049725f,.206243f,.056664f},{-.100586f,.203407f,.035539f}},
        {{-.105568f,.354137f,-.237264f},{-.079036f,.347426f,-.251262f},{-.078182f,.356149f,-.254705f}}};
    for (const auto &v : vertices)
    {
        Triangle t;
        t.p1 = v[0]; t.p2 = v[1]; t.p3 = v[2];
        t.n1 = t.n2 = t.n3 = QVector3D::crossProduct(t.p2-t.p1,t.p3-t.p1).normalized();
        mesh->triangles.push_back(t);
    }
    mesh->build();
    Fixture f;
    f.meshes = {mesh};
    SceneInstance instance; instance.mesh = instance.material = 0;
    f.instances = {instance}; f.prepare(); gpu.scene(f);
    const QVector3D eye(9.38f,13.12f,-6);
    const std::vector<TestRay> rays = {
        {eye,{-.5530031323432922f,-.7544493675231934f,.3535445034503937f}},
        {eye,{-.5601868033409119f,-.7551976442337036f,.3403927683830261f}}};
    const float expected[] = {17.119807f,16.908560f};
    for (bool picking : {false,true})
    {
        const auto pixels = gpu.run(rays, hitBody, picking);
        for (int i=0;i<2;++i)
        {
            require(pixels[4*i] > 0, "Island near-edge front surface was missed");
            require(std::abs(pixels[4*i+1]-expected[i]) < 3e-5f, "Island front hit distance changed");
        }
    }
    const auto shadow = gpu.run(rays,"HitResult h=hitBVH(r,true);outputColor=vec4(h.isHit?1:0,h.hitDistance,0,0);");
    for (int i=0;i<2;++i) require(shadow[4*i] == 1, "Shadow traversal missed island edge");
    std::cout << "GPU island edge fixtures hit the front surfaces in beauty/picking/shadow paths\n";
}
void collapsedWorldEdges(Gpu &gpu)
{
    for (bool mirrored : {false, true})
    {
        Fixture f; f.meshes = {plane()};
        SceneInstance instance; instance.mesh = instance.material = 0;
        instance.transform.translate(16777216.f, 16777216.f, 0);
        instance.transform.scale(mirrored ? -.001f : .001f, .001f, .001f);
        f.instances = {instance}; f.prepare(); gpu.scene(f);
        const auto pixels = gpu.run({{{16777216.f,16777216.f,1},{0,0,-1}}},
            "HitResult h=hitBVH(r);outputColor=vec4(h.isHit?1:0,h.geometricNormal.z,h.normal.z,h.hitDistance);");
        require(pixels[0] == 1 && std::isfinite(pixels[1]) && std::isfinite(pixels[2]),
                "Collapsed world edges produced a non-finite surface normal");
        require(std::abs(pixels[1]-1) < 1e-5f && std::abs(pixels[2]-1) < 1e-5f &&
                    std::abs(pixels[3]-1) < 1e-5f, "Local face normal or mirrored hit orientation changed");
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const auto invalid = gpu.run({{{nan,0,1},{0,0,-1}},{{0,0,1},{inf,0,-1}},{{0,0,1},{0,0,0}}},
        "HitResult h=hitBVH(r);outputColor=vec4(h.isHit?1:0,0,0,0);");
    for (int i=0;i<3;++i) require(invalid[4*i] == 0, "Invalid ray entered BVH traversal");
    std::cout << "GPU collapsed world edges, mirrored normals and invalid ray rejection passed\n";
}
void sharedEdgesAndGaps(Gpu &gpu)
{
    Fixture f; f.meshes = {plane(8)};
    SceneInstance instance; instance.mesh = instance.material = 0;
    for (bool mirrored : {false,true})
    {
        instance.transform.setToIdentity();
        instance.transform.rotate(21,QVector3D(1,2,3));
        instance.transform.scale(mirrored ? -1.7f : 1.7f,.8f,1.3f);
        f.instances = {instance}; f.prepare(); gpu.scene(f);
        const auto n=f.instances[0].inverse.transposed().mapVector(QVector3D(0,0,1)).normalized();
        const auto tangent=instance.transform.mapVector(QVector3D(1,0,0)).normalized();
        std::vector<TestRay> rays;
        for (int y=-2;y<=2;++y) for (int x=-2;x<=2;++x)
            for (float side : {-1.f,1.f}) for(float slope : {0.f,20.f})
            {
                const auto point=instance.transform.map(QVector3D(x*.25f,y*.25f,0));
                const auto direction=(n*side+tangent*slope).normalized();
                rays.push_back({point+direction*10,-direction});
            }
        const auto pixels=gpu.run(rays,hitBody);
        for(size_t i=0;i<rays.size();++i)
            require(pixels[4*i]>0 && std::abs(pixels[4*i+1]-10.f)<1e-3f,
                    "Shared vertex/edge failed: ray=" + std::to_string(i) +
                        " mirrored=" + std::to_string(mirrored) + " surface=" +
                        std::to_string(pixels[4*i]) + " distance=" + std::to_string(pixels[4*i+1]));
    }
    // A real slit must remain open; no barycentric epsilon should fill it.
    f.meshes={plane()}; f.instances.clear();
    for(float side : {-1.f,1.f})
    {
        SceneInstance part;part.mesh=part.material=0;
        part.transform.translate(side*.50005f,0,0);part.transform.scale(.49995f,1,1);
        f.instances.push_back(part);
    }
    f.prepare();gpu.scene(f);
    const auto gap=gpu.run({{{0,0,2},{0,0,-1}}},hitBody);
    require(gap[0]==0,"Robust intersection closed a real geometry gap");
    const auto degenerate=gpu.run({{{0,0,2},{0,0,-1}},{{0,0,2},{1,0,0}},{{0,0,2},{0,0,1}}},
        "TriangleRay tr=PrepareTriangleRay(r);vec3 bary;float d;"
        "bool bad=IntersectTriangle(tr,vec3(0),vec3(1,0,0),vec3(2,0,0),bary,d);"
        "bool hit=IntersectTriangle(tr,vec3(-1,-1,0),vec3(1,-1,0),vec3(0,1,0),bary,d);"
        "outputColor=vec4(bad?1:0,hit?1:0,0,0);");
    for(int i=0;i<3;++i)
    {
        require(degenerate[4*i]==0,"Degenerate triangle produced an intersection");
        require(degenerate[4*i+1]==(i==0?1.f:0.f),"Parallel or backward triangle test failed");
    }
    std::cout << "GPU shared edges/vertices, grazing/mirrored rays, real gaps and degenerates passed\n";
}
void boundedRayOrigins(Gpu &gpu)
{
    for(float scale:{.0001f,1.f,10000.f})for(float translation:{0.f,10000.f,-10000.f})
    {
        Fixture f;f.meshes={plane()};
        SceneInstance a,b;a.mesh=b.mesh=0;a.material=b.material=0;
        a.transform.translate(translation,0,.1f*scale);
        a.transform.scale(scale,scale*.7f,scale*1.3f);
        b.transform.translate(translation,0,(.1f+.0001f)*scale);
        b.transform.scale(-scale,scale*.7f,scale*1.3f);
        f.instances={a,b};f.prepare();gpu.scene(f);
        const auto values=gpu.run({{{translation,0,(.1f+.00005f)*scale},{0,0,-1}}},
            "HitResult h=hitBVH(r);r.startPoint=OffsetRayOrigin(h.hitPoint,h.positionError,h.geometricNormal,vec3(0,0,1));"
            "r.direction=vec3(0,0,1);HitResult next=hitBVH(r);outputColor=vec4(float(next.isHit),float(next.triangleIndex),h.positionError.z,1);");
        require(values[0]==1 && f.surfaceInstances.at(int(values[1]))==1u,
                "Error-bound origin skipped adjacent instance or re-hit its source");
    }
    std::cout<<"GPU error-bound origins: scaled, far-origin, mirrored/nonuniform instances passed\n";
    for(float scale:{.0001f,1.f,10000.f}) {
        Fixture f;f.meshes={plane()};SceneInstance object;object.mesh=object.material=0;
        object.transform.scale(-scale,scale*.7f,scale*1.3f);
        f.instances={object};f.prepare();gpu.scene(f);
        std::vector<TestRay> rays;
        for(int i=0;i<128;++i) {
            const float angle=float(i)*.213f;
            const QVector3D direction=QVector3D(std::cos(angle),std::sin(angle),.2f+float(i%7)*.15f).normalized();
            rays.push_back({{0,0,0},direction});
            rays.push_back({-direction*(scale*.0001f),direction});
        }
        const auto values=gpu.run(rays,hitBody);
        for(size_t i=0;i<rays.size();++i)
            require((values[4*i]>0)==(i%2==1),
                "Oblique zero-plane ray self-intersected or lost a nearby real hit: scale="+
                    std::to_string(scale)+" ray="+std::to_string(i)+" t="+std::to_string(values[4*i+1]));
    }
    std::cout<<"GPU oblique zero-plane origins: no self hits; scaled nearby hits preserved\n";
}
void shadowAnyHitProfile(Gpu &gpu)
{
    auto mesh=std::make_shared<MeshGeometry>();
    for(int i=0;i<128;++i) {
        Triangle t;const float offset=i*.02f;
        t.p1={-3,-3,offset};t.p2={3,-3,4+offset};t.p3={0,3,4+offset};
        t.n1=t.n2=t.n3=QVector3D::crossProduct(t.p2-t.p1,t.p3-t.p1).normalized();
        mesh->triangles.push_back(t);
    }
    mesh->build();Fixture f;f.meshes={mesh};SceneInstance instance;instance.mesh=0;instance.material=0;
    f.instances={instance};f.prepare();gpu.scene(f);
    std::vector<TestRay> rays;
    for(int y=0;y<20;++y)for(int x=0;x<20;++x)rays.push_back({{(x+.3f)/20-.5f,(y+.6f)/20-.5f,-.1f},{0,0,1}});
    gpu.extraDefines="#define TRACE_TRAVERSAL_PROFILE 1\n";
    const auto closest=gpu.run(rays,"HitResult h=hitBVH(r,true);outputColor=vec4(float(h.isHit&&h.hitDistance<3.4),float(traversalNodeVisits),float(traversalTriangleTests),float(traversalEarlyExits));");
    const auto any=gpu.run(rays,"HitResult h=hitBVH(r,true,3.4,true,-1);outputColor=vec4(float(h.isHit),float(traversalNodeVisits),float(traversalTriangleTests),float(traversalEarlyExits));");
    double closestNodes=0,anyNodes=0,closestTriangles=0,anyTriangles=0;
    for(int i=0;i<Gpu::size*Gpu::size;++i) {
        require(closest[i*4]==any[i*4],"AnyHit visibility differs from nearest occlusion");
        closestNodes+=closest[i*4+1];anyNodes+=any[i*4+1];
        closestTriangles+=closest[i*4+2];anyTriangles+=any[i*4+2];
        require(any[i*4+3]==any[i*4],"AnyHit did not record its early exit");
    }
    require(anyNodes<closestNodes && anyTriangles<closestTriangles,"AnyHit did not reduce overlap-fixture traversal");
    const auto shortRay=gpu.run(rays,"HitResult h=hitBVH(r,true,.05,true,-1);outputColor=vec4(float(h.isHit),0,0,0);");
    for(int i=0;i<Gpu::size*Gpu::size;++i)require(shortRay[i*4]==0,"Geometry beyond maxDistance cast a shadow");
    Fixture alpha;alpha.meshes={plane()};alpha.instances={instance};alpha.prepare();
    for(int mode:{2,3}) {
        alpha.materials[0].alphaMode=mode;alpha.materials[0].opacity=mode==2?.4f:.25f;
        gpu.scene(alpha);
        const auto values=gpu.run({{{.21f,-.23f,-1},{0,0,1}}},"HitResult h=hitBVH(r,true,2.0,true,-1);outputColor=vec4(float(h.isHit),0,0,0);");
        double fraction=0;for(int i=0;i<Gpu::size*Gpu::size;++i)fraction+=values[i*4]/(Gpu::size*Gpu::size);
        require(mode==2?fraction==0:std::abs(fraction-.25)<.025,"AnyHit changed Mask/Blend alpha semantics");
    }
    alpha.materials[0].alphaMode=0;alpha.materials[0].opacity=1;gpu.scene(alpha);
    const auto excluded=gpu.run({{{.21f,-.23f,-1},{0,0,1}}},"HitResult first=hitBVH(r);HitResult h=hitBVH(r,true,2.0,true,first.triangleIndex);outputColor=vec4(float(h.isHit),0,0,0);");
    require(excluded[0]==0,"Target emitter surface self-occluded");
    gpu.shadowAny=gpu.shadowEligible=true;
    const auto medium=gpu.run({{{0,0,-1},{0,0,1}}},
        "MediumStack m;m.size=1;m.entries[0].type=MEDIUM_ABSORB;m.entries[0].density=2;m.entries[0].color=vec3(0);m.entries[0].g=0;"
        "outputColor=vec4(ShadowTransmittance(r.startPoint,vec3(0),r.direction,.05,m),1);");
    require(std::abs(medium[0]-std::exp(-.1))<1e-5,"AnyHit bypassed medium attenuation");
    gpu.shadowAny=gpu.shadowEligible=false;gpu.extraDefines.clear();
    QJsonObject result{{"fixture","128 overlapping tilted occluders; not a general speedup claim"},
        {"rays",Gpu::size*Gpu::size},{"closestNodeVisits",closestNodes},{"anyHitNodeVisits",anyNodes},
        {"closestTriangleTests",closestTriangles},{"anyHitTriangleTests",anyTriangles},
        {"visibilityEqual",true},{"maskBlendDistanceEmitterMediumCovered",true}};
    const auto output=qEnvironmentVariable("LEARNQT_TRAVERSAL_REPORT");
    if(!output.isEmpty()) {
        QFile file(output);require(file.open(QIODevice::WriteOnly),"Cannot write traversal profile");
        file.write(QJsonDocument(result).toJson());
    }
    std::cout<<"GPU AnyHit visits: nodes "<<closestNodes<<" -> "<<anyNodes<<", triangles "<<closestTriangles<<" -> "<<anyTriangles<<'\n';
}
} // namespace
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    try
    {
        Gpu gpu;
        if(app.arguments().contains("--shadow-profile")) {shadowAnyHitProfile(gpu);return 0;}
        randomized(gpu);
        tiesAndAlpha(gpu);
        normalsAndMedia(gpu);
        grazingShadingNormals(gpu);
        surfaceReconstruction(gpu);
        islandEdgeHits(gpu);
        collapsedWorldEdges(gpu);
        sharedEdgesAndGaps(gpu);
        boundedRayOrigins(gpu);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
