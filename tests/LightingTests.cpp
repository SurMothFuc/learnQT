#include "common.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QFile>
#include <QVector4D>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

static void check(bool value, const std::string& text) {
    if (!value) throw std::runtime_error(text);
}
static void checkNear(double actual, double expected, double tolerance, const std::string& name) {
    std::cout << name << ": " << actual << " (expected " << expected << ")\n";
    check(std::isfinite(actual) && std::abs(actual-expected) <= tolerance, name);
}
static QString read(const QString& path) {
    QFile file(path); check(file.open(QIODevice::ReadOnly), path.toStdString());
    return QString::fromUtf8(file.readAll());
}

class Audit : public QOpenGLFunctions_3_3_Core {
public:
    QOpenGLContext context;
    QOffscreenSurface surface;
    std::unique_ptr<QOpenGLFramebufferObject> target;
    QString modules;
    GLuint vao = 0, hdr = 0, cache = 0;
    GLuint triangleBuffer=0, triangleTexture=0, nodeBuffer=0, nodeTexture=0, lightBuffer=0, lightTexture=0;
    int lightCount=0, analyticCount=0, triangleCount=0;
    static constexpr int resolution = 256;
    Audit() {
        QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format); check(context.create(), "create GL context");
        surface.setFormat(context.format()); surface.create();
        check(context.makeCurrent(&surface), "make current"); initializeOpenGLFunctions();
        std::cout << "OpenGL: " << glGetString(GL_RENDERER) << "\n";
        QOpenGLFramebufferObjectFormat f; f.setInternalTextureFormat(GL_RGBA32F);
        target.reset(new QOpenGLFramebufferObject(resolution,resolution,f));
        check(target->isValid(), "float FBO");
        glGenVertexArrays(1,&vao); glBindVertexArray(vao);
        glGenTextures(1,&hdr); glGenTextures(1,&cache);
        for (const char* name : {"defines","structs","uniforms","utils","bvh_material","hdr_utils","bsdf","light_sampling","medium","pathtrace"})
            modules += read(QString::fromStdString(getShaderPath(std::string("include/")+name+".glsl"))) + "\n";
        setGeometry({});
        setLights({},0);
    }
    ~Audit() {
        target.reset();
        for (GLuint value : {hdr,cache,triangleTexture,nodeTexture,lightTexture}) glDeleteTextures(1,&value);
        for (GLuint value : {triangleBuffer,nodeBuffer,lightBuffer}) glDeleteBuffers(1,&value);
        glDeleteVertexArrays(1,&vao);
        context.doneCurrent();
    }
    void image(GLuint id, int unit, int w, int h, const float* data) {
        glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_2D,id);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB32F,w,h,0,GL_RGB,GL_FLOAT,data);
    }
    void environment(int w, int h, const std::vector<float>& rgb) {
        std::unique_ptr<float[]> c(calculateHdrCache(const_cast<float*>(rgb.data()),w,h));
        double sum=0;
        for(int i=0;i<w*h;++i) { check(std::isfinite(c[3*i+2]) && c[3*i+2]>=0,"finite HDR mass"); sum+=c[3*i+2]; }
        checkNear(sum,1,1e-6,"CDF probability sum");
        image(hdr,0,w,h,rgb.data()); image(cache,1,w,h,c.get());
    }
    void buffer(GLuint& buffer, GLuint& texture, int unit, GLenum format, const std::vector<float>& data) {
        if (!buffer) glGenBuffers(1,&buffer);
        if (!texture) glGenTextures(1,&texture);
        glBindBuffer(GL_TEXTURE_BUFFER,buffer);
        const std::vector<float> dummy(16,0);
        const auto& actual=data.empty()?dummy:data;
        glBufferData(GL_TEXTURE_BUFFER,actual.size()*sizeof(float),actual.data(),GL_STATIC_DRAW);
        glActiveTexture(GL_TEXTURE0+unit); glBindTexture(GL_TEXTURE_BUFFER,texture); glTexBuffer(GL_TEXTURE_BUFFER,format,buffer);
    }
    void setLights(const std::vector<float>& data, int analytic) {
        lightCount=int(data.size()/16); analyticCount=analytic;
        buffer(lightBuffer,lightTexture,4,GL_RGBA32F,data);
    }
    void setGeometry(const std::vector<float>& data) {
        triangleCount=int(data.size()/80);
        buffer(triangleBuffer,triangleTexture,2,GL_RGBA32F,data);
        // One leaf, reserved node zero. It is sufficient for all small analytic fixtures.
        std::vector<float> nodes(24,0); nodes[15]=float(triangleCount);
        buffer(nodeBuffer,nodeTexture,3,GL_RGB32F,nodes);
    }
    std::vector<float> run(const QString& body, bool environmentEnabled=true) {
        QOpenGLShaderProgram program;
        const char* vertex = "#version 330 core\nout vec3 pix; void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0;pix=vec3(p,0);gl_Position=vec4(p,0,1);}";
        check(program.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex),program.log().toStdString());
        QString fragment="#version 330 core\nin vec3 pix;\nlayout(location=0) out vec4 outputColor;\n";
        if(environmentEnabled) fragment+="#define USEENVIRONMENTMAP\n";
        fragment+=modules+"\n"+body;
        check(program.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment),program.log().toStdString());
        check(program.link(),program.log().toStdString()); program.bind();
        program.setUniformValue("width",resolution); program.setUniformValue("height",resolution);
        glUniform1ui(program.uniformLocation("frameCounter"),7); program.setUniformValue("hdrResolution",64);
        program.setUniformValue("hdrMap",0); program.setUniformValue("hdrCache",1);
        program.setUniformValue("triangles",2); program.setUniformValue("nodes",3); program.setUniformValue("lights",4);
        program.setUniformValue("nTriangles",triangleCount); program.setUniformValue("nNodes",triangleCount>0?2:0);
        program.setUniformValue("nLights",lightCount); program.setUniformValue("nAnalyticLights",analyticCount);
        program.setUniformValue("materialTextureCount",0);
        // Distinct sampler types need distinct units even when the test has no material images.
        program.setUniformValue("materialTextures",5); program.setUniformValue("materialTextureInfo",6);
        std::vector<float> sobol= getSobelRandomNumber(7,60);
        program.setUniformValueArray("sobelNumber",sobol.data(),int(sobol.size()),1);
        check(glGetError()==GL_NO_ERROR,"GL setup/uniform error");
        target->bind(); glViewport(0,0,resolution,resolution); glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES,0,3);
        std::vector<float> pixels(resolution*resolution*4);
        glReadPixels(0,0,resolution,resolution,GL_RGBA,GL_FLOAT,pixels.data());
        GLenum error=glGetError(); check(error==GL_NO_ERROR,"OpenGL draw/read error "+std::to_string(error));
        for(float v:pixels) check(std::isfinite(v),"NaN/Inf pixel");
        program.release(); target->release(); return pixels;
    }
    std::array<double,4> mean(const QString& body, bool env=true) {
        auto pixels=run(body,env); std::array<double,4> sum{};
        for(size_t i=0;i<pixels.size();++i) sum[i%4]+=pixels[i]/double(resolution*resolution);
        return sum;
    }
};

