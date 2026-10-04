#include "LightingAudit.h"
#include "PathDiagnostics.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

namespace {
QString outputDirectory;
QJsonObject report;
void require(bool ok, const char *message) { check(ok, message); }
std::vector<float> materialGeometry() {
  std::vector<float> t(80, 0);
  t[28] = t[29] = t[30] = 1;
  t[36] = 0;
  t[37] = 1;
  t[45] = 1;
  t[60] = 1;
  t[62] = 1;
  t[54] = t[55] = -1;
  for (int i = 56; i < 60; ++i)
    t[i] = -1;
  return t;
}
double channelMean(const std::vector<float> &data, int channel) {
  double sum = 0;
  for (size_t i = channel; i < data.size(); i += 4)
    sum += data[i];
  return sum / (data.size() / 4);
}
QImage image(const std::vector<float> &data) {
  QImage result(Audit::resolution, Audit::resolution, QImage::Format_RGB32);
  for (int y = 0; y < result.height(); ++y)
    for (int x = 0; x < result.width(); ++x) {
      const size_t i = (size_t(y) * result.width() + x) * 4;
      auto c = [&](int k) {
        return qRound(
            std::pow(std::min(1.f, std::max(0.f, data[i + k])), 1.f / 2.2f) *
            255);
      };
      result.setPixel(x, result.height() - 1 - y, qRgb(c(0), c(1), c(2)));
    }
  return result;
}
void compare(const QString &name, const QImage &before, const QImage &after,
             const QString &left, const QString &right,
             const QString &description) {
  const int w = before.width(), h = before.height();
  QImage panel(w * 2, h + 100, QImage::Format_RGB32);
  panel.fill(QColor("#151b24"));
  QPainter painter(&panel);
  painter.setPen(Qt::white);
  painter.setFont(QFont("Segoe UI", 10));
  painter.drawText(QRect(12, 5, w - 24, 45),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, left);
  painter.drawText(QRect(w + 12, 5, w - 24, 45),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, right);
  painter.drawImage(0, 50, before);
  painter.drawImage(w, 50, after);
  painter.drawText(QRect(12, h + 53, w * 2 - 24, 45),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                   description);
  painter.end();
  require(before.save(outputDirectory + "/" + name + "-before.png"),
          "save before");
  require(after.save(outputDirectory + "/" + name + "-after.png"),
          "save after");
  require(panel.save(outputDirectory + "/" + name + "-comparison.png"),
          "save comparison");
}
const QString calibration = R"(
void main() {
    vec2 p=(gl_FragCoord.xy/vec2(width,height))*2.0-1.0;
    bool right=p.x>0.0, upper=p.y>0.0;
    vec2 q=vec2(fract(gl_FragCoord.x/(float(width)*.5)),
                fract(gl_FragCoord.y/(float(height)*.5)))*2.0-1.0;
    q*=1.08;
    if(dot(q,q)>1.0){outputColor=vec4(.015,.022,.033,1);return;}
    vec3 N=vec3(q,sqrt(1.0-dot(q,q)));
    Material m=getMaterial(0);
    m.baseColor=upper ? vec3(.82,.57,.23) : vec3(.2,.45,.75);
    m.metallic=upper?1.0:0.0;m.roughness=right?.5:.2;
    m.IOR=1.5;m.ax=m.ay=m.roughness;
    vec3 L=normalize(vec3(-.4,.6,1));float pdf;
    vec3 f=DisneyEval(vec3(0,0,1),N,L,m,1.0/1.5,pdf);
    outputColor=vec4(f*max(dot(N,L),0.0)*2.0+m.baseColor*.025,1);
})";
void materialBaseline(Audit &a, const QString &baseline) {
  QJsonArray results;
  a.setGeometry(materialGeometry());
  for (const QString &kind : {"diffuse", "metal", "mixed"})
    for (float roughness : {.35f, .7f, 1.f}) {
      QString setup =
          "Material m=getMaterial(0);m.baseColor=vec3(.7);m.roughness=" +
          QString::number(roughness) + ";m.ax=m.ay=m.roughness;m.metallic=" +
          (kind == "metal"   ? "1.0"
           : kind == "mixed" ? "0.4"
                             : "0.0") +
          ";vec3 V=normalize(vec3(.2,.1,1));";
      auto sampled = a.mean("void main(){" + setup + R"(
            BsdfSample s=SampleDisneyBSDF(V,vec3(0,0,1),m,1.0,vec3(rand(),rand(),rand()));
            float p;vec3 f=DisneyEval(V,vec3(0,0,1),s.direction,m,1.0,p);
            vec3 expected=p>0.0?f*abs(s.direction.z)/p:vec3(0);
            float error=length(expected-s.weight);
            outputColor=vec4(s.weight.r,error,float(s.delta),float(s.pdf>0.0));})");
      auto integral = a.mean("void main(){" + setup + R"(
            float z=(gl_FragCoord.x/float(width))*2.0-1.0;
            float phi=gl_FragCoord.y/float(height)*TWO_PI;
            vec3 L=vec3(sqrt(1-z*z)*cos(phi),sqrt(1-z*z)*sin(phi),z);
            float p;vec3 f=DisneyEval(V,vec3(0,0,1),L,m,1.0,p);
            outputColor=vec4(f.r*abs(z)*4.0*PI,p*4.0*PI,0,1);})");
      require(sampled[1] < 1e-5, "BSDF sample/eval weight mismatch");
      require(sampled[2] == 0, "rough BSDF unexpectedly delta");
      checkNear(sampled[3], integral[1], .01,
                "sampled valid mass vs integrated PDF " + kind.toStdString());
      checkNear(sampled[0], integral[0], .025,
                "sampled vs quadrature " + kind.toStdString());
      require(sampled[0] >= 0 && sampled[0] <= 1.05,
              "unexpected white-furnace response");
      results.append(QJsonObject{{"material", kind},
                                 {"roughness", roughness},
                                 {"sampledReflectance", sampled[0]},
                                 {"quadratureReflectance", integral[0]},
                                 {"pdfValidMass", integral[1]},
                                 {"sampleEvalError", sampled[1]}});
    }
  auto white = a.mean(R"(void main(){
        Material m=getMaterial(0);vec3 V=vec3(0,0,1);
        BsdfSample s=SampleDisneyBSDF(V,V,m,1.0,vec3(rand(),rand(),rand()));
        outputColor=vec4(s.weight,float(s.delta));})");
  checkNear(white[0], 29.0 / 28.0, .004,
            "Disney diffuse model integral (not unit-energy claim)");
  auto reciprocal = a.mean(R"(void main(){
        Material m=getMaterial(0);m.baseColor=vec3(.7);m.metallic=.4;m.ax=.3;m.ay=.6;
        vec3 V=normalize(vec3(.3,.1,1)),L=CosineSampleHemisphere(rand(),rand());
        float p,q;vec3 f=DisneyEval(V,vec3(0,0,1),L,m,1.0/1.5,p);
        vec3 g=DisneyEval(L,vec3(0,0,1),V,m,1.0/1.5,q);
        outputColor=vec4(length(f-g)/max(length(f)+length(g),1e-8),0,0,1);})");
  require(reciprocal[0] < 1e-5, "reflection reciprocity");
  // Delta mass is checked separately from continuous PDF normalization.
  auto delta = a.mean(R"(void main(){
        Material m=getMaterial(0);m.metallic=1;m.roughness=0;m.baseColor=vec3(.6);
        BsdfSample s=SampleDisneyBSDF(vec3(0,0,1),vec3(0,0,1),m,1.0/1.5,vec3(rand(),rand(),rand()));
        outputColor=vec4(s.weight.r,float(s.delta),s.pdf,1);})");
  checkNear(delta[0], .6, 1e-6, "delta mirror reflectance");
  checkNear(delta[1], 1, 0, "delta flag");
  checkNear(delta[2], 0, 0, "delta is not a continuous PDF");
  const auto after = a.run(calibration);
  std::vector<float> before;
  if (baseline.isEmpty())
    before = after;
  else {
    Audit previous(baseline);
    previous.setGeometry(materialGeometry());
    before = previous.run(calibration);
  }
  require(a.context.makeCurrent(&a.surface), "restore current audit context");
  require(before == after,
          "adding material baseline changed calibration pixels");
  compare("R-C01", image(before), image(after), "Before: existing BSDF",
          "After: BSDF + numerical baseline",
          "Same pixels expected: this task adds validation, not a material "
          "model change.");
  report["R-C01"] = QJsonObject{{"cases", results},
                                {"diffuseModelIntegral", white[0]},
                                {"reflectionReciprocityError", reciprocal[0]},
                                {"comparisonIdentical", true}};
}
void diagnostics(Audit &a) {
  a.context.makeCurrent(&a.surface);
  std::vector<float> geometry;
  for (int z = 1; z <= 130; ++z) {
    auto t = materialGeometry();
    const float points[] = {-20,      -20, float(z), 20,      -20,
                            float(z), 0,   20,       float(z)};
    for (int v = 0; v < 3; ++v)
      for (int c = 0; c < 3; ++c)
        t[v * 4 + c] = points[v * 3 + c];
    for (int v = 0; v < 3; ++v)
      t[(v + 3) * 4 + 2] = 1;
    t[39] = 1;
    geometry.insert(geometry.end(), t.begin(), t.end());
  }
  a.setGeometry(geometry);
  a.extraDefines = "#define BVH_STACK_CAPACITY 2\n";
  const auto faults = a.run(R"(void main(){
        if(gl_FragCoord.y>1.0){outputColor=vec4(0);return;}
        int band=int(gl_FragCoord.x*3.0/float(width));
        if(band==0) {
            Ray ray;ray.startPoint=vec3(uintBitsToFloat(0x7fc00000u),0,0);
            ray.direction=vec3(0,0,1);hitBVH(ray);
        } else if(band==1) {
            MediumStack stack;stack.size=MAX_MEDIA;
            HitResult hit;hit.material=getMaterial(0);hit.material.mediumtype=1;
            hit.isInside=false;hit.geometricNormal=vec3(0,0,1);
            CrossMediumBoundary(stack,hit,vec3(0,0,-1));
        } else {
            MediumStack stack;stack.size=0;
            ShadowTransmittance(vec3(0),vec3(0),vec3(0,0,1),200.0,stack);
        }
        outputColor=vec4(float(pathDiagnosticFlags),0,0,1);})",
                            false);
  const unsigned expected[] = {5, 16, 32};
  for (int i = 0; i < 3; ++i)
    checkNear(faults[((i * 2 + 1) * Audit::resolution / 6) * 4], expected[i], 0,
              "fault event flags " + std::to_string(i));
  // Force a bounded traversal to exhaust its pending-node capacity.
  std::vector<float> nodes(6 * 12, 0);
  auto node = [&](int id, int left, int right, int count) {
    int p = id * 12;
    nodes[p] = float(left);
    nodes[p + 1] = float(right);
    nodes[p + 3] = float(count);
    nodes[p + 6] = nodes[p + 7] = -20;
    nodes[p + 8] = .1f;
    nodes[p + 9] = nodes[p + 10] = 20;
    nodes[p + 11] = 200;
  };
  node(1, 2, 3, 0);
  node(2, 4, 5, 0);
  node(3, 0, 0, 1);
  node(4, 0, 0, 1);
  node(5, 0, 0, 1);
  nodes[3 * 12 + 8] = .2f;
  a.buffer(a.nodeBuffer, a.nodeTexture, 3, GL_RGB32F, nodes);
  const auto overflow = a.run(R"(void main(){
        Ray ray;ray.startPoint=vec3(0);ray.direction=vec3(0,0,1);hitBVH(ray);
        outputColor=vec4(float(pathDiagnosticFlags),0,0,1);})",
                              false);
  checkNear(channelMean(overflow, 0), 8, 0, "BVH overflow marker");
  a.extraDefines.clear();
  a.setGeometry(materialGeometry());
  const auto rejected = a.mean(R"(void main(){
        bool valid=ValidTraceSample(vec4(uintBitsToFloat(0x7fc00000u)),vec4(0),vec4(0));
        outputColor=vec4(float(valid),float(pathDiagnosticFlags),0,1);})");
  require(rejected[0] == 0 && rejected[1] == 3,
          "non-finite sample rejection markers");
  QImage before(512, 256, QImage::Format_RGB32),
      after(before.size(), before.format());
  before.fill(QColor("#101722"));
  after.fill(QColor("#101722"));
  const char *names[] = {"Non-finite ray", "Medium overflow", "Boundary limit",
                         "BVH overflow"};
  QPainter p(&after);
  p.setFont(QFont("Segoe UI", 9));
  p.setPen(Qt::white);
  for (int i = 0; i < 4; ++i) {
    const unsigned flag =
        i < 3 ? unsigned(faults[((i * 2 + 1) * Audit::resolution / 6) * 4])
              : unsigned(channelMean(overflow, 0));
    p.fillRect(i * 128 + 5, 55, 118, 145, QColor::fromHsv(i * 70, 190, 200));
    p.drawText(QRect(i * 128 + 5, 5, 118, 45),
               Qt::AlignCenter | Qt::TextWordWrap, names[i]);
    p.drawText(QRect(i * 128 + 5, 205, 118, 40), Qt::AlignCenter,
               QString("GPU flags: %1").arg(flag));
  }
  p.end();
  compare("R-C05", before, after, "Before: faults lack diagnostic markers",
          "After: controlled GPU fault report",
          "Fault injection view. Colors show detected events, not a "
          "beauty-image improvement.");
  std::vector<float> normal(16, 0), albedo(16, 0);
  for (int i = 0; i < 4; ++i) {
    normal[i * 4 + 3] = i == 0 ? 2.f : 3.f;
    albedo[i * 4 + 3] = float((i < 3 ? expected[i] : 8u) << 8 | 2u);
  }
  auto summary = summarizePathDiagnostics(normal, albedo, 4, 3, 7);
  require(summary["rejectedSamples"].toInt() == 1 &&
              summary["nonFinite"].toInt() == 1 &&
              summary["mediumOverflow"].toInt() == 1 &&
              summary["boundaryLimit"].toInt() == 1 &&
              summary["bvhOverflow"].toInt() == 1,
          "diagnostic aggregation");
  summary["fixture"] = "GPU event detection and synthetic CPU aggregation; "
                       "scene diagnostics saved separately";
  summary["gpuFlags"] =
      QJsonArray{int(expected[0]), int(expected[1]), int(expected[2]),
                 int(channelMean(overflow, 0)), int(rejected[1])};
  report["R-C05"] = summary;
}
void anisotropy(Audit &a, const QString &baseline) {
  a.context.makeCurrent(&a.surface);
  auto x = materialGeometry(), y = x;
  for (int v = 0; v < 3; ++v) {
    x[(17 + v) * 4] = 1;
    x[(17 + v) * 4 + 3] = 1;
    y[(17 + v) * 4 + 1] = 1;
    y[(17 + v) * 4 + 3] = 1;
  }
  x.insert(x.end(), y.begin(), y.end());
  a.setGeometry(x);
  const QString body = R"(void main(){
        int i=gl_FragCoord.x<float(width)*.5?0:1;
        vec2 q=vec2(fract(gl_FragCoord.x/(float(width)*.5))*2.0-1.0,
                    gl_FragCoord.y/float(height)*2.0-1.0);
        Material m=getMaterial(i);m.metallic=1;m.anisotropic=.9;
        m.baseColor=vec3(.85,.6,.25);m.roughness=.25;m.ax=.5;m.ay=.1;
        AUTHOR_FRAME
        float pdf;vec3 L=normalize(vec3(q,1));
        vec3 f=DisneyEval(vec3(0,0,1),vec3(0,0,1),L,m,1.0/1.5,pdf);
        outputColor=vec4(f*L.z*.25,1);})";
  auto source = body;
  source.replace("AUTHOR_FRAME",
                 "m.tangent=BsdfTangent(i,vec3(1,0,0),vec3(0,0,1));");
  const auto after = a.run(source, false);
  auto old = body;
  old.replace("AUTHOR_FRAME", "");
  std::vector<float> before;
  if (baseline.isEmpty())
    before = a.run(old, false);
  else {
    Audit previous(baseline);
    previous.setGeometry(x);
    before = previous.run(old, false);
  }
  a.context.makeCurrent(&a.surface);
  double change = 0, oldMismatch = 0, newMismatch = 0;
  const int n = Audit::resolution;
  for (int row = 0; row < n; ++row)
    for (int col = 0; col < n / 2; ++col) {
      size_t l = (size_t(row) * n + col) * 4, r = l + n / 2 * 4;
      oldMismatch += std::abs(before[l] - before[r]);
      newMismatch += std::abs(after[l] - after[r]);
      change += std::abs(before[l] - after[l]) + std::abs(before[r] - after[r]);
    }
  require(oldMismatch < 1e-6 && newMismatch > 100 && change > 100,
          "authored axes did not rotate anisotropic highlight");
  const auto samples = a.mean(R"(void main(){
        Material m=getMaterial(0);m.metallic=.7;m.anisotropic=.9;m.ax=.5;m.ay=.1;
        m.tangent=vec4(0,1,0,-1);vec3 N=vec3(0,0,1),V=normalize(vec3(.1,.3,1));
        BsdfSample s=SampleDisneyBSDF(V,N,m,1.0/1.5,vec3(rand(),rand(),rand()));
        float p;vec3 f=DisneyEval(V,N,s.direction,m,1.0/1.5,p);
        vec3 expected=p>0?f*abs(s.direction.z)/p:vec3(0);
        outputColor=vec4(length(expected-s.weight),0,0,1);})");
  require(samples[0] < 1e-5, "authored frame sample/eval disagreement");
  compare("R-C02", image(before), image(after), "Before: X/Y tangents ignored",
          "After: X/Y tangents orient BSDF",
          "Same light and material. The two authored axes now rotate the "
          "stretched highlight.");
  report["R-C02"] = QJsonObject{{"legacyAxesDifference", oldMismatch},
                                {"authoredAxesDifference", newMismatch},
                                {"sampleEvalError", samples[0]}};
}
void linearTextures(Audit &a, const QString &baseline) {
  QImage source(2, 2, QImage::Format_RGBA8888);
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
      source.setPixelColor(x, y,
                           QColor(x ? 255 : 0, x ? 255 : 0, x ? 255 : 0, 128));
  auto render = [&](Audit &gpu, bool linear, const QString &body) {
    gpu.context.makeCurrent(&gpu.surface);
    GLuint texture = 0, buffer = 0, info = 0;
    gpu.glGenTextures(1, &texture);
    gpu.glActiveTexture(GL_TEXTURE5);
    gpu.glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
    gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_EDGE);
    gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_EDGE);
    if (linear) {
      gpu.glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA16F, 2, 2, 2, 0, GL_RGBA,
                       GL_FLOAT, nullptr);
      for (int view = 0; view < 2; ++view) {
        const auto data =
            prepareMaterialTexturePixels(source, {2, 2}, view == 1);
        gpu.glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, view, 2, 2, 1,
                            GL_RGBA, GL_FLOAT, data.data());
      }
    } else {
      const auto data = prepareMaterialTextureImage(source, {2, 2});
      gpu.glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 2, 2, 1, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, data.constBits());
    }
    gpu.glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    std::vector<float> metadata = {1,
                                   1,
                                   0,
                                   0,
                                   0,
                                   1,
                                   1,
                                   0,
                                   9987,
                                   9729,
                                   linear ? 1.f : 0.f,
                                   linear ? 1.f : 0.f};
    gpu.buffer(buffer, info, 6, GL_RGBA32F, metadata);
    gpu.textureCount = 1;
    const auto pixels = gpu.run(body, false);
    gpu.textureCount = 0;
    gpu.glDeleteTextures(1, &texture);
    gpu.glDeleteTextures(1, &info);
    gpu.glDeleteBuffers(1, &buffer);
    return pixels;
  };
  const QString afterBody = "void "
                            "main(){outputColor=SampleMaterialColorTexture(0,"
                            "gl_FragCoord.xy/vec2(width,height));}";
  const auto after = render(a, true, afterBody);
  const QString oldBody =
      "void main(){vec4 "
      "v=SampleMaterialTexture(0,gl_FragCoord.xy/"
      "vec2(width,height));outputColor=vec4(SrgbToLinear(v.rgb),v.a);}";
  std::vector<float> before;
  if (baseline.isEmpty())
    before = render(a, false, oldBody);
  else {
    Audit previous(baseline);
    before = render(previous, false, oldBody);
  }
  const auto middle = render(
      a, true,
      "void main(){outputColor=SampleMaterialColorTexture(0,vec2(.5));}");
  checkNear(channelMean(middle, 0), .5, 1e-5, "linear color interpolation");
  checkNear(channelMean(middle, 3), 128.0 / 255, .0003,
            "linear-view alpha stays linear");
  const auto mip = render(
      a, true,
      "void main(){outputColor=textureLod(materialTextures,vec3(.5,.5,1),1);}");
  checkNear(channelMean(mip, 0), .5, 1e-5, "linear color mip average");
  QImage gray(2, 2, QImage::Format_RGBA8888);
  gray.fill(QColor(128, 64, 32, 128));
  const auto data = prepareMaterialTexturePixels(gray, {1, 1}, false),
             color = prepareMaterialTexturePixels(gray, {1, 1}, true);
  checkNear(data[0], 128.0 / 255, 1e-6, "shared image data view");
  checkNear(color[0], std::pow((128.0 / 255 + .055) / 1.055, 2.4), 1e-6,
            "shared image color view");
  const auto resized = prepareMaterialTexturePixels(source, {1, 1}, true);
  checkNear(resized[0], .5, 1e-6, "decode before CPU downscale");
  compare("R-Q01", image(before), image(after),
          "Before: interpolate encoded sRGB", "After: interpolate linear color",
          "Black/white transition, same alpha. Linear midpoint: old 0.214, "
          "corrected 0.500.");
  report["R-Q01"] = QJsonObject{{"linearMidpoint", channelMean(middle, 0)},
                                {"linearMipMean", channelMean(mip, 0)},
                                {"alpha", channelMean(middle, 3)},
                                {"sharedImageData", data[0]},
                                {"sharedImageColor", color[0]}};
}
void textureLod(Audit &a, const QString &baseline) {
  QImage source(512, 512, QImage::Format_RGBA8888);
  for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x)
      source.setPixelColor(x, y, ((x / 4 + y / 4) & 1) ? Qt::white : Qt::black);
  auto render = [&](Audit &gpu, const QString &body, int filter = 9987,
                    int wrap = 0) {
    gpu.context.makeCurrent(&gpu.surface);
    GLuint texture = 0, buffer = 0, info = 0;
    gpu.glGenTextures(1, &texture);
    gpu.glActiveTexture(GL_TEXTURE5);
    gpu.glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
    gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_EDGE);
    gpu.glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_EDGE);
    auto data = prepareMaterialTexturePixels(source, {512, 512}, true);
    gpu.glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA16F, 512, 512, 1, 0,
                     GL_RGBA, GL_FLOAT, data.data());
    gpu.glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    std::vector<float> metadata = {
        1, 1, 0, 0, 0, float(wrap), 0, 0, float(filter), 9729, 0, 1};
    gpu.buffer(buffer, info, 6, GL_RGBA32F, metadata);
    gpu.textureCount = 1;
    auto pixels = gpu.run(body, false);
    gpu.textureCount = 0;
    gpu.glDeleteTextures(1, &texture);
    gpu.glDeleteTextures(1, &info);
    gpu.glDeleteBuffers(1, &buffer);
    return pixels;
  };
  const QString body = "void main(){FOOTPRINT "
                       "outputColor=SampleMaterialColorTexture(0,gl_FragCoord."
                       "xy/vec2(width,height)*11.3);}";
  auto filteredBody = body;
  filteredBody.replace("FOOTPRINT",
                       "materialEvaluationFootprint=vec2(16.0/512.0);");
  auto unfilteredBody = body;
  unfilteredBody.replace("FOOTPRINT", "");
  const auto after = render(a, filteredBody);
  std::vector<float> before;
  if (baseline.isEmpty())
    before = render(a, unfilteredBody);
  else {
    Audit previous(baseline);
    before = render(previous, unfilteredBody);
  }
  a.context.makeCurrent(&a.surface);
  double oldError = 0, newError = 0;
  for (size_t i = 0; i < after.size(); i += 4) {
    oldError += std::pow(before[i] - .5, 2);
    newError += std::pow(after[i] - .5, 2);
  }
  oldError /= after.size() / 4;
  newError /= after.size() / 4;
  require(oldError > .01 && newError < 1e-10,
          "mip filtering did not converge to checker average");
  for (int filter : {9984, 9985, 9986, 9987}) {
    const auto mip =
        render(a,
               "void "
               "main(){materialEvaluationFootprint=vec2(16.0/"
               "512.0);outputColor=SampleMaterialColorTexture(0,vec2(.003));}",
               filter);
    checkNear(channelMean(mip, 0), .5, 1e-6,
              "minFilter " + std::to_string(filter));
  }
  for (int filter : {9728, 9729}) {
    const auto base =
        render(a,
               "void "
               "main(){materialEvaluationFootprint=vec2(16.0/"
               "512.0);outputColor=SampleMaterialColorTexture(0,vec2(.003));}",
               filter);
    checkNear(channelMean(base, 0), 1, 1e-6,
              "non-mipmap minFilter " + std::to_string(filter));
  }
  for (int wrap : {0, 1, 2, 3}) {
    const auto wrapped = render(
        a,
        "void "
        "main(){outputColor=SampleMaterialColorTexture(0,vec2(1.003,.003));}",
        9728, wrap);
    checkNear(channelMean(wrapped, 3), wrap == 3 ? 0 : 1, 1e-6,
              "wrap alpha " + std::to_string(wrap));
  }
  auto triangle = materialGeometry();
  const float positions[] = {-1, -1, 0, 1, -1, 0, -1, 1, 0};
  for (int v = 0; v < 3; ++v)
    for (int c = 0; c < 3; ++c)
      triangle[v * 4 + c] = positions[v * 3 + c];
  triangle[48] = 0;
  triangle[49] = 0;
  triangle[50] = 1;
  triangle[51] = 0;
  triangle[52] = 0;
  triangle[53] = 1;
  a.setGeometry(triangle);
  const auto footprint = a.mean(R"(void main(){
        Ray ray;ray.startPoint=vec3(0,0,3);ray.direction=vec3(0,0,-1);
        rayConeWidth=.01;rayConeSpread=.02;
        SetTriangleFootprint(0,ray,3.0,vec3(0,0,1));
        outputColor=vec4(materialEvaluationFootprint,0,1);})");
  checkNear(footprint[0], .035, 1e-6, "world cone to UV footprint");
  checkNear(footprint[1], .035, 1e-6, "world cone to UV footprint y");
  compare("R-Q02", image(before), image(after), "Before: fixed LOD 0",
          "After: footprint selects mip",
          "GPU texture fixture: identical UVs and checker. Compare against the "
          "linear 0.5 average.");
  report["R-Q02"] =
      QJsonObject{{"oldCheckerMse", oldError},
                  {"filteredCheckerMse", newError},
                  {"uvFootprint", footprint[0]},
                  {"minFiltersTested", 6},
                  {"limits", "Ray cone approximation; alpha/emission point "
                             "sampling; EWA/differentials deferred"}};
}
} // namespace
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  try {
    const auto args = app.arguments();
    outputDirectory = args.value(1, "render-foundation-evidence");
    if (args.contains("--compare-images")) {
      const int at = args.indexOf("--compare-images");
      outputDirectory = args.value(at + 1);
      require(QDir().mkpath(outputDirectory), "create comparison directory");
      QImage before(args.value(at + 3)), after(args.value(at + 4));
      require(!before.isNull() && !after.isNull() &&
                  before.size() == after.size(),
              "comparison inputs");
      compare(args.value(at + 2), before, after,
              "Before: identical camera / spp", "After: identical camera / spp",
              args.value(at + 5));
      return 0;
    }
    require(QDir().mkpath(outputDirectory), "create evidence directory");
    Audit audit;
    const int i = args.indexOf("--baseline");
    materialBaseline(audit, i < 0 ? QString() : args.value(i + 1));
    diagnostics(audit);
    anisotropy(audit, i < 0 ? QString() : args.value(i + 1));
    linearTextures(audit, i < 0 ? QString() : args.value(i + 1));
    const int lod = args.indexOf("--lod-baseline");
    textureLod(audit, lod < 0 ? QString() : args.value(lod + 1));
    report["device"] = QString::fromLatin1(
        reinterpret_cast<const char *>(audit.glGetString(GL_RENDERER)));
    report["surface"] = "QOffscreenSurface; no window shown";
    QFile file(outputDirectory + "/report.json");
    require(file.open(QIODevice::WriteOnly), "write report");
    file.write(QJsonDocument(report).toJson());
    std::cout << "Render foundation tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
  return 0;
}
