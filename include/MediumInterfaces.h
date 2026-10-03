#pragma once
#include "Scene.h"
#include <array>
#include <map>
#include <algorithm>

// Exact coincident triangles with opposite outward normals form an explicit
// contact interface. No tolerance is used: nearby thin walls must stay distinct.
inline std::vector<QVector4D> mediumContactData(const Scene &scene) {
    struct Face { int surface,instance; QVector3D normal; };
    using Point=std::array<float,3>;
    using Key=std::array<Point,3>;
    std::map<Key,std::vector<Face>> faces;
    std::vector<bool> supported(scene.instances.size(),false);
    for(size_t i=0;i<scene.instances.size();++i) {
        const auto &object=scene.instances[i];const auto &m=scene.materials[object.material];
        supported[i]=object.visible && m.alphaMode!=Mask && m.alphaMode!=Blend && (m.mediumtype!=None || m.transmission>0) && scene.meshes[object.mesh]->closedBoundary();
    }
    for(int instance=0;instance<int(scene.instances.size());++instance) {
        if(!supported[instance])continue;
        const auto &object=scene.instances[instance];const auto &triangles=scene.meshes[object.mesh]->triangles;
        for(int local=0;local<int(triangles.size());++local) {
            const auto &triangle=triangles[local];Key key;const QVector3D positions[]={triangle.p1,triangle.p2,triangle.p3};
            for(int c=0;c<3;++c){const auto p=object.transform.map(positions[c]);key[c]={p.x(),p.y(),p.z()};}
            std::sort(key.begin(),key.end());
            const auto normal=object.inverse.transposed().mapVector(QVector3D::crossProduct(triangle.p2-triangle.p1,triangle.p3-triangle.p1)).normalized();
            faces[key].push_back({object.surfaceOffset+local,instance,normal});
        }
    }
    std::map<int,int> contacts;
    for(const auto &entry:faces) {
        const auto &v=entry.second;
        if(v.size()==2 && v[0].instance!=v[1].instance && QVector3D::dotProduct(v[0].normal,v[1].normal)<-.999f) {
            contacts[v[0].surface]=v[1].instance;contacts[v[1].surface]=v[0].instance;
        }
    }
    std::vector<QVector4D> data;
    for(const auto &entry:contacts) {
        const auto &m=scene.materials[scene.instances[entry.second].material];
        data.emplace_back(float(entry.first),float(entry.second),0,0);
        data.emplace_back(float(m.mediumtype),std::max(0.f,m.mediumDensity),std::max(-.999f,std::min(.999f,m.mediumAnisotropy)),m.transmission>0?m.IOR:1.f);
        data.emplace_back(QVector3D(std::max(0.f,m.mediumColor.x()),std::max(0.f,m.mediumColor.y()),std::max(0.f,m.mediumColor.z())),0);
    }
    return data;
}