static void hdrTests(Audit& a) {
    const double pi=std::acos(-1.0);
    for (auto size : {std::array<int,2>{64,32}, {7,5}, {1,1}}) {
        a.environment(size[0],size[1],std::vector<float>(size[0]*size[1]*3,1));
        auto result=a.mean(R"(
void main(){
    float p; vec3 L=SampleHdr(rand(),rand(),p);
    float q=hdrPdf(L,hdrResolution);
    outputColor=vec4(4.0*PI*p, abs(p-q)/max(p,1e-30), L.y, max(L.y,0.0)/p);
})");
        checkNear(result[0],1,2e-5,"white HDR uniform solid-angle PDF");
        checkNear(result[1],0,1e-5,"sample/query PDF agreement");
        checkNear(result[2],0,.01,"white HDR mean cosine");
        checkNear(result[3],pi,.03,"Lambert hemisphere integral");
    }
    a.environment(13,7,std::vector<float>(13*7*3,0));
    auto black=a.mean("void main(){float p;vec3 L=SampleHdr(rand(),rand(),p);outputColor=vec4(p*4.0*PI,hdrColor(L));}");
    checkNear(black[0],1,1e-5,"black HDR uniform fallback"); checkNear(black[1],0,0,"black HDR radiance");
    std::vector<float> spike(16*8*3,0);
    for(int c=0;c<3;++c) spike[3*(3*16+5)+c]=100;
    a.environment(16,8,spike);
    auto hot=a.mean(R"(
void main(){float p;vec3 L=SampleHdr(rand(),rand(),p);ivec2 cell=ivec2(toSphericalCoord(L)*vec2(16,8));
outputColor=vec4(float(cell==ivec2(5,3)),abs(p-hdrPdf(L,hdrResolution))/p,hdrColor(L).r/p,1);})");
    checkNear(hot[0],1,0,"single hot texel support");
    checkNear(hot[1],0,1e-6,"hot texel sample/query PDF agreement");
    checkNear(hot[2],100*(2*pi/16)*(std::cos(3*pi/8)-std::cos(4*pi/8)),1e-3,"hot texel radiance integral");
    for(int row:{0,2047}) {
        std::vector<float> polar(64*2048*3,0);
        for(int c=0;c<3;++c) polar[3*(row*64+23)+c]=100;
        a.environment(64,2048,polar);
        auto pole=a.mean("void main(){float p;vec3 L=SampleHdr(rand(),rand(),p);outputColor=vec4(hdrPdf(L,hdrResolution)/p,hdrColor(L).r,0,1);}");
        checkNear(pole[0],1,2e-4,"polar texel sample/query agreement");
        checkNear(pole[1],100,.02,"polar texel retains latitude and longitude");
    }

}

