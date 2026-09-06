#include "SceneGraph.h"
#include "common.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
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
                                   "bsdf", "light_sampling", "medium", "pathtrace"})
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
        QOpenGLShaderProgram program;
        const char *vertex =
            "#version 330 core\nout vec3 pix;void main(){vec2 "
            "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2-1;pix=vec3(p,0);gl_Position=vec4(p,0,1);}";
        require(program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertex), program.log().toStdString());
        const auto fragment =
            QString("#version 330 core\n#define INSTANCED_SCENE\nin vec3 pix;layout(location=0)out vec4 "
                    "outputColor;\nuniform samplerBuffer testRays;uniform int testRayCount;\n") +
            modules +
            "\nvoid main(){int id=(int(gl_FragCoord.y)*64+int(gl_FragCoord.x))%testRayCount;Ray "
            "r;r.startPoint=texelFetch(testRays,id*2).xyz;r.direction=texelFetch(testRays,id*2+1).xyz;" +
            body + "}\n";
        require(program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment),
                program.log().toStdString());
        require(program.link(), program.log().toStdString());
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
    f.prepare();
    gpu.scene(f);
    const auto normal = f.instances[0].inverse.transposed().mapVector(QVector3D(0, 0, 1)).normalized();
    const auto tangent = instance.transform.mapVector(QVector3D(1, 0, 0)).normalized();
    const auto expected = (normal + tangent).normalized();
    const auto mapped =
        gpu.run({{normal * 4, -normal}}, "HitResult h=hitBVH(r);outputColor=vec4(h.normal,1);");
    require(QVector3D::dotProduct(QVector3D(mapped[0], mapped[1], mapped[2]), expected) > .9999f,
            "Mirrored nonuniform normal-map tangent frame changed");
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
} // namespace
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    try
    {
        Gpu gpu;
        randomized(gpu);
        tiesAndAlpha(gpu);
        normalsAndMedia(gpu);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
