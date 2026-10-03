#pragma once
#include "Scene.h"
#include <algorithm>
#include <array>
#include <QVector4D>

struct InitialMediumState {
    std::vector<QVector4D> properties,colors,identity;
    int unsupported=0;
};
inline SceneHit initialBoundaryHit(const MeshGeometry &mesh,const SceneInstance &instance,
                                  const QVector3D &origin,const QVector3D &direction) {
    const auto o=instance.inverse.map(origin),d=instance.inverse.mapVector(direction);
    using Vector=std::array<double,3>;
    auto vector=[](const QVector3D &p){return Vector{p.x(),p.y(),p.z()};};
    auto subtract=[](const Vector &a,const Vector &b){return Vector{a[0]-b[0],a[1]-b[1],a[2]-b[2]};};
    auto cross=[](const Vector &a,const Vector &b){return Vector{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    auto dot=[](const Vector &a,const Vector &b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    SceneHit hit;double nearest=1e30;std::vector<int> pending;
    if(mesh.nodes.size()>1)pending.push_back(1);
    while(!pending.empty()) {
        const auto &node=mesh.nodes[pending.back()];pending.pop_back();
        double low=0,high=nearest;
        for(int c=0;c<3;++c) {
            if(d[c]==0) {if(o[c]<node.AA[c] || o[c]>node.BB[c])high=-1;}
            else {double a=(double(node.AA[c])-o[c])/d[c],b=(double(node.BB[c])-o[c])/d[c];
                if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);}
        }
        if(high<low)continue;
        if(!node.n){pending.push_back(node.left);pending.push_back(node.right);continue;}
        for(int index=node.index;index<node.index+node.n;++index) {
            const auto &t=mesh.triangles[index];const auto a=vector(t.p1),ab=subtract(vector(t.p2),a),ac=subtract(vector(t.p3),a);
            const auto p=cross(vector(d),ac);const double determinant=dot(ab,p);
            if(determinant==0)continue;
            const auto relative=subtract(vector(o),a),q=cross(relative,ab);
            const double u=dot(relative,p)/determinant,v=dot(vector(d),q)/determinant,distance=dot(ac,q)/determinant;
            if(u>=0 && v>=0 && u+v<=1 && distance>0 && distance<nearest){nearest=distance;hit={0,index,float(distance)};}
        }
    }
    return hit;
}
// Camera initialization is done once per camera/scene update, using immutable
// BLASes. Only closed, oriented per-instance boundaries are supported.
inline InitialMediumState initialMediaAt(const Scene &scene,const QVector3D &origin) {
    struct Entry { float exit; int instance; Material material; };
    std::vector<Entry> entries;
    InitialMediumState result;
    const std::array<QVector3D,3> directions={QVector3D(.3123f,.5271f,.7907f).normalized(),
        QVector3D(-.8111f,.4197f,.4079f).normalized(),QVector3D(.2171f,-.9113f,.3481f).normalized()};
    for(int id=0;id<int(scene.instances.size());++id) {
        const auto &instance=scene.instances[id];
        if(!instance.visible || instance.material<0)continue;
        const auto &material=scene.materials[instance.material];
        if(material.mediumtype==None && material.transmission<=0)continue;
        const auto &mesh=*scene.meshes[instance.mesh];
        if(material.alphaMode==Mask || material.alphaMode==Blend || !mesh.closedBoundary()) { ++result.unsupported;continue; }
        const auto &b=instance.bounds;
        if(origin.x()<b.minimum.x()||origin.x()>b.maximum.x()||origin.y()<b.minimum.y()||origin.y()>b.maximum.y()||origin.z()<b.minimum.z()||origin.z()>b.maximum.z())continue;
        int inside=0;float exit=0;
        for(int d=0;d<3;++d) {
            const auto hit=initialBoundaryHit(mesh,instance,origin,directions[d]);
            if(hit.triangle<0)continue;
            const auto &t=mesh.triangles[hit.triangle];
            const auto n=instance.inverse.transposed().mapVector(QVector3D::crossProduct(t.p2-t.p1,t.p3-t.p1));
            if(QVector3D::dotProduct(n,directions[d])>0)++inside;
            if(d==0)exit=hit.distance;
        }
        if(inside==3)entries.push_back({exit,id,material});
        else if(inside!=0)++result.unsupported;
    }
    std::sort(entries.begin(),entries.end(),[](const Entry &a,const Entry &b){return a.exit>b.exit;});
    if(entries.size()>8)throw std::runtime_error("Camera is inside more than eight medium boundaries");
    for(const auto &e:entries) {
        const auto &m=e.material;
        result.properties.emplace_back(float(m.mediumtype),std::max(0.f,m.mediumDensity),std::max(-.999f,std::min(.999f,m.mediumAnisotropy)),m.transmission>0?m.IOR:1.f);
        result.colors.emplace_back(QVector3D(std::max(0.f,m.mediumColor.x()),std::max(0.f,m.mediumColor.y()),std::max(0.f,m.mediumColor.z())),0);
        result.identity.emplace_back(float(e.instance),0,0,0);
    }
    return result;
}