static std::vector<float> triangle(float z, bool up=true) {
    std::vector<float> t(80,0);
    const float points[9]={-20,-20,z,20,-20,z,0,20,z};
    for(int v=0;v<3;++v) for(int c=0;c<3;++c) t[v*4+c]=points[v*3+c];
    if(!up) for(int c=0;c<3;++c) std::swap(t[4+c],t[8+c]);
    for(int v=0;v<3;++v) t[(v+3)*4+2]=up?1.f:-1.f;
    t[7*4]=t[7*4+1]=t[7*4+2]=1; // diffuse white
    t[9*4]=1; t[9*4+1]=1; // coat roughness, IOR
    t[11*4+1]=1;
    t[13*4+2]=t[13*4+3]=-1;
    for(int c=0;c<4;++c)t[14*4+c]=-1;
    t[15*4]=1; t[15*4+1]=.5f; t[15*4+2]=1;
    return t;
}
static std::vector<float> light(int type,float z,float radius,float radiance,float select=1) {
    std::vector<float> l(16,0);
    l[0]=float(type);l[1]=-1;l[2]=select;l[3]=radius;
    l[6]=z;l[8]=l[9]=l[10]=radiance;l[12]=1;
    return l;
}
static const QString surfacePath = R"(
void main(){Ray r;r.startPoint=vec3(0,0,1);r.direction=vec3(0,0,-1);
OutputColor c=pathTracingImportanceSampling(r,1);outputColor=vec4(c.render_color,1);})";

