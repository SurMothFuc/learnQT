#include "LightingAudit.h"

static void materialTextureUploadTests(Audit& a) {
    GLuint texture=0;a.glGenTextures(1,&texture);
    a.glActiveTexture(GL_TEXTURE5);a.glBindTexture(GL_TEXTURE_2D_ARRAY,texture);
    for(auto format:{QImage::Format_RGB888,QImage::Format_RGBA8888,QImage::Format_ARGB32_Premultiplied}) {
        QImage source(8,8,format);
        for(int y=0;y<8;++y) for(int x=0;x<8;++x)
            source.setPixelColor(x,y,y<4?QColor(220,150,30,128):QColor(10,60,240,255));
        for(int size:{8,4,16}) {
            const QImage image=prepareMaterialTextureImage(source,QSize(size,size));
            a.glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,size,size,1,0,GL_RGBA,GL_UNSIGNED_BYTE,image.constBits());
            std::vector<unsigned char> pixels(size*size*4);
            a.glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            check(a.glGetError()==GL_NO_ERROR,"material texture upload/readback");
            for(int half:{0,1}) {
                const QColor expected=source.pixelColor(2,half==0?6:2);
                const int offset=4*((half==0?size/4:3*size/4)*size+size/2);
                const int channels[]={expected.red(),expected.green(),expected.blue(),expected.alpha()};
                for(int c=0;c<4;++c)
                    check(std::abs(int(pixels[offset+c])-channels[c])<=2,
                        "resized texture preserves RGBA channels, straight alpha and vertical orientation");
            }
        }
    }
    a.glDeleteTextures(1,&texture);
    std::cout<<"Material image GPU upload preserves color/alpha with and without scaling\n";
}

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
    const auto whiteComponents=a.mean(R"(void main(){
        Ray r;r.startPoint=vec3(0,0,1);r.direction=vec3(0,0,-1);HitResult h=hitBVH(r);
        MediumStack m;m.size=0;vec3 direct=EstimateDirectLighting(h,vec3(1),1.0,m);
        vec2 uv=CranleyPattersonRotation(vec2(sobelNumber[0],sobelNumber[1]));
        BsdfSample s=SampleSurfaceBSDF(h,1.0,vec3(uv,rand()));
        r.startPoint=OffsetRayOrigin(h.hitPoint,h.positionError,h.geometricNormal,s.direction);r.direction=s.direction;
        HitResult next=hitBVH(r);
        vec3 escaped=s.weight*InfiniteEmission(s.direction,h.hitPoint,s.delta,s.pdf);
        outputColor=vec4(direct.r,escaped.r,s.weight.r,float(next.isHit));})");
    std::cout<<"White HDR components: direct="<<whiteComponents[0]<<" bsdf="<<whiteComponents[1]
             <<" weight="<<whiteComponents[2]<<" selfHit="<<whiteComponents[3]<<std::endl;
    checkNear(whiteComponents[3],0,0,"oblique secondary rays do not re-hit the zero-coordinate plane");
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
outputColor=vec4(s.weight.r,float(s.delta),0,s.pdf);})");
    // Read actual directions and compare against a CPU double-precision oracle.
    // A GPU dot(normalize(...), normalize(...)) also tests driver constant folding,
    // and produced 0.999869 on Intel even when the returned direction was correct.
    auto tirDirections=a.run(R"(
void main(){Material m=getMaterial(0);float x=.76+.23*gl_FragCoord.x/float(width);
vec3 V=vec3(x,0,sqrt(1.0-x*x));
BsdfSample s=SampleDisneyBSDF(V,vec3(0,0,1),m,1.5,vec3(rand(),rand(),rand()));
outputColor=vec4(s.direction,float(s.delta));})");
    double maxDirectionError=0;
    for(int y=0;y<Audit::resolution;++y) for(int x=0;x<Audit::resolution;++x) {
        const double vx=.76+.23*(x+.5)/Audit::resolution;
        const double expected[]={-vx,0,std::sqrt(1-vx*vx)};
        const size_t pixel=4*(y*Audit::resolution+x);
        for(int c=0;c<3;++c)
            maxDirectionError=std::max(maxDirectionError,std::abs(tirDirections[pixel+c]-expected[c]));
        check(tirDirections[pixel+3]==1,"TIR direction sweep remains delta");
    }
    checkNear(tir[0],1,1e-6,"total internal reflection weight");checkNear(tir[1],1,0,"TIR is delta");
    checkNear(maxDirectionError,0,1e-5,"TIR maximum direction error (CPU reference)");
    checkNear(tir[3],0,0,"delta has no solid-angle density");
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

