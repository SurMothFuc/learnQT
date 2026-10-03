#include "renderer.h"
#include "OidnAuxiliary.h"
#include <QApplication>
#include <QMutex>
#include <QOffscreenSurface>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <cmath>
#include <iostream>

QMutex param_mutex;
struct RendererDenoiseTestAccess
{
    static void guideWidth(Renderer &r,int width) { r.render_width=width; }
    static GLuint color(Renderer &r) { return r.preRenderColorTex; }
    static GLuint normal(Renderer &r) { return r.previousNormalTex; }
    static GLuint albedo(Renderer &r) { return r.previousAlbedoTex; }
    static GLuint moments(GpuDenoiser &d) { return d.moments[d.read]; }
    static GLuint filtered(Renderer &r, DenoiseMode mode) { return mode==DenoiseMode::Realtime ? r.gpuDenoiser.output() : mode==DenoiseMode::OIDN ? r.RenderColorTexfiltered : r.preRenderColorTex; }
};
namespace
{
void require(bool v, const char *message) { if(!v) throw std::runtime_error(message); }
void finish(Renderer &r) { r.submitGpuBoundary(); while(!r.waitForGpuBoundary()) {} r.pollGpuTimers(); }
std::vector<float> read(QOpenGLFunctions_3_3_Core *gl, GLuint texture, int w, int h)
{
    std::vector<float> data(size_t(w)*h*4);
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER,0); gl->glBindTexture(GL_TEXTURE_2D,texture);
    gl->glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,data.data());
    require(gl->glGetError()==GL_NO_ERROR,"Texture readback failed"); return data;
}
void upload(QOpenGLFunctions_3_3_Core *gl, GLuint texture, int w, int h, const std::vector<float> &data)
{
    gl->glBindTexture(GL_TEXTURE_2D,texture); gl->glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_FLOAT,data.data());
}
void saveLinear(const std::vector<float> &data,int w,int h,const QString &path)
{
    QFile f(path+".linear"); require(f.open(QIODevice::WriteOnly),"Cannot write linear reference");
    require(f.write(reinterpret_cast<const char*>(data.data()),qint64(data.size()*sizeof(float)))==qint64(data.size()*sizeof(float)),"Linear write failed");
    QImage image(w,h,QImage::Format_RGB32);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        size_t i=(size_t(y)*w+x)*4;
        auto c=[&](int k) { return qRound(std::min(1.f,std::max(0.f,data[i+k]))*255); };
        image.setPixel(x,h-y-1,qRgb(c(0),c(1),c(2)));
    }
    require(image.save(path),"PNG write failed");
}
double mse(const std::vector<float> &a,const std::vector<float> &b)
{
    double sum=0; for(size_t i=0;i<a.size();i+=4) for(int c=0;c<3;++c) sum+=std::pow(double(a[i+c])-b[i+c],2);
    return sum/(a.size()/4*3);
}
double mean(const std::vector<float> &a)
{
    double sum=0; for(size_t i=0;i<a.size();i+=4) sum+=a[i]; return sum/(a.size()/4);
}
std::shared_ptr<Scene> aaScene(QMatrix4x4 &world, float planeWidth=2.1f)
{
    auto d=SceneDocument::empty(); world.rotate(23,0,0,1); world.rotate(90,1,0,0); world.scale(planeWidth,1,1.6f);
    Material m; m.emissive=QVector3D(1,1,1); auto material=SceneDocument::materialJson(m); material["id"]="white";
    d.root["materials"]=QJsonArray{material};
    d.root["models"]=QJsonArray{QJsonObject{{"id","plane"},{"source",QStringLiteral(RESOURCE_DIR)+"/models/plane.obj"},
        {"normalize",false},{"smoothNormals",false},{"material","white"},{"transform",sceneMatrixJson(world)}}};
    Camera camera; camera.restoreState({0,0,3},{0,0,0},{0,1,0},53.130102);
    d.captureCamera(camera); auto s=d.settings(); s.denoise=false; s.antialiasing=false; s.useEnvironmentMap=false;
    s.maxBounces=0; d.captureSettings(s);
    QString error; auto scene=Scene::prepareDocument(d,error); require(bool(scene),qPrintable(error)); return scene;
}
void antialiasing(QOpenGLFunctions_3_3_Core *gl,const QString &output,QJsonObject &report)
{
    const int w=96,h=80; QMatrix4x4 world; auto scene=aaScene(world); auto settings=scene->document.settings();
    settings.maxRenderFrames=128; settings.tileSize=17; settings.useTileRendering=true;
    Renderer renderer(w,h,settings,nullptr,scene.get());
    auto render=[&](bool aa,bool tiled,bool compute) {
        settings.antialiasing=aa; settings.useTileRendering=tiled; settings.computePathtrace=compute;
        renderer.render(w,h,settings,kInitialSceneDirty,16); finish(renderer);
        while(renderer.samples()<128) { renderer.render(w,h,settings,0,16); finish(renderer); }
        return read(gl,RendererDenoiseTestAccess::color(renderer),w,h);
    };
    const auto center=render(false,true,false), aa=render(true,true,false);
    const auto aaNormal=read(gl,RendererDenoiseTestAccess::normal(renderer),w,h), aaAlbedo=read(gl,RendererDenoiseTestAccess::albedo(renderer),w,h);
    const auto full=render(true,false,false); require(aa==full,"AA depends on tile layout");
    const auto compute=render(true,true,true);
    const bool computeSupported=renderer.stats.computePathtrace;
    const auto actualFormat=QOpenGLContext::currentContext()->format();
    const bool computeContext=actualFormat.majorVersion()>4 ||
        (actualFormat.majorVersion()==4 && actualFormat.minorVersion()>=3);
    require(!computeContext || computeSupported,"GL 4.3 compute path silently fell back to fragment shader");
    if(computeSupported) {
        require(mse(aa,compute)<1e-10,"AA compute/fragment mismatch");
        require(mse(aaNormal,read(gl,RendererDenoiseTestAccess::normal(renderer),w,h))<1e-10 &&
                mse(aaAlbedo,read(gl,RendererDenoiseTestAccess::albedo(renderer),w,h))<1e-10,"Auxiliary compute/fragment mismatch");
    }
    std::vector<float> reference(size_t(w)*h*4,0); const auto inverse=world.inverted();
    const float scale=std::tan(scene->camera.zoom*3.141592653589793/360.);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        float coverage=0;
        for(int j=0;j<32;++j) for(int i=0;i<32;++i) {
            const auto p=inverse*QVector3D((2.f*(x+(i+.5f)/32)/w-1)*float(w)/h*scale*3,
                                          (2.f*(y+(j+.5f)/32)/h-1)*scale*3,0);
            coverage+=std::abs(p.x())<=.5f && std::abs(p.z())<=.5f ? 1.f/1024 : 0;
        }
        for(int c=0;c<3;++c) reference[(size_t(y)*w+x)*4+c]=coverage;
    }
    const double oldError=mse(center,reference), aaError=mse(aa,reference);
    require(aaError<oldError*.15,"AA did not improve silhouette coverage");
    require(std::abs(mean(aa)-mean(reference))/mean(reference)<.02,"AA changed total emission energy");
    saveLinear(center,w,h,output+"/aa-off.png"); saveLinear(aa,w,h,output+"/aa-on.png"); saveLinear(reference,w,h,output+"/aa-reference.png");
    // A mode switch at the sample cap must preserve all raw accumulated data.
    const auto before=read(gl,RendererDenoiseTestAccess::color(renderer),w,h);
    const auto normalBefore=read(gl,RendererDenoiseTestAccess::normal(renderer),w,h);
    settings.denoise=true; settings.denoiseMode=DenoiseMode::Realtime;
    renderer.render(w,h,settings,0,16); finish(renderer);
    require(renderer.samples()==128 && read(gl,RendererDenoiseTestAccess::color(renderer),w,h)==before &&
            read(gl,RendererDenoiseTestAccess::normal(renderer),w,h)==normalBefore,"Mode switch modified raw accumulation");
    require(renderer.stats.denoisedVersion==renderer.stats.accumulationVersion,"Mode switch did not filter capped image");
    renderer.render(w,h,settings,0,16); finish(renderer);
    require(renderer.stats.denoiseRounds==1,"Stopped preview repeatedly filters unchanged image");
    settings.denoiseMode=DenoiseMode::None;
    renderer.render(w,h,settings,0,16); finish(renderer);
    require(!renderer.stats.denoisedVersion && renderer.samples()==128,"Off mode kept stale denoise state");
    // Fault injection at a complete sample cap: exceed the guide texture limit without
    // allocating a large raw framebuffer or interrupting an actual sampling round.
    GLint maximum=0;gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    settings.denoiseMode=DenoiseMode::Realtime;RendererDenoiseTestAccess::guideWidth(renderer,maximum+1);
    renderer.render(w,h,settings,0,16);finish(renderer);
    require(!renderer.stats.denoiseError.isEmpty() && !renderer.stats.denoisedVersion,
            "GPU resource failure was not explicitly reported");
    // This deliberately invalid allocation leaves a GL error. Consume the
    // expected fault before checking a NEW readback operation; otherwise read()
    // reports the injected allocation error as a texture readback failure.
    for(int error=0;error<16 && gl->glGetError()!=GL_NO_ERROR;++error) {}
    require(read(gl,RendererDenoiseTestAccess::color(renderer),w,h)==before,
            "GPU resource failure corrupted raw preview");
    renderer.formal=true;bool failed=false;
    try {renderer.render(w,h,settings,0,16);}catch(const std::exception&){failed=true;}
    require(failed,"Explicit realtime final job silently fell back after GPU failure");
    renderer.formal=false;RendererDenoiseTestAccess::guideWidth(renderer,w);settings.denoiseMode=DenoiseMode::None;
    renderer.render(w,h,settings,0,16);finish(renderer);
    renderer.setRasterActive(true); renderer.render(w,h,settings,0,16); finish(renderer);
    require(renderer.rasterActive() && renderer.stats.rasterSamples>1,"Raster AA did not use MSAA");
    const int rasterSamples=renderer.stats.rasterSamples;
    settings.antialiasing=false;renderer.render(w,h,settings,0,16);finish(renderer);
    require(renderer.stats.rasterSamples==1,"Disabling raster AA kept a multisample target");
    settings.antialiasing=true;renderer.render(w,h,settings,0,16);finish(renderer);
    require(renderer.stats.rasterSamples==rasterSamples,"Re-enabling raster AA failed to recreate MSAA");
    report["aa"]=QJsonObject{{"centerMse",oldError},{"aaMse",aaError},{"energyBias",(mean(aa)-mean(reference))/mean(reference)},
                            {"compute",computeSupported},{"rasterSamples",renderer.stats.rasterSamples},{"resourceFailureFallback",true},{"formalFailureExplicit",true}};
}
void fineCoverage(QOpenGLFunctions_3_3_Core *gl,const QString &output,QJsonObject &report)
{
    const int w=96,h=80;
    for(bool mask : {false,true}) {
        QMatrix4x4 world;auto scene=aaScene(world,mask?2.1f:.035f);
        if(mask) {
            const QString path=output+"/mask-input.png";
            QImage image(256,8,QImage::Format_RGBA8888);image.fill(Qt::black);
            for(int y=0;y<8;++y)for(int x=126;x<130;++x)image.setPixelColor(x,y,Qt::white);
            require(image.save(path),"Mask fixture write failed");
            auto d=scene->document;
            d.root["textures"]=QJsonArray{QJsonObject{{"id","mask"},{"source",QFileInfo(path).absoluteFilePath()},
                {"magFilter",9728},{"minFilter",9728},{"wrapS",1},{"wrapT",1}}};
            auto materials=d.root["materials"].toArray();auto material=materials[0].toObject();
            material["alphaMode"]=int(Mask);material["textures"]=QJsonObject{{"opacity","mask"}};
            materials[0]=material;d.root["materials"]=materials;
            QString error;auto prepared=Scene::prepareDocument(d,error);require(bool(prepared),qPrintable(error));
            scene=std::shared_ptr<Scene>(std::move(prepared));
        }
        auto settings=scene->document.settings();settings.maxRenderFrames=128;settings.useTileRendering=true;settings.tileSize=17;
        Renderer renderer(w,h,settings,nullptr,scene.get());
        auto render=[&](bool aa){settings.antialiasing=aa;renderer.render(w,h,settings,kInitialSceneDirty,16);finish(renderer);
            while(renderer.samples()<128){renderer.render(w,h,settings,0,16);finish(renderer);}
            return read(gl,RendererDenoiseTestAccess::color(renderer),w,h);};
        const auto center=render(false), aa=render(true);
        std::vector<float> reference(size_t(w)*h*4,0);const auto inverse=world.inverted();
        const float scale=std::tan(scene->camera.zoom*3.141592653589793/360.);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            float coverage=0;
            for(int j=0;j<32;++j)for(int i=0;i<32;++i){
                const auto p=inverse*QVector3D((2.f*(x+(i+.5f)/32)/w-1)*float(w)/h*scale*3,
                    (2.f*(y+(j+.5f)/32)/h-1)*scale*3,0);
                const int texel=int(std::floor((p.x()+.5f)*256));
                coverage+=std::abs(p.x())<=.5f && std::abs(p.z())<=.5f && (!mask || (texel>=126 && texel<130))?1.f/1024:0;
            }
            for(int c=0;c<3;++c)reference[(size_t(y)*w+x)*4+c]=coverage;
        }
        require(mse(aa,reference)<mse(center,reference)*.3,"AA did not improve thin/Mask coverage");
        require(std::abs(mean(aa)/mean(reference)-1)<.02,"Thin/Mask AA energy bias exceeded 2 percent");
        const auto normal=read(gl,RendererDenoiseTestAccess::normal(renderer),w,h), albedo=read(gl,RendererDenoiseTestAccess::albedo(renderer),w,h);
        for(size_t i=0;i<aa.size();i+=4){
            require(std::abs(albedo[i]-aa[i])<1e-5,"Albedo and beauty sampled different coverage");
            require(std::abs(normal[i+2]-(.5f+.5f*aa[i]))<1e-5 && normal[i+3]==128,"Normal/beauty validity or coverage differs");
        }
        const QString name=mask?"mask":"thin-line";
        saveLinear(center,w,h,output+"/"+name+"-off.png");saveLinear(aa,w,h,output+"/"+name+"-on.png");
        saveLinear(reference,w,h,output+"/"+name+"-reference.png");
        report[name]=QJsonObject{{"centerMse",mse(center,reference)},{"aaMse",mse(aa,reference)},
            {"energyBias",mean(aa)/mean(reference)-1},{"auxiliaryCoverageMatches",true}};
    }
}
unsigned hash(unsigned x) { x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16); }
void synthetic(QOpenGLFunctions_3_3_Core *gl,const QString &output,QJsonObject &report)
{
    QMatrix4x4 world; auto fixture=aaScene(world); auto settings=fixture->document.settings();
    Renderer renderer(64,64,settings,nullptr,fixture.get());
    Scene scene(false); scene.camera.restoreState({0,0,3},{0,0,0},{0,1,0},53.130102);
    SceneInstance instance; instance.id="surface"; instance.transform.setToIdentity(); instance.inverse.setToIdentity(); scene.instances.push_back(instance);
    GpuDenoiser denoiser; const int w=64,h=64; denoiser.ensure(renderer,{w,h});
    // UI query statistics may lag or skip a frame. Assert the current GPU markers,
    // after the completed fence, rather than treating asynchronous UI metrics as fresh.
    auto acceptance=[&] {
        const auto values=read(gl,RendererDenoiseTestAccess::moments(denoiser),w,h);
        int accepted=0;for(size_t i=3;i<values.size();i+=4)accepted+=values[i]>.5f;
        return double(accepted)/(w*h);
    };
    std::vector<float> average(size_t(w)*h*4,0), reference(size_t(w)*h*4,1);
    std::vector<float> data[5]; for(auto &d:data) d.resize(size_t(w)*h*4,0);
    GLuint vao=0,vbo=0;
    auto frame=[&](int sequence,int kind=1,bool disocclusion=false) {
        denoiser.prepare(scene);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const size_t i=(size_t(y)*w+x)*4;
            const bool fresh=disocclusion && x<w/2;
            const float v=fresh ? 5.f : 1.f+.8f*(float(hash(unsigned(i)+unsigned(sequence)*123457u))/4294967296.f*2-1);
            for(int c=0;c<3;++c) data[0][i+c]=v;
            data[0][i+3]=v*v;
            const float scale=std::tan(scene.camera.zoom*3.141592653589793/360.);
            const auto point=(scene.camera.getViewMatrix().inverted()*QVector4D(
                (2.f*(x+.5f)/w-1)*scale*3,(2.f*(y+.5f)/h-1)*scale*3,-3,1)).toVector3D();
            data[1][i]=point.x(); data[1][i+1]=point.y(); data[1][i+2]=point.z(); data[1][i+3]=(point-scene.camera.position).length();
            data[2][i]=0;data[2][i+1]=0;data[2][i+2]=1;data[2][i+3]=.7;
            data[3][i]=data[3][i+1]=data[3][i+2]=.5;data[3][i+3]=fresh ? 2.f : 1.f;
            data[4][i]=.7;data[4][i+1]=float(kind);data[4][i+2]=fresh ? 1.f : 0.f;data[4][i+3]=1;
            if(sequence<4) for(int c=0;c<4;++c) average[i+c]+=data[0][i+c]*.25f;
        }
        for(int i=0;i<5;++i) upload(gl,denoiser.sample(i),w,h,data[i]);
        denoiser.filter(vao,scene,0,sequence+1,false);
    };
    // A dedicated full-screen VAO is needed for the factory's triangle vertex shader.
    gl->glGenVertexArrays(1,&vao); gl->glGenBuffers(1,&vbo); gl->glBindVertexArray(vao);
    const float vertices[]={-1,-1,0,1,-1,0,-1,1,0,1,1,0,-1,1,0,1,-1,0};
    gl->glBindBuffer(GL_ARRAY_BUFFER,vbo); gl->glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    gl->glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr); gl->glEnableVertexAttribArray(0);
    // Populate current guides, then filter with the complete scene snapshot.
    auto run=[&](int n,int kind=1,bool fresh=false) {
        gl->glBindVertexArray(vao);
        frame(n,kind,fresh);
        finish(renderer); denoiser.poll();
    };
    for(int n=0;n<4;++n) run(n);
    auto filtered=read(gl,denoiser.output(),w,h);
    require(acceptance()>.95,"Static reprojection rejected matching history");
    require(mse(filtered,reference)<mse(average,reference)*.8,"GPU denoiser failed to reduce low-spp error");
    require(std::abs(mean(filtered)-1)<.02,"GPU denoiser changed diffuse mean by more than 2 percent");
    saveLinear(average,w,h,output+"/diffuse-raw.png");saveLinear(filtered,w,h,output+"/diffuse-filtered.png");
    const double staticAcceptance=acceptance(), diffuseError=mse(filtered,reference), diffuseMean=mean(filtered);
    scene.camera.restoreState({.04f,0,3},{.04f,0,0},{0,1,0},53.130102);run(4);
    require(acceptance()>.85,"Camera reprojection lost valid planar history"); const double cameraAcceptance=acceptance();
    scene.instances[0].transform.translate(.03f,0,0);scene.instances[0].inverse=scene.instances[0].transform.inverted();run(5);
    require(acceptance()>.7,"Object transform reprojection lost valid history"); const double objectAcceptance=acceptance();
    scene.instances[0].transform.setToIdentity();scene.instances[0].transform.scale(-1.f,1.05f,.5f);
    scene.instances[0].inverse=scene.instances[0].transform.inverted();run(6);
    require(acceptance()>.7,"Mirrored/nonuniform transform reprojection lost history");
    const double mirroredAcceptance=acceptance();
    SceneInstance revealed=instance;revealed.id="revealed";scene.instances.push_back(revealed);run(7,1,true);
    require(acceptance()<.55,"Disoccluded surface accepted old object history");
    filtered=read(gl,denoiser.output(),w,h);
    for(int y=0;y<h;++y) for(int x=0;x<w/2;++x) require(std::abs(filtered[(size_t(y)*w+x)*4]-5)<1e-4,"Disocclusion ghost or edge bleeding");
    scene.camera.restoreState({.08f,0,3},{.08f,0,0},{0,1,0},53.130102);run(8,3);
    require(acceptance()==0,"Moving refractive path reused history");
    denoiser.invalidate();run(9,4);require(acceptance()==0,"Volume path reused history");
    run(10,1);
    scene.camera.restoreState({.08f,0,3},{.08f,0,6},{0,1,0},53.130102);run(11,1);
    require(acceptance()==0,"Rapid camera turn accepted off-screen old geometry");
    run(12,1);require(acceptance()>.95,"Stationary history did not recover after turn");
    // A last diffuse sample cannot reclassify a full accumulation containing volume paths.
    std::vector<float> volumeAverage(size_t(w)*h*4,2.f), volumeFlags(size_t(w)*h*4,.5f), averageNormals(size_t(w)*h*4,.5f);
    for(size_t i=0;i<volumeFlags.size();i+=4){volumeFlags[i+3]=16;averageNormals[i+2]=1;averageNormals[i+3]=4;volumeAverage[i+3]=4;}
    upload(gl,RendererDenoiseTestAccess::color(renderer),w,h,volumeAverage);
    upload(gl,RendererDenoiseTestAccess::albedo(renderer),w,h,volumeFlags);
    upload(gl,RendererDenoiseTestAccess::normal(renderer),w,h,averageNormals);
    denoiser.prepare(scene);
    denoiser.filter(vao,scene,RendererDenoiseTestAccess::color(renderer),4,false,
        RendererDenoiseTestAccess::normal(renderer),RendererDenoiseTestAccess::albedo(renderer));finish(renderer);denoiser.poll();
    require(mse(read(gl,denoiser.output(),w,h),volumeAverage)<1e-12 && acceptance()==0,"Volume preview lost raw accumulation or reused history");
    denoiser.filter(vao,scene,RendererDenoiseTestAccess::color(renderer),4,true,
        RendererDenoiseTestAccess::normal(renderer),RendererDenoiseTestAccess::albedo(renderer));finish(renderer);
    require(mse(read(gl,denoiser.output(),w,h),volumeAverage)<1e-12,"Final filter reclassified accumulated volume as diffuse");
    for(size_t i=3;i<volumeFlags.size();i+=4)volumeFlags[i]=2;
    upload(gl,RendererDenoiseTestAccess::albedo(renderer),w,h,volumeFlags);
    denoiser.filter(vao,scene,RendererDenoiseTestAccess::color(renderer),4,false,
        RendererDenoiseTestAccess::normal(renderer),RendererDenoiseTestAccess::albedo(renderer));finish(renderer);denoiser.poll();
    require(acceptance()==0,"New diffuse region reused old volume accumulation");
    auto invalidMaterial=read(gl,denoiser.sample(4),w,h);
    for(size_t i=3;i<invalidMaterial.size();i+=4)invalidMaterial[i]=0;
    upload(gl,denoiser.sample(4),w,h,invalidMaterial);
    upload(gl,denoiser.sample(0),w,h,std::vector<float>(size_t(w)*h*4,0));
    denoiser.filter(vao,scene,RendererDenoiseTestAccess::color(renderer),4,false,
        RendererDenoiseTestAccess::normal(renderer),RendererDenoiseTestAccess::albedo(renderer));finish(renderer);denoiser.poll();
    require(mse(read(gl,denoiser.output(),w,h),volumeAverage)<1e-12 && acceptance()==0,
        "Invalid realtime sample replaced the valid raw accumulation");
    // Textured guides can reject history even with a stationary camera. High spp
    // must keep the complete raw mean, rather than restarting from noisy samples.
    std::vector<float> settledRaw(size_t(w)*h*4,1.f), noisySample(size_t(w)*h*4);
    upload(gl,RendererDenoiseTestAccess::color(renderer),w,h,settledRaw);
    for(int kind : {1,2,3}) for(int frameIndex=0;frameIndex<4;++frameIndex) {
        for(size_t i=0;i<settledRaw.size();i+=4) {
            const float value=(frameIndex&1)?1.8f:.2f;
            for(int c=0;c<3;++c) noisySample[i+c]=value;
            noisySample[i+3]=value*value;
            invalidMaterial[i+1]=float(kind);invalidMaterial[i+3]=1;
            averageNormals[i+3]=512;volumeFlags[i+3]=float(1u<<kind);
            data[2][i]=(frameIndex&1)?.8f:-.8f;data[2][i+1]=0;data[2][i+2]=.6f;
            for(int c=0;c<3;++c)data[3][i+c]=(frameIndex&1)?.7f:.3f;
        }
        upload(gl,denoiser.sample(0),w,h,noisySample);upload(gl,denoiser.sample(2),w,h,data[2]);
        upload(gl,denoiser.sample(3),w,h,data[3]);upload(gl,denoiser.sample(4),w,h,invalidMaterial);
        upload(gl,RendererDenoiseTestAccess::normal(renderer),w,h,averageNormals);
        upload(gl,RendererDenoiseTestAccess::albedo(renderer),w,h,volumeFlags);
        denoiser.prepare(scene);
        denoiser.filter(vao,scene,RendererDenoiseTestAccess::color(renderer),512,false,
            RendererDenoiseTestAccess::normal(renderer),RendererDenoiseTestAccess::albedo(renderer));finish(renderer);denoiser.poll();
        require(mse(read(gl,denoiser.output(),w,h),settledRaw)<1e-12,
            "Stationary diffuse/reflective/transmissive preview retained a finite-history noise floor");
    }
    report["diffuse"]=QJsonObject{{"rawMse",mse(average,reference)},{"filteredMse",diffuseError},
        {"mean",diffuseMean},{"stationaryAccumulationConverges",true},{"mixedVolumeAccumulationProtected",true},{"invalidSampleAccumulationProtected",true},{"staticAcceptance",staticAcceptance},{"cameraAcceptance",cameraAcceptance},{"objectAcceptance",objectAcceptance},{"mirroredNonuniformAcceptance",mirroredAcceptance},
        {"gpuMs",denoiser.milliseconds},{"bytes",double(denoiser.allocatedBytes())}};
    gl->glDeleteBuffers(1,&vbo);gl->glDeleteVertexArrays(1,&vao);
}
void compatibility(QJsonObject &report)
{
    auto fresh=SceneDocument::empty(); require(fresh.settings().antialiasing && fresh.settings().effectiveDenoiseMode()==DenoiseMode::Realtime,"New preview defaults wrong");
    auto output=RenderJobSettings::fromJson(fresh.root["output"].toObject());require(output.antialiasing && output.effectiveDenoiseMode()==DenoiseMode::OIDN,"New output defaults wrong");
    require(RenderJobSettings::fromJson(SceneDocument::model("fixture.obj").root["output"].toObject()).antialiasing,"New model output AA default wrong");
    auto legacy=fresh; legacy.root["render"]=QJsonObject{{"denoise",true}};
    require(!legacy.settings().antialiasing && legacy.settings().effectiveDenoiseMode()==DenoiseMode::OIDN,"Legacy mode changed");
    legacy.root["render"]=QJsonObject{{"denoise",false},{"denoiseMode","realtime"}};
    require(legacy.settings().effectiveDenoiseMode()==DenoiseMode::Realtime,"New mode did not take precedence");
    QString error;legacy.root["render"]=QJsonObject{{"denoiseMode","bad"}};require(!legacy.validate(error,false),"Unknown mode accepted");
    float n[]={.5,.5,.5,.75,.75,.5};double lo=0,hi=0;decodeOidnNormals(n,n,6,lo,hi);
    require(n[0]==0 && n[1]==0 && n[2]==0 && std::abs(n[3]*n[3]+n[4]*n[4]-1)<1e-6,"OIDN normal decode invalid");
    report["compatibility"]=true;
}
void benchmark(const QStringList &args,QOpenGLFunctions_3_3_Core *gl)
{
    require(args.size()>=8,"Usage: --benchmark scene output.json width height spp mode [--aa] [--compute]");
    QString error;auto scene=Scene::prepareScene(args[2],false,error);require(bool(scene),qPrintable(error));
    auto settings=scene->document.settings();settings.antialiasing=args.contains("--aa");
    const auto mode=args[7];require(mode=="none" || mode=="oidn" || mode=="realtime","Invalid benchmark mode");
    if(args.contains("--profile")) {
        require(mode=="none","Profile pass requires denoising disabled");
        qputenv("LEARNQT_TRACE_PROFILE","1");
    }
    settings.denoiseMode=readDenoiseMode(QJsonObject{{"denoiseMode",mode}});settings.denoise=mode!="none";
    settings.computePathtrace=args.contains("--compute");settings.renderLow=false;settings.maxBounces=4;
    const int seedOption=args.indexOf("--seed");
    if(seedOption>=0) {bool ok=false;const auto value=args.value(seedOption+1).toULongLong(&ok);
        require(ok && value<=4294967295ull,"Invalid sampler seed");settings.sampleSeed=unsigned(value);}
    const int rrOption=args.indexOf("--rr-depth");
    if(rrOption>=0)settings.rrMinDepth=args.value(rrOption+1).toInt();
    require(settings.rrMinDepth>=0 && settings.rrMinDepth<=64,"Invalid RR depth");
    const int bouncesOption=args.indexOf("--bounces");
    if(bouncesOption>=0) settings.maxBounces=args.value(bouncesOption+1).toInt();
    require(settings.maxBounces>0 && settings.maxBounces<=int(MAX_BOUNCES_LIMIT),"Invalid bounce budget");
    settings.tileSize=128;settings.useTileRendering=true;const int w=args[4].toInt(),h=args[5].toInt(),spp=args[6].toInt();
    const int tileOption=args.indexOf("--tile");
    if(tileOption>=0){settings.tileSize=args.value(tileOption+1).toInt();require(settings.tileSize>0,"Invalid tile size");}
    if(args.contains("--full"))settings.useTileRendering=false;
    const int warmup=args.contains("--no-warmup") ? 0 : 2;
    const int secondsOption=args.indexOf("--seconds");
    const double budget=secondsOption<0?0:args.value(secondsOption+1).toDouble();
    require(secondsOption<0 || budget>0,"Invalid time budget");
    settings.maxRenderFrames=budget>0?1000000:spp+warmup;require(w>0 && h>0 && spp>0,"Invalid dimensions/spp");
    Renderer renderer(w,h,settings,nullptr,scene.get());renderer.prepareJob({w,h},settings,kInitialSceneDirty);
    require(!args.contains("--require-compute") || renderer.stats.computePathtrace,"Requested compute backend fell back to fragment shader");
    const bool preview=args.contains("--preview");renderer.formal=!preview;
    const int captureOption=args.indexOf("--capture-tail");
    const int captureTail=captureOption<0?0:args.value(captureOption+1).toInt();
    const bool checkConvergence=args.contains("--check-convergence");
    require(!checkConvergence || (preview && mode=="realtime" && captureTail>=2 && budget<=0),"Convergence check requires a fixed realtime preview and at least two tail frames");
    int capturedSpp=0;
    std::vector<float> previousRaw,previousFiltered;
    double rawDelta=0,filteredDelta=0;int framePairs=0;
    auto step=[&] {
        renderer.render(w,h,settings,0,16);finish(renderer);
        if(captureTail>0 && renderer.completeRound() && renderer.samples()!=capturedSpp &&
           renderer.samples()>settings.maxRenderFrames-captureTail) {
            capturedSpp=renderer.samples();const QString prefix=args[3]+".frame-"+QString::number(capturedSpp);
            auto raw=read(gl,RendererDenoiseTestAccess::color(renderer),w,h);
            auto filtered=read(gl,RendererDenoiseTestAccess::filtered(renderer,settings.effectiveDenoiseMode()),w,h);
            if(!previousRaw.empty()) {rawDelta+=mse(raw,previousRaw);filteredDelta+=mse(filtered,previousFiltered);++framePairs;}
            saveLinear(raw,w,h,prefix+".raw.png");saveLinear(filtered,w,h,prefix+".filtered.png");
            previousRaw=std::move(raw);previousFiltered=std::move(filtered);
        }
    };
    while(renderer.samples()<warmup) step();QElapsedTimer timer;timer.start();
    while(renderer.samples()<settings.maxRenderFrames &&
          (budget<=0 || timer.nsecsElapsed()/1e9<budget || renderer.roundInProgress())) step();
    const int measuredSpp=renderer.samples()-warmup;
    if(!preview) renderer.finishDenoise(settings);
    finish(renderer);const double seconds=timer.nsecsElapsed()/1e9;
    if(checkConvergence) require(framePairs>=captureTail-1 && filteredDelta<=rawDelta*16,
        "Stationary textured preview flicker did not converge with raw sampling");
    const auto image=renderer.result(settings);require(image.save(args[3]+".png"),"Benchmark save failed");
    const auto raw=read(gl,RendererDenoiseTestAccess::color(renderer),w,h);saveLinear(raw,w,h,args[3]+".raw.png");
    if(args.contains("--diagnostics")) {
        QFile diagnostics(args[3]+".diagnostics.json");
        require(diagnostics.open(QIODevice::WriteOnly),"Cannot write path diagnostics");
        diagnostics.write(QJsonDocument(renderer.pathDiagnostics()).toJson());
        QFile textures(args[3]+".textures.json");
        require(textures.open(QIODevice::WriteOnly),"Cannot write texture resource diagnostics");
        textures.write(QJsonDocument(renderer.textureResources()).toJson());
    }
    if(args.contains("--profile")) {
        QFile profile(args[3]+".profile.json");require(profile.open(QIODevice::WriteOnly),"Cannot write trace profile");
        profile.write(QJsonDocument(renderer.traceProfile()).toJson());
    }
    saveLinear(read(gl,RendererDenoiseTestAccess::filtered(renderer,settings.effectiveDenoiseMode()),w,h),w,h,args[3]+".filtered.png");
    QJsonObject result{{"width",w},{"height",h},{"spp",measuredSpp},{"totalSpp",renderer.samples()},{"seconds",seconds},{"fps",measuredSpp/seconds},
        {"mode",mode},{"aa",settings.antialiasing},{"preview",preview},{"historyAcceptance",renderer.stats.historyAcceptance},
        {"gpuDenoiseMs",renderer.stats.realtimeDenoiseMs},{"oidnMs",renderer.stats.oidnMs},
        {"rawTailRms",framePairs?std::sqrt(rawDelta/framePairs):0},{"filteredTailRms",framePairs?std::sqrt(filteredDelta/framePairs):0},
        {"bytes",double(renderer.allocatedBytes())},{"compute",renderer.stats.computePathtrace},
        {"backend",renderer.stats.pathtraceBackend},
        {"shadowAnyHitEnabled",!qEnvironmentVariableIsSet("LEARNQT_DISABLE_ANYHIT")},
        {"binaryShadowScene",std::none_of(scene->materials.begin(),scene->materials.end(),[](const Material &m) {
            return m.alphaMode==Transparent||m.mediumtype!=None;
        })},
        {"gpuTraceMs",renderer.stats.gpuMs},
        {"bounces",settings.maxBounces},
        {"sampleSeed",double(settings.sampleSeed)},
        {"rrMinDepth",settings.rrMinDepth},
        {"environmentSelectProbability",settings.useEnvironmentMap?scene->environmentSelectionProbability():0},
        {"tileSize",settings.tileSize},{"tiled",settings.useTileRendering},
        {"device",QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER)))}};
    QFile f(args[3]);require(f.open(QIODevice::WriteOnly),"Benchmark JSON write failed");f.write(QJsonDocument(result).toJson());
    std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<std::endl;
}
}
int main(int argc,char **argv)
{
    qInstallMessageHandler([](QtMsgType type,const QMessageLogContext &,const QString &message) {
        if(type==QtWarningMsg || type==QtCriticalMsg || type==QtFatalMsg)
            std::cerr<<message.toStdString()<<std::endl;
    });
    bool requestCompute=false;for(int i=1;i<argc;++i) requestCompute=requestCompute || std::string(argv[i])=="--compute";
    QSurfaceFormat format;format.setVersion(requestCompute?4:3,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setSwapInterval(0);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);
    try {
        QOpenGLContext context;context.setFormat(format);require(context.create(),"Context creation failed");
        QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(context.makeCurrent(&surface),"Offscreen context unavailable");
        auto gl=context.versionFunctions<QOpenGLFunctions_3_3_Core>();require(gl && gl->initializeOpenGLFunctions(),"GL functions unavailable");
        const auto args=app.arguments();
        if(args.contains("--benchmark")) { benchmark(args,gl);return 0; }
        const QString output=args.value(1,"aa-denoise-regression");require(QDir().mkpath(output),"Cannot create evidence directory");
        QJsonObject report;compatibility(report);antialiasing(gl,output,report);fineCoverage(gl,output,report);synthetic(gl,output,report);
        QFile f(output+"/report.json");require(f.open(QIODevice::WriteOnly),"Report write failed");f.write(QJsonDocument(report).toJson());
        std::cout<<QJsonDocument(report).toJson(QJsonDocument::Compact).constData()<<std::endl;return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<std::endl;return 1; }
}