static void analyticTests(Audit& a) {
    const double pi=std::acos(-1.0);
    a.environment(8,4,std::vector<float>(8*4*3,0));
    a.setGeometry({});
    a.setLights(light(2,2,.5f,3),1);
    auto samples=a.run(R"(
void main(){
    MediumStack m;m.size=0;
    LightSample s=SampleOneLight(vec3(0),rand(),rand(),rand());
    float pb=max(0.0,s.direction.z)*INV_PI;
    float nl=s.radiance.r*pb/s.pdf;
    vec3 b=CosineSampleHemisphere(rand(),rand());
    EncodedLight light=GetEncodedLight(0);
    float p=SphereLightPdf(light,vec3(0),b);
    float bl=p>0.0?light.color.r:0.0;
    float nee=nl*ShadowTransmittance(vec3(0),vec3(0,0,1),s.direction,s.distance,m,s.lightIndex,s.triangleIndex).r;
    float mis=nee*misMixWeight(s.pdf,pb)+bl*misMixWeight(b.z*INV_PI,p);
    outputColor=vec4(nee,bl,mis,abs(s.pdf-SphereLightPdf(light,vec3(0),s.direction))/s.pdf);
})",false);
    std::array<double,4> mean{},square{},peak{};
    for(size_t i=0;i<samples.size();++i) {
        int c=int(i%4);mean[c]+=samples[i]/double(Audit::resolution*Audit::resolution);
        square[c]+=samples[i]*samples[i]/double(Audit::resolution*Audit::resolution);
        peak[c]=std::max(peak[c],double(samples[i]));
    }
    for(int c=0;c<3;++c) {
        checkNear(mean[c],3*.25/4,c==1?.009:.003,"sphere estimator "+std::to_string(c));
        std::cout<<"  same 65536 samples: variance="<<square[c]-mean[c]*mean[c]<<" max="<<peak[c]<<"\n";
    }
    checkNear(mean[3],0,1e-5,"sphere sample/PDF agreement");
    check(square[2]-mean[2]*mean[2] < square[1]-mean[1]*mean[1],"MIS did not reduce BSDF-only variance");
    a.setGeometry(triangle(0));
    checkNear(a.mean(surfacePath,false)[0],3*.25/4,.005,"full integrator sphere MIS");
    a.setGeometry({});
    auto primary=a.mean("void main(){Ray r;r.startPoint=vec3(0);r.direction=vec3(0,0,1);outputColor=vec4(pathTracingImportanceSampling(r,0).render_color,1);}",false);
    checkNear(primary[0],3,1e-6,"camera-visible sphere");
    auto inside=a.mean("void main(){LightSample s=SampleOneLight(vec3(0,0,2),rand(),rand(),rand());outputColor=vec4(float(s.valid));}",false);
    checkNear(inside[0],0,0,"opaque outward-emitting sphere interior");
    auto shadow=a.mean("void main(){MediumStack m;m.size=0;outputColor=vec4(ShadowTransmittance(vec3(0),vec3(0),vec3(0,0,1),10.0,m),1);}",false);
    checkNear(shadow[0],0,0,"analytic sphere blocks shadow rays");

    a.setLights(light(3,-1,.6f,2),1);
    a.setGeometry(triangle(0));
    checkNear(a.mean(surfacePath,false)[0],2*std::pow(std::sin(.6),2),.007,"full integrator sun MIS");
    a.setGeometry({});
    checkNear(a.mean("void main(){outputColor=vec4(InfiniteEmission(vec3(0,0,1),vec3(0),true,0.0),1);}",false)[0],2,1e-6,"camera-visible sun with HDR off");
    auto tiny=light(3,-1,1e-4f,2);
    a.setLights(tiny,1);
    checkNear(a.mean("void main(){LightSample s=SampleOneLight(vec3(0),rand(),rand(),rand());outputColor=vec4(float(s.valid),SunLightPdf(GetEncodedLight(0),s.direction)/s.pdf,0,1);}",false)[1],1,1e-4,"small sun solid-angle precision");
    a.setLights({},0);
    a.setGeometry(triangle(0));
    a.environment(7,5,std::vector<float>(7*5*3,1));
    checkNear(a.mean(surfacePath)[0],29.0/28.0,.007,"full integrator white HDR MIS (analytic Disney diffuse integral)");
    auto sun1=light(3,-1,.6f,2,.5f),sun2=light(3,-1,.6f,2,.5f);sun1[12]=.5f;
    sun1.insert(sun1.end(),sun2.begin(),sun2.end());a.setLights(sun1,2);
    checkNear(a.mean(surfacePath)[0],29.0/28.0+4*std::pow(std::sin(.6),2),.015,"overlapping suns plus HDR MIS");
    auto zeroSelection=light(3,-1,.6f,2,0);a.setLights(zeroSelection,1);a.setGeometry({});
    checkNear(a.mean("void main(){outputColor=vec4(InfiniteEmission(vec3(0,0,1),vec3(0),false,0.2),1);}",false)[0],2,1e-6,
              "zero selection mass retains BSDF sun emission");

}

