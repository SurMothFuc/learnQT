#include "LightingAudit.h"
#include "MaterialMaskTextures.h"
#include "MaterialTexturePlan.h"
#include "RenderEvidence.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
QString output;
QJsonObject report;
void appendPlane(std::vector<float> &geometry, float x, float z, float halfX, float halfY)
{
    const QVector3D p[] = {
        {x - halfX, -halfY, z}, {x + halfX, -halfY, z}, {x + halfX, halfY, z}, {x - halfX, halfY, z}};
    for (const auto ids : {std::array<int, 3>{0, 1, 2}, std::array<int, 3>{0, 2, 3}})
    {
        std::vector<float> t(80, 0);
        for (int v = 0; v < 3; ++v)
            for (int c = 0; c < 3; ++c)
                t[v * 4 + c] = p[ids[v]][c];
        t[14] = t[18] = t[22] = 1;
        t[28] = t[29] = t[30] = .7f;
        t[37] = 1.5f;
        t[45] = 1;
        for (int i = 54; i < 60; ++i)
            t[i] = -1;
        geometry.insert(geometry.end(), t.begin(), t.end());
    }
}
double mean(const std::vector<float> &data, int channel = 0)
{
    double sum = 0;
    for (size_t i = channel; i < data.size(); i += 4)
        sum += data[i];
    return sum / (data.size() / 4);
}
void rayErrors(Audit &gpu, const QString &baseline)
{
    std::vector<float> geometry;
    appendPlane(geometry, 10000, .1f, 1, 1);
    appendPlane(geometry, 10000, .101f, .08f, .2f);
    gpu.setGeometry(geometry);
    gpu.setLights({}, 0);
    const QString prefix = R"(void main(){
        vec2 p=gl_FragCoord.xy/vec2(width,height)-.5;
        Ray ray;ray.startPoint=vec3(10000.0+p.x*.6,p.y*.6,.1005);ray.direction=vec3(0,0,-1);
        HitResult h=hitBVH(ray);MediumStack media;media.size=0;
        vec3 tr=ShadowTransmittance(h.hitPoint,)",
                  suffix = R"(h.geometricNormal,vec3(0,0,1),.3,media,-1,-1);
        outputColor=vec4(tr*.75,1);})";
    const auto after = gpu.run(prefix + "h.positionError," + suffix, false);
    std::vector<float> before;
    if (!baseline.isEmpty())
    {
        Audit old(baseline);
        old.setGeometry(geometry);
        old.setLights({}, 0);
        before = old.run(prefix + suffix, false);
    }
    else
        before = after;
    gpu.context.makeCurrent(&gpu.surface);
    check(mean(after) < .7 && mean(after) > .4, "Far-origin thin blocker was lost");
    check(
        saveEvidence(
            output, "R-C03", evidenceImage(before, 256, 256), evidenceImage(after, 256, 256),
            "Actual GPU visibility: 0.001-unit thin gap at x=10000. Error-bound offsets retain the blocker."),
        "save ray error comparison");
    QJsonArray cases;
    for (float scale : {.0001f, 1.f, 10000.f})
        for (float x : {0.f, 10000.f, -10000.f})
        {
            // Legacy vertices are already world floats; an edge below their ULP
            // cannot be recovered. Tiny far-origin faces are covered as instances.
            if (scale < 1.f && x != 0.f)
                continue;
            std::vector<float> g;
            appendPlane(g, x, .1f * scale, scale, scale);
            appendPlane(g, x, (.1f + .0001f) * scale, scale, scale);
            gpu.setGeometry(g);
            const auto values = gpu.mean(
                "void main(){Ray r;r.startPoint=vec3(" + QString::number(x, 'g', 10) + ",0," +
                    QString::number((.1f + .00005f) * scale, 'g', 10) +
                    ");r.direction=vec3(0,0,-1);HitResult h=hitBVH(r);"
                    "r.startPoint=OffsetRayOrigin(h.hitPoint,h.positionError,h.geometricNormal,vec3(0,0,1));"
                    "r.direction=vec3(0,0,1);HitResult next=hitBVH(r);"
                    "outputColor=vec4(float(next.isHit),float(next.triangleIndex>=2),h.positionError.z,1);}",
                false);
            check(values[0] == 1 && values[1] == 1, "Scaled thin boundary skipped or self-intersected");
            cases.append(QJsonObject{{"scale", scale}, {"x", x}, {"errorZ", values[2]}});
        }
    report["R-C03"] =
        QJsonObject{{"cases", cases},
                    {"beforeVisibility", mean(before)},
                    {"afterVisibility", mean(after)},
                    {"basis", "gamma(7) interpolation; gamma(3) affine transform; outward float rounding"}};
}
void shadingNormals(Audit &gpu, const QString &baseline)
{
    std::vector<float> geometry;
    appendPlane(geometry, 0, 0, 1, 1);
    gpu.setGeometry(geometry);
    const QString setup = R"(HitResult h;h.normal=normalize(vec3(.94,0,.342));h.geometricNormal=vec3(0,0,1);
        h.viewDir=vec3(0,0,-1);h.material=getMaterial(0);h.material.IOR=1.0;
        vec3 L=normalize(vec3(cos(gl_FragCoord.x/float(width)*TWO_PI)*sqrt(1-z*z),
                             sin(gl_FragCoord.x/float(width)*TWO_PI)*sqrt(1-z*z),z));float pdf;)",
                  prefix = "void main(){float z=gl_FragCoord.y/float(height)*2.0-1.0;";
    const auto after = gpu.run(
        prefix + setup +
            "vec3 f=EvaluateSurfaceBSDF(h,L,1.0,pdf);outputColor=vec4(f*abs(dot(h.normal,L))*2.0,1);}",
        false);
    std::vector<float> before;
    if (!baseline.isEmpty())
    {
        Audit old(baseline);
        old.setGeometry(geometry);
        before = old.run(prefix + setup +
                             "vec3 "
                             "f=DisneyEval(-h.viewDir,h.normal,L,h.material,1.0,pdf);outputColor=vec4(f*abs("
                             "dot(h.normal,L))*2.0,1);}",
                         false);
    }
    else
        before = after;
    gpu.context.makeCurrent(&gpu.surface);
    double invalidEnergy = 0;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 256; ++x)
            invalidEnergy += after[(y * 256 + x) * 4];
    check(invalidEnergy == 0, "Opaque back-side reflection leaked energy");
    check(saveEvidence(output, "R-C04", evidenceImage(before, 256, 256), evidenceImage(after, 256, 256),
                       "GPU directional BSDF response with a 70-degree shading normal. Back-side reflection "
                       "is excluded."),
          "save normal comparison");
    QJsonArray cases;
    for (float angle : {0.f, .6f, 1.1f})
        for (float transmission : {0.f, .65f})
            for (int side : {1, -1})
            {
                const QString fixture =
                    "HitResult "
                    "h;h.material=getMaterial(0);h.material.roughness=.6;h.material.ax=h.material.ay=.6;"
                    "h.material.transmission=" +
                    QString::number(transmission) + ";h.normal=" + QString::number(side) + ".0*vec3(sin(" +
                    QString::number(angle) + "),0,cos(" + QString::number(angle) +
                    "));"
                    "h.geometricNormal=vec3(0,0,1);h.viewDir=vec3(0,0," +
                    QString::number(-side) + ");float eta=" + QString::number(side > 0 ? 1. / 1.5 : 1.5) +
                    ";";
                const auto sampled = gpu.mean("void main(){" + fixture + R"(
            BsdfSample s=SampleSurfaceBSDF(h,eta,vec3(rand(),rand(),rand()));
            float p;vec3 f=EvaluateSurfaceBSDF(h,s.direction,eta,p);
            vec3 weight=p>0.0?f*abs(dot(h.normal,s.direction))/p:vec3(0);
            bool valid=ValidSurfaceScatter(-h.viewDir,h.normal,h.geometricNormal,s.direction);
            outputColor=vec4(s.weight.r,float(!valid&&maxComponent(s.weight)>0.0),length(weight-s.weight),float(s.pdf>0.0));})",
                                              false);
                const auto integrated = gpu.mean("void main(){" + fixture + R"(
            vec2 total=vec2(0);
            // Refractive Jacobians need finer quadrature than the display grid.
            for(int x=0;x<4;++x)for(int y=0;y<4;++y) {
                vec2 q=gl_FragCoord.xy-.5+(vec2(x,y)+.5)/4.0;
                float z=q.x/float(width)*2.0-1.0,phi=q.y/float(height)*TWO_PI;
                vec3 L=vec3(sqrt(1-z*z)*cos(phi),sqrt(1-z*z)*sin(phi),z);float p;
                vec3 f=EvaluateSurfaceBSDF(h,L,eta,p);
                total+=vec2(f.r*abs(dot(h.normal,L)),p)*4.0*PI/16.0;
            }
            outputColor=vec4(total,0,1);})",
                                                 false);
                check(sampled[1] == 0 && sampled[2] < 1e-5, "Normal-contract sample/eval mismatch");
                checkNear(sampled[0], integrated[0], .035, "shading-normal transport integral");
                checkNear(sampled[3], integrated[1], .025, "normal-contract continuous PDF mass");
                cases.append(QJsonObject{{"angleRadians", angle},
                                         {"transmission", transmission},
                                         {"side", side},
                                         {"sampledWeight", sampled[0]},
                                         {"integratedWeight", integrated[0]},
                                         {"validPdfMass", integrated[1]}});
            }
    report["R-C04"] = QJsonObject{
        {"cases", cases},
        {"invalidBacksideEnergy", invalidEnergy},
        {"contract",
         "Sample/Eval/MIS share geometric-side validation; rejected directions are null probability mass"},
        {"limits", "No universal shading-normal energy compensation or terminator smoothing is claimed"}};
}
void textureAntialiasing(Audit &gpu, const QString &baseline)
{
    QImage normals(512, 512, QImage::Format_RGBA8888), mask(normals.size(), normals.format());
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x)
        {
            normals.setPixelColor(x, y, QColor(x & 1 ? 230 : 26, 128, 204, 255));
            mask.setPixelColor(x, y, QColor(255, 255, 255, x % 8 == 1 ? 255 : 0));
        }
    auto render = [&](Audit &g, const QImage &source, bool coverage, const QString &body, bool average) {
        g.context.makeCurrent(&g.surface);
        GLuint texture = 0, buffer = 0, info = 0;
        g.glGenTextures(1, &texture);
        g.glActiveTexture(GL_TEXTURE5);
        g.glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
        g.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        g.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        g.glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA16F, 512, 512, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
        const auto pixels = prepareMaterialTexturePixels(source, {512, 512}, false);
        g.glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, 512, 512, 1, GL_RGBA, GL_FLOAT, pixels.data());
        g.glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        if (coverage)
        {
            const auto chain = prepareMaskTextureMips(source, {512, 512}, false, {3, .5f});
            for (int level = 1; level < int(chain.size()); ++level)
                g.glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, 0, chain[level].size.width(),
                                  chain[level].size.height(), 1, GL_RGBA, GL_FLOAT,
                                  chain[level].pixels.data());
            checkNear(chain.back().pixels[3], .125, 1e-6, "Mask mip mean preserves exact cutoff coverage");
        }
        g.buffer(buffer, info, 6, GL_RGBA32F,
                 {1, 1, 0, 0, 0, 0, 0, 0, 9987, 9729, 0, 1, .5f, 3, coverage ? 1.f : 0.f, 0});
        g.textureCount = 1;
        g.textureInfoStride = 4;
        std::vector<float> result(256 * 256 * 4, 0);
        const int count = average ? 16 : 1;
        for (int i = 0; i < count; ++i)
        {
            g.sampleIndex = unsigned(i + 7);
            const auto values = g.run(body, false);
            for (size_t k = 0; k < result.size(); ++k)
                result[k] += values[k] / count;
        }
        g.sampleIndex = 7;
        g.textureCount = 0;
        g.textureInfoStride = 0;
        g.glDeleteTextures(1, &texture);
        g.glDeleteTextures(1, &info);
        g.glDeleteBuffers(1, &buffer);
        return result;
    };
    std::vector<float> geometry;
    appendPlane(geometry, 0, 0, 1, 1);
    for (size_t p = 0; p < geometry.size(); p += 80)
    {
        geometry[p + 43] = 1;
        geometry[p + 45] = .05f;
        geometry[p + 55] = 0;
        geometry[p + 62] = 1;
    }
    gpu.setGeometry(geometry);
    const QString normalBody = R"(void main(){Material m=getMaterial(0);vec3 N=vec3(0,0,1);
        materialEvaluationFootprint=vec2(16.0/512.0);
        N=ApplyNormalMap(0,vec2(.5),vec3(.3,.3,.4),N,m);
        vec2 p=gl_FragCoord.xy/vec2(width,height)-.5;vec3 L=normalize(vec3(p*2.0,1));float pdf;
        vec3 f=DisneyEval(vec3(0,0,1),N,L,m,1.0/1.5,pdf);outputColor=vec4(f*max(dot(N,L),0.0),1);})";
    const auto normalAfter = render(gpu, normals, false, normalBody, false);
    std::vector<float> normalBefore = normalAfter;
    if (!baseline.isEmpty())
    {
        Audit old(baseline);
        old.setGeometry(geometry);
        normalBefore = render(old, normals, false, normalBody, false);
    }
    gpu.context.makeCurrent(&gpu.surface);
    for (size_t p = 0; p < geometry.size(); p += 80)
    {
        geometry[p + 43] = 0;
        geometry[p + 55] = -1;
        geometry[p + 54] = 0;
        geometry[p + 39] = 2;
        geometry[p + 60] = 1;
        geometry[p + 61] = .5;
        for (int v = 0; v < 3; ++v)
        {
            geometry[p + 48 + v * 2] = (geometry[p + v * 4] + 1) * .5f;
            geometry[p + 49 + v * 2] = (geometry[p + v * 4 + 1] + 1) * .5f;
        }
    }
    gpu.setGeometry(geometry);
    const QString maskBody =
        R"(void main(){Ray r;r.startPoint=vec3(gl_FragCoord.xy/vec2(width,height)*2.0-1.0,1);
        r.direction=vec3(0,0,-1);rayConeSpread=.04;HitResult h=hitBVH(r);outputColor=vec4(vec3(h.isHit?1:0),1);})";
    const auto maskAfter = render(gpu, mask, true, maskBody, true);
    std::vector<float> maskBefore = maskAfter;
    if (!baseline.isEmpty())
    {
        Audit old(baseline);
        old.setGeometry(geometry);
        maskBefore = render(old, mask, false, maskBody, true);
    }
    gpu.context.makeCurrent(&gpu.surface);
    checkNear(mean(maskAfter), .125, .004, "minified GPU Mask coverage");
    QImage before(256, 512, QImage::Format_RGB32), after(before.size(), before.format());
    QPainter a(&before);
    a.drawImage(0, 0, evidenceImage(normalBefore, 256, 256));
    a.drawImage(0, 256, evidenceImage(maskBefore, 256, 256));
    a.end();
    QPainter b(&after);
    b.drawImage(0, 0, evidenceImage(normalAfter, 256, 256));
    b.drawImage(0, 256, evidenceImage(maskAfter, 256, 256));
    b.end();
    check(saveEvidence(output, "R-Q03", before, after,
                       "GPU fixtures: filtered normal highlight (top); 16-spp sparse Mask (bottom), target "
                       "coverage 12.5%."),
          "save texture AA comparison");
    TextureAsset asset;
    asset.image = mask;
    asset.width = asset.height = 512;
    Material m;
    m.alphaMode = Mask;
    m.baseColorTex = 0;
    m.alphaCutoff = .5;
    Material n = m;
    n.alphaCutoff = .25;
    const auto plan = planMaterialMaskTextures({asset}, {m, n});
    check(plan.textures.size() == 3 && plan.materialSources[0][0] != plan.materialSources[1][0],
          "Shared image cutoff views were conflated");
    report["R-Q03"] = QJsonObject{{"referenceCoverage", .125},
                                  {"beforeCoverage", mean(maskBefore)},
                                  {"afterCoverage", mean(maskAfter)},
                                  {"spp", 16},
                                  {"independentCutoffViews", true},
                                  {"limits", "Toksvig-style roughness approximation; two varying alpha "
                                             "sources and emissive Mask retain point semantics"}};
}
void texturePools(Audit &gpu)
{
    std::vector<TextureAsset> sources(9);
    std::vector<Material> materials(9);
    for (int i = 0; i < 9; ++i)
    {
        const int dimension = i == 0 ? 1024 : 64;
        sources[i].image = QImage(dimension, dimension, QImage::Format_RGBA8888);
        sources[i].image.fill(QColor(128, 64, 32));
        sources[i].width = sources[i].height = dimension;
        materials[i].baseColorTex = i;
    }
    const auto mask = planMaterialMaskTextures(sources, materials);
    const auto plan = planMaterialTextures(mask, materials, 4, 2048, 2048, 512ull * 1024 * 1024);
    check(plan.pools[0].size == QSize(64, 64) && plan.pools[0].layers == 8 &&
              plan.pools[1].size == QSize(1024, 1024),
          "Small source images were enlarged to the largest texture");
    const auto minimum = planMaterialTextures(mask, materials, 2, 2048, 2048, 512ull * 1024 * 1024);
    check(minimum.pools.size() == 2 && minimum.views.size() == 9,
          "GL 3.3 minimum sampler capacity lost views");
    const auto constrained = planMaterialTextures(mask, materials, 4, 2048, 2048, 4ull * 1024 * 1024);
    check(constrained.bytes <= 4ull * 1024 * 1024 && constrained.reduced, "Texture budget was not enforced");
    const auto limited = planMaterialTextures(mask, materials, 4, 2048, 1, 512ull * 1024 * 1024);
    check(limited.missingViews == 7, "Layer-limit scalar fallbacks were not recorded");
    gpu.context.makeCurrent(&gpu.surface);
    GLuint arrays[2] = {}, buffer = 0, info = 0;
    gpu.glGenTextures(2, arrays);
    for (int i = 0; i < 2; ++i)
    {
        const int dimension = i == 0 ? 512 : 32;
        const auto pixels = prepareMaterialTexturePixels(sources[1].image, {dimension, dimension}, i == 0);
        gpu.glActiveTexture(GL_TEXTURE5 + i * 2);
        gpu.glBindTexture(GL_TEXTURE_2D_ARRAY, arrays[i]);
        gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gpu.glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, i == 0 ? GL_RGBA16F : GL_RGBA8, dimension, dimension, 1, 0,
                         GL_RGBA, GL_FLOAT, pixels.data());
        gpu.glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }
    gpu.buffer(buffer, info, 6, GL_RGBA32F,
               {1, 1, 0, 0, 0, 0, 0, 0, 9987, 9729, 0, 1, 0, -1, 0, 0, 1, 0, 0, 0});
    gpu.textureCount = 1;
    gpu.textureInfoStride = 5;
    gpu.texturePoolCount = 2;
    gpu.extraDefines = "#define MATERIAL_TEXTURE_POOL_COUNT 2\n";
    const auto result = gpu.mean(R"(void main(){
        vec4 color=SampleMaterialColorTexture(0,vec2(.5));vec4 data=SampleMaterialTexture(0,vec2(.5));
        outputColor=vec4(color.r,data.r,MaterialTextureRho(0,vec2(.125)),1);})",
                                 false);
    checkNear(result[0], std::pow((128. / 255 + .055) / 1.055, 2.4), .0001, "pool color stays linear");
    checkNear(result[1], 128. / 255, 1e-6, "pool data stays raw");
    checkNear(result[2], 4, 1e-6, "footprint uses the selected pool dimensions");
    gpu.textureCount = 0;
    gpu.textureInfoStride = 0;
    gpu.texturePoolCount = 1;
    gpu.extraDefines.clear();
    gpu.glDeleteTextures(2, arrays);
    gpu.glDeleteTextures(1, &info);
    gpu.glDeleteBuffers(1, &buffer);
    report["R-A02"] = QJsonObject{{"fixtureTextureBytes", double(plan.bytes)},
                                  {"budgetAppliedBytes", double(constrained.bytes)},
                                  {"minimumSamplerCapacityCovered", true},
                                  {"layerLimitFallbacks", limited.missingViews},
                                  {"color", result[0]},
                                  {"data", result[1]}};
}
} // namespace
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    try
    {
        const auto args = app.arguments();
        output = args.value(1, "second-batch-evidence");
        if (args.contains("--compare-images"))
        {
            const int at = args.indexOf("--compare-images");
            output = args.value(at + 1);
            check(QDir().mkpath(output), "create comparison output");
            QImage before(args.value(at + 3)), after(args.value(at + 4));
            check(!before.isNull() && !after.isNull(), "comparison images are missing");
            check(saveEvidence(output, args.value(at + 2), before, after, args.value(at + 5)),
                  "save comparison");
            return 0;
        }
        check(QDir().mkpath(output), "create output directory");
        const int b = args.indexOf("--baseline");
        const auto baseline = b < 0 ? QString() : args.value(b + 1);
        Audit gpu;
        const auto only = args.value(args.indexOf("--only") + 1);
        if (!args.contains("--only") || only == "C03")
            rayErrors(gpu, baseline);
        const int n = args.indexOf("--normals-baseline");
        if (!args.contains("--only") || only == "C04")
            shadingNormals(gpu, n < 0 ? QString() : args.value(n + 1));
        const int t = args.indexOf("--textures-baseline");
        if (!args.contains("--only") || only == "Q03")
            textureAntialiasing(gpu, t < 0 ? QString() : args.value(t + 1));
        if (!args.contains("--only") || only == "A02")
            texturePools(gpu);
        QFile file(output + "/report.json");
        check(file.open(QIODevice::WriteOnly), "write evidence report");
        file.write(QJsonDocument(report).toJson());
        std::cout << "Second batch GPU tests passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
