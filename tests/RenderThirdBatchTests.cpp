#include "LightingAudit.h"
#include "EmissionTexturePower.h"
#include <QJsonDocument>
#include <QJsonObject>

void samplerTests(Audit &gpu)
{
    gpu.unifiedSampler=true;
    gpu.samplerSeedValue=421;
    const QString body=R"(void main(){
        vec4 value=vec4(SampleDimension(0u),SampleDimension(4u),SampleDimension(20u),SampleDimension(119u));
        float bsdf=SampleBounce(2,3);BeginAlphaQuery();float alpha=SampleAlpha(19);
        for(int i=0;i<17;++i){rand();SampleEvent(12u,uint(i));}
        if(bsdf!=SampleBounce(2,3)||alpha!=SampleAlpha(19))value=vec4(-1);
        outputColor=value;})";
    std::vector<unsigned> bins(size_t(Audit::resolution)*Audit::resolution*4,0u);
    for(unsigned index=0;index<8;++index) {
        gpu.sampleIndex=index;
        const auto pixels=gpu.run(body,false);
        for(size_t i=0;i<pixels.size();++i) {
            check(pixels[i]>=0 && pixels[i]<1,"Sampler escaped [0,1) or consumed another domain");
            const unsigned bit=1u<<unsigned(pixels[i]*8);
            check((bins[i]&bit)==0,"Digitally shifted Sobol lost one-dimensional stratification");
            bins[i]|=bit;
        }
    }
    for(unsigned value:bins)check(value==255,"Sobol did not cover all eight strata");
    const auto fallback=gpu.mean(R"(void main(){
        float a=SampleDimension(120u),b=SampleDimension(700u);
        BeginAlphaQuery();float first=SampleAlpha(2);BeginAlphaQuery();float second=SampleAlpha(2);
        outputColor=vec4(a,b,float(a==b),float(first==second));})",false);
    checkNear(fallback[0],.5,.01,"High-dimension counter sampler mean");
    checkNear(fallback[1],.5,.01,"Deep-path counter sampler mean");
    check(fallback[2]<.001 && fallback[3]<.001,"Sampler dimensions or alpha queries alias");
    auto pixels=gpu.run(body,false);gpu.samplerSeedValue=422;
    const auto changed=gpu.run(body,false);
    check(pixels!=changed,"Seed did not change the samples");
    gpu.unifiedSampler=false;
    std::cout<<"Fixed sampler domains, Sobol strata, deep/event streams and seed change passed\n";
}
void emissionPowerTests()
{
    TextureAsset source;source.image=QImage(16,8,QImage::Format_RGBA8888);source.image.fill(QColor(0,0,0,255));
    for(int y=0;y<8;++y)for(int x=8;x<16;++x)source.image.setPixelColor(x,y,QColor(255,255,255,255));
    source.averageLinearColor={.5,.5,.5};source.wrapS=source.wrapT=1;source.magFilter=9728;
    std::vector<TextureAsset> textures{source};EmissionTexturePower power(textures);
    Triangle bright,dark;bright.uv1={.6,.1};bright.uv2={.9,.1};bright.uv3={.9,.9};
    dark.uv1={.1,.1};dark.uv2={.4,.1};dark.uv3={.4,.9};
    Material material;material.emissive={2,2,2};material.emissiveTex=0;
    checkNear(power.estimate(bright,material),2,1e-5,"UV-local bright emission power");
    check(power.estimate(dark,material)>0 && power.estimate(dark,material)<.002,
          "Dark UV region did not retain only conservative sampling support");
    material.alphaMode=Blend;material.opacity=.25;
    checkNear(power.estimate(bright,material),.5,1e-5,"Blend alpha enters UV power estimate");
    material.alphaMode=Mask;material.opacity=.2;material.alphaCutoff=.5;
    check(power.estimate(bright,material)==0,"Invisible Mask emitter retained power");
    material.alphaMode=Opaque;material.opacity=1;
    textures[0].uvScale={-1,1};textures[0].uvOffset={1,0};
    EmissionTexturePower mirrored(textures);
    checkNear(mirrored.estimate(dark,material),2,1e-5,"Mirrored UV emission power");
    textures[0].wrapS=3;textures[0].uvOffset={3,0};EmissionTexturePower border(textures);
    checkNear(border.sample(0,{.1,.1},true).x(),0,0,"Border emission is zero");
    textures[0].image.fill(QColor(128,128,128,255));textures[0].uvScale={1,1};textures[0].uvOffset={0,0};
    EmissionTexturePower linear(textures);
    checkNear(linear.sample(0,{.5,.5},true).x(),.2158605,1e-6,"CPU power estimator decodes color before filtering");
}