static void alphaDeltaTests(Audit& a) {
    a.environment(8,4,std::vector<float>(8*4*3,0));
    auto emitter=triangle(2); emitter[24]=emitter[25]=emitter[26]=4;
    emitter[39]=2; emitter[60]=0; emitter[66]=1;
    auto l=light(1,0,0,4);l[1]=0;
    a.setLights(l,0);a.setGeometry(emitter);
    const QString sampledEmission="void main(){LightSample s=SampleOneLight(vec3(0),rand(),rand(),rand());outputColor=vec4(s.radiance,1);}";
    const QString hitEmission="void main(){Ray r;r.startPoint=vec3(0);r.direction=vec3(0,0,1);outputColor=vec4(pathTracingImportanceSampling(r,0).render_color,1);}";
    checkNear(a.mean(sampledEmission,false)[0],0,0,"masked emitter NEE");
    checkNear(a.mean(hitEmission,false)[0],0,0,"masked emitter path hit");
    emitter[39]=3;emitter[60]=.25f;a.setGeometry(emitter);
    checkNear(a.mean(sampledEmission,false)[0],1,1e-6,"blend emitter NEE coverage");
    checkNear(a.mean(hitEmission,false)[0],1,.025,"blend emitter path coverage");
    emitter[39]=0;emitter[60]=1;a.setGeometry(emitter);
    auto twoSided=a.mean("void main(){LightSample s=SampleOneLight(vec3(0,0,4),rand(),rand(),rand());outputColor=vec4(s.radiance.r,float(s.valid),abs(s.pdf-LightPdf(vec3(0,0,4),s.direction,0,s.distance))/s.pdf,1);}",false);
    checkNear(twoSided[0],4,0,"triangle back-side emission");checkNear(twoSided[2],0,1e-6,"triangle sample/PDF agreement");

    auto mirror=triangle(0);mirror[43]=1;mirror[45]=0;
    mirror[28]=mirror[29]=mirror[30]=.8f;
    a.setGeometry(mirror);a.setLights(light(2,2,.5f,3),1);
    checkNear(a.mean(surfacePath,false)[0],2.4,1e-5,"delta mirror sees sphere with weight one");
    a.setLights(light(3,-1,.2f,3),1);
    checkNear(a.mean(surfacePath,false)[0],2.4,1e-5,"delta mirror sees sun with weight one");
    a.setLights({},0);a.environment(8,4,std::vector<float>(8*4*3,1));
    auto glass=triangle(0);glass[37]=1.5f;glass[38]=1;glass[45]=0;a.setGeometry(glass);
    checkNear(a.mean(surfacePath)[0],.04+.96/(1.5*1.5),.005,"delta dielectric radiance transport");
    auto tir=a.mean(R"(
void main(){Material m=getMaterial(0);vec3 V=normalize(vec3(.9,0,.43589));
BsdfSample s=SampleDisneyBSDF(V,vec3(0,0,1),m,1.5,vec3(rand(),rand(),rand()));
outputColor=vec4(s.weight.r,float(s.delta),dot(s.direction,reflect(-V,vec3(0,0,1))),s.pdf);})");
    checkNear(tir[0],1,1e-6,"total internal reflection weight");checkNear(tir[1],1,0,"TIR is delta");checkNear(tir[2],1,1e-5,"TIR direction");checkNear(tir[3],0,0,"delta has no solid-angle density");
    glass[37]=1;glass[45]=.5f;a.setGeometry(glass);
    checkNear(a.mean(surfacePath)[0],1,.001,"index-matched rough transmission is straight-through delta");
    auto mixed=triangle(0);mixed[37]=1.5f;mixed[45]=0;a.setGeometry(mixed);
    auto mixture=a.mean(R"(
void main(){Material m=getMaterial(0);BsdfSample s=SampleDisneyBSDF(vec3(0,0,1),vec3(0,0,1),m,1.0/1.5,vec3(rand(),rand(),rand()));
outputColor=vec4(float(s.delta),float(HasNonDeltaLobes(m,1.0/1.5)),s.delta?s.weight.r:0.0,1);})");
    checkNear(mixture[0],.04/1.04,.003,"mixed BSDF discrete probability");
    checkNear(mixture[1],1,0,"mixed BSDF retains diffuse NEE");
    checkNear(mixture[2],.04,.003,"mixed BSDF delta contribution");
}