static void standardInterfaceLightingTests(Audit& a) {
    a.environment(8,4,std::vector<float>(8*4*3,0));
    a.setLights(light(2,3,.5f,3),1);
    // The emitter is visible from the surface, but every connection from
    // the interior receiver crosses the material interface.
    const QString blocked=R"(
void main(){MediumStack m;m.size=0;
outputColor=vec4(ShadowTransmittance(vec3(0),vec3(0),vec3(0,0,1),4.0,m).r,
EstimateVolumeLighting(vec3(0),vec3(0,0,-1),vec3(1),m).r,0,1);})";
    const QString surfaceEstimator=R"(
void main(){Ray r;r.startPoint=vec3(0,0,2);r.direction=vec3(0,0,-1);
HitResult h=hitBVH(r);float eta=1.0/h.material.IOR;
BsdfSample s=SampleDisneyBSDF(-r.direction,h.normal,h.material,eta,vec3(rand(),rand(),rand()));
MediumStack m;m.size=0;
outputColor=vec4(float(HasNonDeltaLobes(h.material,eta)),float(s.delta),
EstimateDirectLighting(h,vec3(1),eta,m).r,1);})";
    for(float roughness:{0.f,.12f}) {
        auto glass=triangle(1);glass[37]=1.5f;glass[38]=1;glass[45]=roughness;
        // The rough fixture also carries a scattering medium, like water.
        if(roughness>0){glass[40]=2;glass[41]=6;}
        a.setGeometry(glass);
        const auto shadow=a.mean(blocked,false),surface=a.mean(surfaceEstimator,false);
        checkNear(shadow[0],0,0,"material interface blocks straight shadow connection");
        checkNear(shadow[1],0,0,"material interface blocks volume light connection");
        checkNear(surface[0],roughness>0?1:0,0,"surface NEE only for continuous BSDF lobes");
        checkNear(surface[1],roughness>0?0:1,0,"ideal versus rough dielectric BSDF event");
        if(roughness==0) checkNear(surface[2],0,0,"pure delta has no ordinary NEE contribution");
        else check(surface[2]>0,"rough dielectric ordinary NEE lost visible emitter");
    }
    a.setGeometry({});a.setLights({},0);
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