void rrTests(Audit &gpu)
{
    std::vector<float> triangles;
    for(int i=0;i<4;++i) {
        const float z=i<2?-i*.4f:-1.f-(i-2)*.4f;
        std::vector<float> t(80,0);float positions[]={-20,-20,z,20,-20,z,0,20,z};
        for(int v=0;v<3;++v)for(int c=0;c<3;++c)t[v*4+c]=positions[v*3+c];
        if(i%2)for(int c=0;c<3;++c)std::swap(t[4+c],t[8+c]);
        for(int v=0;v<3;++v)t[(v+3)*4+2]=i%2?-1:1;
        t[28]=t[29]=t[30]=1;t[36]=1;t[37]=2.5f;t[38]=1;t[60]=1;t[61]=.5;t[62]=1;
        t[54]=t[55]=t[56]=t[57]=t[58]=t[59]=-1;
        triangles.insert(triangles.end(),t.begin(),t.end());
    }
    gpu.setGeometry(triangles);gpu.setLights({},0);gpu.environment(1,1,{1,1,1});
    gpu.unifiedSampler=true;
    const QString body=R"(void main(){Ray ray;ray.startPoint=vec3(0,0,1);ray.direction=vec3(0,0,-1);
        float value=pathTracingImportanceSampling(ray,12).render_color.r;outputColor=vec4(value,value*value,0,1);})";
    gpu.etaScaleRR=false;const auto old=gpu.mean(body);
    gpu.etaScaleRR=true;gpu.rrDepth=3;const auto current=gpu.mean(body);
    gpu.rrDepth=64;const auto reference=gpu.mean(body);
    checkNear(old[0],reference[0],.02,"Legacy RR mean matches no-RR finite-depth reference");
    checkNear(current[0],reference[0],1e-6,"etaScale RR mean matches no-RR finite-depth reference");
    check(current[1]-current[0]*current[0] < (old[1]-old[0]*old[0])*.02,
          "etaScale RR did not remove temporary-transmission variance");
    std::cout<<"RR variance: "<<old[1]-old[0]*old[0]<<" -> "<<current[1]-current[0]*current[0]<<'\n';
    gpu.etaScaleRR=false;gpu.unifiedSampler=false;gpu.rrDepth=3;
}
void lightGroupTests(Audit &gpu)
{
    std::vector<float> t(80,0);const float points[]={-20,-20,0,20,-20,0,0,20,0};
    for(int v=0;v<3;++v)for(int c=0;c<3;++c)t[v*4+c]=points[v*3+c];
    for(int v=0;v<3;++v)t[(v+3)*4+2]=1;
    t[28]=t[29]=t[30]=1;t[36]=t[37]=t[45]=t[60]=t[62]=1;t[61]=.5;
    for(int i=54;i<60;++i)t[i]=-1;
    std::vector<float> sun(16,0);sun[0]=3;sun[1]=-1;sun[2]=1;sun[3]=.2;sun[6]=-1;
    sun[8]=sun[9]=sun[10]=2;sun[12]=1;
    gpu.setGeometry(t);gpu.setLights(sun,1);gpu.environment(7,5,std::vector<float>(105,1));
    gpu.unifiedSampler=true;gpu.powerGroups=true;
    const QString body=R"(void main(){Ray ray;ray.startPoint=vec3(0,0,1);ray.direction=vec3(0,0,-1);
        outputColor=vec4(pathTracingImportanceSampling(ray,1).render_color,1);})";
    const double expected=29./28+2*std::pow(std::sin(.2),2);
    for(float probability:{.05f,.5f,.95f}) {
        gpu.groupProbability=probability;
        checkNear(gpu.mean(body)[0],expected,.012,"HDR + sun MIS preserves energy with different group probabilities");
        const auto query=gpu.mean(R"(void main(){
            LightSample s=SampleOneLight(vec3(0),SampleBounce(0,0),SampleBounce(0,1),SampleBounce(0,2));
            float p=s.lightIndex<0?EnvSelectPdf()*hdrPdf(s.direction,hdrResolution):SunLightPdf(GetEncodedLight(0),s.direction);
            outputColor=vec4(abs(p-s.pdf)/max(s.pdf,1e-20),float(s.lightIndex<0),0,1);})");
        checkNear(query[0],0,1e-5,"Group sample/PMF query agreement");
        checkNear(query[1],probability,.01,"Observed group selection frequency");
    }
    gpu.groupProbability=0;
    checkNear(gpu.mean(body)[0],expected,.015,"Zero environment NEE mass retains BSDF environment emission");
    gpu.powerGroups=false;gpu.unifiedSampler=false;
}

int main(int argc,char **argv)
{
    QGuiApplication app(argc,argv);
    try {emissionPowerTests();Audit gpu;samplerTests(gpu);rrTests(gpu);lightGroupTests(gpu);}
    catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