static void mediumTests(Audit& a) {
    auto entry=triangle(1,false),exit=triangle(3,true);
    for(auto* t:{&entry,&exit}) {
        (*t)[39]=1; (*t)[40]=1; (*t)[41]=2;
        (*t)[32]=(*t)[33]=(*t)[34]=.5f;
    }
    auto geometry=entry;geometry.insert(geometry.end(),exit.begin(),exit.end());a.setGeometry(geometry);
    a.setLights({},0);a.environment(8,4,std::vector<float>(8*4*3,1));
    const QString shadow=R"(
void main(){MediumStack m;m.size=0;
outputColor=vec4(ShadowTransmittance(vec3(0),vec3(0),vec3(0,0,1),4.0,m),1);})";
    const QString volumePath=R"(
void main(){Ray r;r.startPoint=vec3(0);r.direction=vec3(0,0,1);
outputColor=vec4(pathTracingImportanceSampling(r,12).render_color,1);})";
    checkNear(a.mean(shadow)[0],std::exp(-2.),1e-4,"Beer-Lambert shadow across closed absorber");
    checkNear(a.mean(volumePath)[0],std::exp(-2.),1e-4,"Beer-Lambert attenuation before environment emission");
    auto innerEntry=triangle(1.5f,false),innerExit=triangle(2.5f,true);
    for(auto* t:{&innerEntry,&innerExit}) { (*t)[39]=1;(*t)[40]=1;(*t)[41]=4;(*t)[32]=(*t)[33]=(*t)[34]=.5f; }
    auto nested=geometry;nested.insert(nested.end(),innerEntry.begin(),innerEntry.end());nested.insert(nested.end(),innerExit.begin(),innerExit.end());
    a.setGeometry(nested);
    checkNear(a.mean(shadow)[0],std::exp(-3.),1e-4,"nested medium restores outer absorber on exit");
    a.setGeometry(geometry);

    // An interior camera initializes its first containing medium.
    checkNear(a.mean("void main(){Ray r;r.startPoint=vec3(0,0,2);r.direction=vec3(0,0,1);outputColor=vec4(pathTracingImportanceSampling(r,0).render_color,1); }")[0],
         std::exp(-1.),1e-4,"camera inside absorber");
    for(int i:{0,80}){geometry[i+40]=2;geometry[i+41]=.7f;geometry[i+32]=geometry[i+33]=geometry[i+34]=0;}
    a.setGeometry(geometry);
    checkNear(a.mean(shadow)[0],std::exp(-1.4),1e-4,"scattering extinction on shadow segments");
    checkNear(a.mean(volumePath)[0],std::exp(-1.4),.006,"free-flight survival matches extinction");
    for(int i:{0,80}) geometry[i+32]=geometry[i+33]=geometry[i+34]=1;
    a.setGeometry(geometry);
    checkNear(a.mean(volumePath)[0],1,.035,"conservative volume white furnace with NEE and phase MIS");
    auto phase=a.mean(R"(
void main(){vec3 incoming=vec3(0,0,1);vec3 d=SampleHG(-incoming,.6,rand(),rand());
float p=PhaseHG(dot(-incoming,d),.6);outputColor=vec4(d.z,1.0/p,0,1);})");
    checkNear(phase[0],.6,.01,"HG forward-scattering convention");
    checkNear(phase[1],4*std::acos(-1.),.25,"HG phase PDF normalization");
    for(int i:{0,80}){geometry[i+40]=3;geometry[i+41]=.3f;geometry[i+32]=.2f;geometry[i+33]=.3f;geometry[i+34]=.4f;}
    a.setGeometry(geometry);
    checkNear(a.mean(volumePath)[0],1+.2*.3*2,1e-4,"homogeneous medium emission integral");
    a.setGeometry({});a.setLights({},0);
}


int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    try { Audit audit; hdrTests(audit); analyticTests(audit); alphaDeltaTests(audit); mediumTests(audit); std::cout<<"Lighting numerical tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
    return 0;
}
