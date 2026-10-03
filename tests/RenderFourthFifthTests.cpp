#include "LightingAudit.h"
#include "InitialMedia.h"
#include "MediumInterfaces.h"
#include "PathDiagnosticCapture.h"
#include "RenderEvidence.h"
#include "RenderResult.h"
#include "OidnConfidence.h"
#include "renderer.h"
#include <QDir>
#include <QJsonDocument>
#include <QApplication>
#include <QMutex>
QMutex param_mutex;

static std::shared_ptr<MeshGeometry> box(float r) {
    auto mesh=std::make_shared<MeshGeometry>();
    QVector3D v[8];for(int i=0;i<8;++i)v[i]={i&1?r:-r,i&2?r:-r,i&4?r:-r};
    const int faces[][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    for(const auto &f:faces)for(int k=1;k<3;++k) {
        Triangle t;t.p1=v[f[0]];t.p2=v[f[k]];t.p3=v[f[k+1]];
        t.n1=t.n2=t.n3=QVector3D::crossProduct(t.p2-t.p1,t.p3-t.p1).normalized();
        mesh->triangles.push_back(t);
    }
    mesh->build();return mesh;
}
static void initialTests() {
    Scene scene(false);scene.meshes={box(3),box(1)};
    check(scene.meshes[0]->closedBoundary(),"closed box rejected");
    Material outer,inner;outer.transmission=inner.transmission=1;outer.IOR=1.5;inner.IOR=1.33;
    outer.mediumtype=Absorb;outer.mediumDensity=.2;outer.mediumColor={.5,.6,.7};
    scene.materials={outer,inner};
    for(int i=0;i<2;++i){SceneInstance object;object.mesh=i;object.material=i;object.bounds=scene.meshes[i]->bounds;scene.instances.push_back(object);}
    const auto both=initialMediaAt(scene,{0,0,0});
    check(both.properties.size()==2 && both.identity[0].x()==0 && both.identity[1].x()==1,"initial nested ordering");
    checkNear(both.properties[0].w(),1.5,1e-6,"initial glass IOR");
    checkNear(both.properties[1].w(),1.33,1e-6,"initial water IOR");
    check(initialMediaAt(scene,{2,0,0}).properties.size()==1,"camera in outer shell");
    check(initialMediaAt(scene,{4,0,0}).properties.empty(),"outside camera has initial media");
    Scene contact(false);contact.meshes={box(1)};contact.materials={outer,inner};
    for(int i=0;i<2;++i){SceneInstance object;object.mesh=0;object.material=i;object.transform.translate(0,0,i==0?1.f:-1.f);object.inverse=object.transform.inverted();object.bounds=contact.meshes[0]->bounds.transformed(object.transform);contact.instances.push_back(object);}
    contact.encodeGeometry();const auto interfaces=mediumContactData(contact);
    check(interfaces.size()==12,"Exact glass/water contact did not pair both triangles and sides");
    contact.instances[1].transform.translate(0,0,-.00001f);contact.instances[1].inverse=contact.instances[1].transform.inverted();
    check(mediumContactData(contact).empty(),"Nearby thin gap was incorrectly merged into a contact");
    auto open=box(1);open->triangles.pop_back();open->boundaryStatus=-1;
    check(!open->closedBoundary(),"open mesh treated as closed boundary");
}
static void boundaryTests(Audit &gpu) {
    const QString body=R"(void main(){
        MediumStack media;media.size=0;HitResult h;h.triangleIndex=5;h.isInside=false;
        h.geometricNormal=vec3(0,0,1);h.material.mediumtype=MEDIUM_NONE;h.material.transmission=1.0;h.material.IOR=1.5;
        float into=BoundaryEta(media,h);bool ok=CrossMediumBoundary(media,h,vec3(0,0,-1));
        h.triangleIndex=7;h.material.IOR=1.33;float inner=BoundaryEta(media,h);
        ok=ok&&CrossMediumBoundary(media,h,vec3(0,0,-1));h.isInside=true;
        float exit=BoundaryEta(media,h);ok=ok&&CrossMediumBoundary(media,h,vec3(0,0,1));
        float restored=CurrentIOR(media);h.triangleIndex=5;h.material.IOR=1.5;
        ok=ok&&CrossMediumBoundary(media,h,vec3(0,0,1));
        outputColor=vec4(into,inner,exit,ok && media.size==0 && abs(restored-1.5)<1e-6?1.0:0.0);
    })";
    gpu.extraDefines="#define TEST_BOUNDARIES\n";
    auto wrapped=body;wrapped.replace("void main(){","void main(){");
    // Audit exposes production uniforms; force this interface in the test.
    gpu.boundaryMedia=true;
    const auto result=gpu.mean(body,false);
    checkNear(result[0],1./1.5,1e-6,"air to glass");
    checkNear(result[1],1.5/1.33,1e-6,"glass to water");
    checkNear(result[2],1.33/1.5,1e-6,"water to glass");
    checkNear(result[3],1,0,"matching boundary exits restore enclosing state");
    const auto mismatch=gpu.mean(R"(void main(){
        MediumStack s;s.size=1;s.entries[0]=Vacuum();s.entries[0].boundary=4;
        HitResult h;h.triangleIndex=9;h.isInside=true;h.geometricNormal=vec3(0,0,1);
        h.material.transmission=1.0;h.material.mediumtype=MEDIUM_NONE;
        bool ok=CrossMediumBoundary(s,h,vec3(0,0,1));outputColor=vec4(float(ok),float(s.size),float(pathDiagnosticFlags),1);
    })",false);
    checkNear(mismatch[0],0,0,"wrong boundary exit rejected");
    checkNear(mismatch[1],1,0,"wrong exit did not pop enclosing boundary");
    checkNear(mismatch[2],64,0,"boundary mismatch diagnosed");
    gpu.boundaryMedia=false;
}
static void clearcoatTests(Audit &gpu) {
    gpu.correctCoat=true;
    auto result=gpu.mean(R"(void main(){
        vec3 V=normalize(vec3(.7,0,.714)),L=normalize(vec3(-.4,.2,.894));vec3 H=normalize(V+L);float pdf;
        float f=EvalClearcoat(.2,V,L,H,pdf).r;
        float F=mix(.04,1.0,SchlickFresnel(dot(V,H))),D=GTR1(H.z,.2);
        float ref=F*D*SmithG(L.z,.25)*SmithG(V.z,.25)/(4.0*L.z*V.z);
        float reverse=EvalClearcoat(.2,L,V,H,pdf).r;
        outputColor=vec4(f,ref,abs(f-reverse),1);
    })",false);
    checkNear(result[0],result[1],1e-7,"clearcoat normalized microfacet formula");
    checkNear(result[2],0,1e-7,"clearcoat reciprocity");
    const auto energy=gpu.mean(R"(void main(){
        float z=rand(),phi=TWO_PI*rand();vec3 V=vec3(0,0,1),L=vec3(sqrt(1.0-z*z)*cos(phi),sqrt(1.0-z*z)*sin(phi),z);float pdf;
        vec3 f=EvalClearcoat(.3,V,L,normalize(V+L),pdf);
        outputColor=vec4(f*L.z*TWO_PI,pdf);
    })",false);
    check(energy[0]>0 && energy[0]<1,"clearcoat directional reflectance bounded");
    gpu.correctCoat=false;
}
static void materialMatrix(Audit &gpu) {
    std::vector<float> triangle(80,0);const float vertices[]={-10,-10,0,10,-10,0,0,10,0};
    for(int v=0;v<3;++v)for(int c=0;c<3;++c)triangle[v*4+c]=vertices[v*3+c];
    for(int v=0;v<3;++v)triangle[(v+3)*4+2]=1;
    triangle[28]=.6f;triangle[29]=.4f;triangle[30]=.2f;triangle[36]=.3f;triangle[37]=1.5f;
    triangle[45]=.4f;triangle[60]=triangle[62]=1;triangle[61]=.5f;
    for(int i=54;i<60;++i)triangle[i]=-1;
    gpu.setGeometry(triangle);gpu.correctCoat=true;
    for(int material=0;material<4;++material)for(double cosV:{1.0,.5}) {
        QString setup=QString("Material m=getMaterial(0);m.metallic=%1;m.transmission=%2;m.clearcoat=%3;vec3 V=vec3(sqrt(1.0-%4*%4),0,%4);")
            .arg(material==1?1:0).arg(material==2?1:0).arg(material==3?1:0).arg(cosV,0,'f',6);
        auto sample=gpu.mean("void main(){"+setup+R"(
            BsdfSample s=SampleDisneyBSDF(V,vec3(0,0,1),m,1.0/1.5,vec3(rand(),rand(),rand()));
            outputColor=vec4(s.weight,float(s.pdf>0.0));})",false);
        auto integrated=gpu.mean("void main(){"+setup+R"(
            float z=1.0-2.0*rand(),phi=TWO_PI*rand();vec3 L=vec3(sqrt(1.0-z*z)*cos(phi),sqrt(1.0-z*z)*sin(phi),z);float pdf;
            vec3 f=DisneyEval(V,vec3(0,0,1),L,m,1.0/1.5,pdf);
            outputColor=vec4(f*abs(L.z)*4.0*PI,pdf*4.0*PI);})",false);
        for(int c=0;c<3;++c)checkNear(sample[c],integrated[c],.04,"sample/eval directional energy matrix");
        checkNear(sample[3],integrated[3],.05,"continuous PDF mass includes null events");
        check(integrated[3]>0 && integrated[3]<=1.05,"continuous PDF invalid probability mass");
        if(material==1)check(integrated[0]<=1.02,"single-scatter metal generated excess energy");
    }
    const auto reciprocity=gpu.mean(R"(void main(){
        Material m=getMaterial(0);m.transmission=1.0;
        float eta=1.0/1.5;vec3 V=normalize(vec3(.3,0,1)),L=normalize(refract(-V,vec3(0,0,1),eta));float pdf;
        float f=DisneyEval(V,vec3(0,0,1),L,m,eta,pdf).r;
        vec3 reverseV=vec3(L.x,-L.y,-L.z),reverseL=vec3(V.x,-V.y,-V.z);
        float reverse=DisneyEval(reverseV,vec3(0,0,1),reverseL,m,1.0/eta,pdf).r;
        outputColor=vec4(f/max(reverse,1e-20),eta*eta,0,1);})",false);
    checkNear(reciprocity[0],reciprocity[1],1e-5,"radiance BTDF eta-squared reciprocity");
    gpu.correctCoat=false;
}
static void partialFilmTest() {
    auto document=SceneDocument::empty();Material material;material.baseColor={0,0,0};material.emissive={2,2,2};
    auto json=SceneDocument::materialJson(material);json["id"]="emitter";document.root["materials"]=QJsonArray{json};
    QMatrix4x4 world;world.rotate(90,1,0,0);world.scale(4);
    document.root["models"]=QJsonArray{QJsonObject{{"id","plane"},{"source",QString(RESOURCE_DIR)+"/models/plane.obj"},
        {"material","emitter"},{"normalize",false},{"smoothNormals",false},{"transform",sceneMatrixJson(world)}}};
    document.root["camera"]=QJsonObject{{"position",QJsonArray{0,0,3}},{"target",QJsonArray{0,0,0}},{"up",QJsonArray{0,1,0}},{"fov",45}};
    QString error;auto scene=Scene::prepareDocument(document,error);check(bool(scene),error.toStdString());
    auto snapshot=scene->document.settings();snapshot.denoise=false;snapshot.useEnvironmentMap=false;snapshot.renderLow=false;
    snapshot.maxRenderFrames=2;snapshot.maxBounces=1;snapshot.tileSize=16;snapshot.useTileRendering=true;
    Renderer renderer(32,32,snapshot,nullptr,scene.get());renderer.formal=true;renderer.prepareJob({32,32},snapshot,kInitialSceneDirty);
    while(renderer.samples()<1)renderer.render(32,32,snapshot,0,4);
    auto before=renderer.linearResult(snapshot,false);
    renderer.render(32,32,snapshot,0,1);check(renderer.roundInProgress(),"Partial-tile fixture failed to stay in progress");
    const auto stopped=renderer.linearResult(snapshot,false);
    check(stopped->samples==1 && stopped->beauty==before->beauty && stopped->sampleCount==before->sampleCount,
        "Stopped film mixed incomplete tiles into a complete snapshot");
}
static void initialMediaRefreshTest() {
    auto document=SceneDocument::empty();Material medium;medium.alphaMode=Transparent;
    medium.mediumtype=Emissive;medium.mediumDensity=1;medium.mediumColor={.3f,.4f,.5f};
    auto material=SceneDocument::materialJson(medium);material["id"]="volume";
    document.root["materials"]=QJsonArray{material};
    QMatrix4x4 world;
    document.root["models"]=QJsonArray{QJsonObject{{"id","box"},{"source",QString(RESOURCE_DIR)+"/models/quad.obj"},
        {"material","volume"},{"normalize",false},{"smoothNormals",false},{"transform",sceneMatrixJson(world)}}};
    document.root["camera"]=QJsonObject{{"position",QJsonArray{0,0,0}},{"target",QJsonArray{0,0,-1}},{"up",QJsonArray{0,1,0}},{"fov",45}};
    QString error;auto scene=Scene::prepareDocument(document,error);check(bool(scene),error.toStdString());
    check(initialMediaAt(*scene,scene->camera.position).properties.size()==1,"Refresh fixture must start inside a closed volume");
    auto snapshot=scene->document.settings();snapshot.denoise=false;snapshot.useEnvironmentMap=false;snapshot.renderLow=false;
    snapshot.maxRenderFrames=1;snapshot.maxBounces=2;snapshot.antialiasing=false;snapshot.useTileRendering=false;
    Renderer reused(16,16,snapshot,nullptr,scene.get());reused.formal=true;reused.prepareJob({16,16},snapshot,kInitialSceneDirty);
    reused.render(16,16,snapshot,0,1);const auto original=reused.linearResult(snapshot,false)->beauty;
    auto compare=[&](SceneDirtyFlag dirty) {
        reused.render(16,16,snapshot,toSceneDirtyFlags(dirty),1);
        const auto updated=reused.linearResult(snapshot,false)->beauty;
        auto freshScene=Scene::prepareDocument(scene->document,error);check(bool(freshScene),error.toStdString());
        Renderer fresh(16,16,snapshot,nullptr,freshScene.get());fresh.formal=true;fresh.prepareJob({16,16},snapshot,kInitialSceneDirty);
        fresh.render(16,16,snapshot,0,1);const auto expected=fresh.linearResult(snapshot,false)->beauty;
        for(size_t i=0;i<expected.size();++i)checkNear(updated[i],expected[i],1e-6,"Updated initial media differ from a fresh renderer");
        return updated;
    };
    auto changed=scene->document;auto materials=changed.root["materials"].toArray();auto m=materials[0].toObject();
    m["mediumDensity"]=2;materials[0]=m;changed.root["materials"]=materials;
    scene->applyEditorDocument(changed,true,true);
    check(compare(SceneDirtyFlag::Material)!=original,"Material refresh fixture did not change volume radiance");
    changed=scene->document;auto objects=changed.root["objects"].toArray();auto object=objects[0].toObject();
    world.translate(0,0,-2);object["transform"]=sceneMatrixJson(world);objects[0]=object;changed.root["objects"]=objects;
    scene->applyEditorDocument(changed,true,false);
    check(initialMediaAt(*scene,scene->camera.position).properties.empty(),"Transformed volume must leave the camera");
    compare(SceneDirtyFlag::Transform);compare(SceneDirtyFlag::SceneBuffers);
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    try { initialTests();
        std::vector<float> confidentRaw={.5f,.5f,.5f},candidate={.8f,.8f,.8f},moments={.25f},counts={32};
        check(protectOidnOutput(confidentRaw.data(),candidate.data(),1,moments,counts)==1 && candidate==confidentRaw,"Converged texture detail was blurred");
        std::vector<unsigned char> noisy={0};candidate={.8f,.8f,.8f};
        check(protectOidnOutput(confidentRaw.data(),candidate.data(),1,moments,counts,&noisy)==0 && candidate[0]==.8f,"Rough transport lost its denoising correction");
        checkNear(encodeDisplaySrgb(.0031308f),.04044994,1e-7,"sRGB linear toe");
        checkNear(encodeDisplaySrgb(.18f),.4613561,1e-6,"sRGB middle gray");
        RenderResult result;result.size={2,2};result.samples=64;result.beauty={.001f,.18f,16.f,1.f,2.f,4.f,.25f,.5f,.75f,0.f,.01f,.1f};
        result.normal={0,0,1,0,1,0,1,0,0,-1,0,0};result.albedo=result.beauty;
        result.depth={1,2,0,4};result.variance={0,.1f,1,2};result.sampleCount={64,63,0,62};
        const auto original=result.beauty;result.display(-3,2);check(result.beauty==original,"Display altered linear film");
        const QString directory=app.arguments().value(1,"build/render-fourth-fifth/format-tests");QDir().mkpath(directory);QString error;
        check(result.writeExr(directory+"/fixture-float.exr",false,error),error.toStdString());
        check(result.writeExr(directory+"/fixture-half.exr",true,error),error.toStdString());
        QFile originalFile(directory+"/fixture-half.exr");check(originalFile.open(QIODevice::ReadOnly),"Read atomic export baseline");const auto bytes=originalFile.readAll();originalFile.close();
        result.beauty[0]=70000;check(!result.writeExr(directory+"/fixture-half.exr",true,error),"HALF overflow was silently clipped");
        check(originalFile.open(QIODevice::ReadOnly) && originalFile.readAll()==bytes,"Failed export overwrote existing EXR");originalFile.close();
        result.beauty[0]=.001f;
        if(app.arguments().contains("--formats-only"))return 0;
        Audit gpu;boundaryTests(gpu);clearcoatTests(gpu);materialMatrix(gpu);partialFilmTest();initialMediaRefreshTest();
        std::cout<<"Fourth/fifth batch foundations passed\n";return 0;
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