static void rasterEnvironmentTests(Audit &a)
{
    constexpr int w = 64, h = 32;
    std::vector<float> rgb(w*h*3, 1.f);
    auto sh = rasterDiffuseEnvironment(rgb.data(), w, h);
    checkNear(sh[0].x() * .2820947918, 1., 1e-6, "constant HDR diffuse energy");
    for (int i = 1; i < 9; ++i)
        checkNear(sh[i].length(), 0., .003, "constant HDR higher bands");
    const float onePixel[] = {1, 1, 1};
    const auto single = rasterDiffuseEnvironment(onePixel, 1, 1);
    checkNear(single[0].x() * .2820947918, 1., 1e-6, "1x1 HDR diffuse energy");
    for (int i = 1; i < 9; ++i)
        checkNear(single[i].length(), 0., 1e-6, "1x1 HDR has no directional detail");
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
    {
        const double theta = 3.141592653589793 * (y + .5) / h;
        const double phi = 6.283185307179586 * ((x + .5) / w - .5);
        rgb[(y*w+x)*3] = float(1 + .8 * std::sin(theta) * std::cos(phi));
        rgb[(y*w+x)*3+1] = float(.3 + .2 * std::cos(theta));
        rgb[(y*w+x)*3+2] = float(.2 + .15 * std::sin(theta) * std::sin(phi));
    }
    a.environment(w, h, rgb);
    sh = rasterDiffuseEnvironment(rgb.data(), w, h);
    QOpenGLShaderProgram program;
    check(program.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
#version 330 core
out vec3 worldPosition; out vec3 worldNormal; out vec2 uv0; flat out int materialIndex;
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0;
    gl_Position=vec4(p,0,1); worldPosition=vec3(p,0);
    worldNormal=vec3(0,0,1); uv0=p; materialIndex=0;
})"), "raster test vertex shader");
    QString fragment = read(QString::fromStdString(getShaderPath("raster.frag")));
    fragment.replace("#version 330 core", "#version 330 core\n#define INSTANCED_SCENE 1\n#define USEENVIRONMENTMAP\n");
    check(program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragment), program.log().toStdString());
    check(program.link(), program.log().toStdString());
    GLuint buffer = 0, texture = 0;
    std::vector<float> material(40, 0);
    material[4] = material[5] = material[6] = .5f;
    material[21] = .5f;
    for (int i = 26; i <= 31; ++i) material[i] = -1;
    a.buffer(buffer, texture, 8, GL_RGBA32F, material);
    auto render = [&](QVector3D eye, float intensity, float rotation) {
        program.bind();
        program.setUniformValue("eye", eye);
        program.setUniformValue("nLights", 0); program.setUniformValue("nAnalyticLights", 0);
        program.setUniformValue("lights", 4);
        program.setUniformValue("materialTable", 8);
        program.setUniformValue("materialTextures", 5); program.setUniformValue("materialTextureInfo", 6);
        program.setUniformValue("materialTextureCount", 0);
        program.setUniformValue("hdrMap", 0); program.setUniformValue("hdrCache", 1);
        program.setUniformValue("environmentIntensity", intensity);
        program.setUniformValue("environmentRotation", rotation);
        program.setUniformValueArray("diffuseEnvironment", sh.data(), int(sh.size()));
        a.target->bind(); a.glViewport(0,0,Audit::resolution,Audit::resolution);
        a.glBindVertexArray(a.vao); a.glDrawArrays(GL_TRIANGLES,0,3);
        std::vector<float> pixels(Audit::resolution*Audit::resolution*4);
        a.glReadPixels(0,0,Audit::resolution,Audit::resolution,GL_RGBA,GL_FLOAT,pixels.data());
        check(a.glGetError() == GL_NO_ERROR, "raster environment GL error");
        return pixels;
    };
    for (const auto eye : {QVector3D(0,0,2), QVector3D(1,1,2)})
    {
        const auto dark = render(eye, 0, 0);
        for (int test = 0; test < 3; ++test)
        {
            const float intensity = test == 1 ? .25f : 1.f;
            const auto lit = render(eye, intensity, test == 2 ? 1.57079632679f : 0.f);
            const double expected[] = {test == 2 ? .5*(1+.8*2/3) : .5,
                                       .15, test == 2 ? .1 : .15};
            double maximumError = 0;
            for (size_t i = 0; i < lit.size(); ++i)
            {
                check(std::isfinite(lit[i]), "finite raster surface color");
                if (i%4 == 3) { check(lit[i] == 1, "raster surface remains opaque"); continue; }
                maximumError = std::max(maximumError,
                    std::abs(lit[i] - dark[i] - expected[i%4] * intensity));
            }
            checkNear(maximumError, 0., .003, "flat surface diffuse lighting independent of camera/pixel");
        }
    }
    program.release(); a.target->release();
    a.glDeleteTextures(1, &texture); a.glDeleteBuffers(1, &buffer);
}

int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    try { Audit audit;
        if(app.arguments().contains("--analytic-only")){analyticTests(audit);return 0;}
        materialTextureUploadTests(audit); hdrTests(audit); analyticTests(audit); alphaDeltaTests(audit); standardInterfaceLightingTests(audit); mediumTests(audit); rasterEnvironmentTests(audit); std::cout<<"Lighting numerical tests passed\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
    return 0;
}
