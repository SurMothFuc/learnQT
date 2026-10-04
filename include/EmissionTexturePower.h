#pragma once
#include "Mesh.h"
#include <QMap>
#include <array>
#include <cmath>

// Point/level-zero emission convention shared with the GPU emitter evaluation.
// The result is an importance-weight estimate, never a replacement for radiance.
class EmissionTexturePower
{
    const std::vector<TextureAsset> &textures;
    QMap<int,QImage> images;
    static float linear(int value) {
        static const auto table=[] {std::array<float,256> values{};
            for(int i=0;i<256;++i){float c=i/255.f;values[i]=c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}return values;}();
        return table[value];
    }
    static int wrap(int p,int size,int mode) {
        if(mode==1||mode==3)return std::max(0,std::min(size-1,p));
        const int period=mode==2?size*2:size;p=(p%period+period)%period;
        return mode==2&&p>=size?period-1-p:p;
    }
  public:
    explicit EmissionTexturePower(const std::vector<TextureAsset> &source):textures(source){}
    QVector4D sample(int source,QVector2D uv,bool color) {
        if(source<0||source>=int(textures.size()))return {1,1,1,1};
        const auto &asset=textures[source];
        if(!images.contains(source))images[source]=asset.image.convertToFormat(QImage::Format_RGBA8888);
        const auto &image=images[source];if(image.isNull())return {1,1,1,1};
        uv=QVector2D(uv.x()*asset.uvScale.x(),uv.y()*asset.uvScale.y());
        const float c=std::cos(asset.uvRotation),s=std::sin(asset.uvRotation);
        uv=QVector2D(c*uv.x()-s*uv.y(),s*uv.x()+c*uv.y())+asset.uvOffset;
        if((asset.wrapS==3&&(uv.x()<0||uv.x()>1))||(asset.wrapT==3&&(uv.y()<0||uv.y()>1)))return {};
        auto coordinate=[](float v,int mode) {
            if(mode==1||mode==3)return std::max(0.f,std::min(1.f,v));
            if(mode==0)return v-std::floor(v);
            v=std::fmod(v,2.f);if(v<0)v+=2;return v<=1?v:2-v;
        };
        uv={coordinate(uv.x(),asset.wrapS),coordinate(uv.y(),asset.wrapT)};
        auto texel=[&](int x,int y) {
            if((asset.wrapS==3&&(x<0||x>=image.width()))||(asset.wrapT==3&&(y<0||y>=image.height())))return QVector4D{};
            x=wrap(x,image.width(),asset.wrapS);y=wrap(y,image.height(),asset.wrapT);
            const auto p=image.constScanLine(image.height()-1-y)+x*4;
            return QVector4D(color?linear(p[0]):p[0]/255.f,color?linear(p[1]):p[1]/255.f,
                             color?linear(p[2]):p[2]/255.f,p[3]/255.f);
        };
        const float x=uv.x()*image.width(),y=uv.y()*image.height();
        if(asset.magFilter==9728)return texel(int(std::floor(x)),int(std::floor(y)));
        const int ix=int(std::floor(x-.5f)),iy=int(std::floor(y-.5f));
        const float fx=x-.5f-ix,fy=y-.5f-iy;
        return (texel(ix,iy)*(1-fx)+texel(ix+1,iy)*fx)*(1-fy)+
               (texel(ix,iy+1)*(1-fx)+texel(ix+1,iy+1)*fx)*fy;
    }
    static float luminance(QVector3D c){return .212671f*c.x()+.715160f*c.y()+.072169f*c.z();}
    float estimate(const Triangle &t,const Material &m) {
        if(m.emissive.lengthSquared()==0)return 0;
        if((m.alphaMode==Blend && m.opacity<=0)||(m.alphaMode==Mask && m.opacity<m.alphaCutoff))return 0;
        const bool textured=m.emissiveTex>=0 || ((m.alphaMode==Mask||m.alphaMode==Blend) && (m.baseColorTex>=0||m.opacityTex>=0));
        if(!textured) {
            const float coverage=m.alphaMode==Blend?std::max(0.f,std::min(1.f,m.opacity)):1.f;
            return luminance(m.emissive)*coverage;
        }
        double sum=0;
        constexpr int steps=16;
        for(int y=0;y<steps;++y)for(int x=0;x<steps;++x) {
            const float root=std::sqrt((x+.5f)/steps),b1=root*(y+.5f)/steps,b2=root-b1;
            const auto uv=t.uv1*(1-root)+t.uv2*b1+t.uv3*b2;
            const auto emission=sample(m.emissiveTex,uv,true).toVector3D()*m.emissive;
            float opacity=std::max(0.f,std::min(1.f,m.opacity*sample(m.baseColorTex,uv,false).w()*sample(m.opacityTex,uv,false).x()));
            float coverage=m.alphaMode==Mask?float(opacity>=m.alphaCutoff):m.alphaMode==Blend?opacity:1.f;
            sum+=luminance(emission)*coverage;
        }
        // A quadrature grid can miss a small bright/opaque island. Keep support
        // with a small positive floor; zero-PMF BSDF emission remains valid too.
        float fallback=luminance(m.emissive);
        if(m.emissiveTex>=0 && m.emissiveTex<int(textures.size()))
            fallback*=std::max(1e-6f,luminance(textures[m.emissiveTex].averageLinearColor));
        return std::max(float(sum/(steps*steps)),fallback*.001f);
    }
};
