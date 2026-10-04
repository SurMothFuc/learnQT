#pragma once
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtEndian>
#include <qfloat16.h>
#include <QMetaType>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

inline float encodeDisplaySrgb(float value) {
    value=std::max(0.f,std::min(1.f,value));
    return value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f;
}
inline quint16 renderHalfBits(float value) {
    quint32 bits;std::memcpy(&bits,&value,4);
    const unsigned sign=(bits>>16)&0x8000u,mantissa=bits&0x7fffffu;
    const int exponent=int((bits>>23)&255u)-127+15;
    unsigned encoded,remainder,halfway;
    if(exponent<=0) {
        if(exponent<-10)return quint16(sign);
        const unsigned m=mantissa|0x800000u,shift=unsigned(14-exponent);
        encoded=m>>shift;remainder=m&((1u<<shift)-1u);halfway=1u<<(shift-1);
    }else {
        encoded=(unsigned(exponent)<<10)|(mantissa>>13);remainder=mantissa&8191u;halfway=4096;
    }
    if(remainder>halfway || (remainder==halfway && (encoded&1u)))++encoded;
    return quint16(sign|encoded);
}
struct RenderResult {
    QSize size;
    unsigned samples=0;
    quint64 version=0;
    QJsonObject settings;
    // Top-down rows, scene-linear RGB with Rec.709/sRGB primaries and D65.
    std::vector<float> beauty,denoised,normal,albedo,depth,variance,sampleCount;
    bool valid() const {
        return size.width()>0 && size.height()>0 && beauty.size()==size_t(size.width())*size.height()*3;
    }
    QImage display(float exposure,int tonemap,bool filtered=true,bool legacyGamma=false) const {
        if(!valid())return {};
        const auto &values=filtered && denoised.size()==beauty.size()?denoised:beauty;
        QImage image(size,QImage::Format_RGB32);
        for(size_t i=0;i<values.size();i+=3) {
            double c[3];for(int k=0;k<3;++k)c[k]=std::max(0.,double(values[i+k]))*std::exp2(double(exposure));
            const double luminance=c[0]*.212671+c[1]*.715160+c[2]*.072169;
            int bytes[3];for(int k=0;k<3;++k) {
                double v=c[k];if(tonemap==0)v/=1+luminance/1.5f;
                else if(tonemap==1) {
                    if(v>1){const double inverse=1/v;v=(2.51+.03*inverse)/(2.43+.59*inverse+.14*inverse*inverse);}
                    else v=(v*(2.51*v+.03))/(v*(2.43*v+.59)+.14);
                }
                v=std::max(0.,std::min(1.,v));
                bytes[k]=qRound(255*(legacyGamma?std::pow(v,1.f/2.2f):encodeDisplaySrgb(v)));
            }
            const int p=int(i/3);image.setPixel(p%size.width(),p/size.width(),qRgb(bytes[0],bytes[1],bytes[2]));
        }
        return image;
    }
    // OpenEXR v2, single-part uncompressed scanlines. No display transform.
    // Explicit FLOAT or HALF; count/depth/variance stay FLOAT to retain range.
    bool writeExr(const QString &path,bool half,QString &error) const {
        try {
            if(!valid())throw std::runtime_error("No linear render result");
            struct Channel { const std::vector<float>*values; int stride,component,type; };
            std::map<QByteArray,Channel> channels;
            const size_t pixels=size_t(size.width())*size.height();
            auto rgb=[&](const QByteArray &prefix,const std::vector<float> &values) {
                if(values.empty())return;
                if(values.size()!=pixels*3)throw std::runtime_error("Mismatched RGB channel dimensions");
                for(int c=0;c<3;++c)channels[prefix+QByteArray("RGB").mid(c,1)]={&values,3,c,half?1:2};
            };
            rgb("",beauty);rgb("denoised.",denoised);rgb("normal.",normal);rgb("albedo.",albedo);
            for(auto item:{std::make_pair("depth.center",&depth),std::make_pair("variance",&variance),std::make_pair("sampleCount",&sampleCount)})
                if(!item.second->empty()) {
                    if(item.second->size()!=pixels)throw std::runtime_error("Mismatched scalar channel dimensions");
                    channels[item.first]={item.second,1,0,2};
                }
            auto u32=[](QByteArray &b,quint32 v){v=qToLittleEndian(v);b.append(reinterpret_cast<const char*>(&v),4);};
            auto u64=[](QByteArray &b,quint64 v){v=qToLittleEndian(v);b.append(reinterpret_cast<const char*>(&v),8);};
            auto f32=[&](QByteArray &b,float f){quint32 v;std::memcpy(&v,&f,4);u32(b,v);};
            auto z=[](QByteArray &b,const QByteArray &s){b+=s;b+=char(0);};
            QByteArray header;u32(header,20000630);u32(header,2);
            auto attribute=[&](const QByteArray &name,const QByteArray &type,const QByteArray &value){z(header,name);z(header,type);u32(header,value.size());header+=value;};
            QByteArray list;
            int rowBytes=0;
            for(const auto &c:channels) {
                z(list,c.first);u32(list,c.second.type);list.append(4,char(0));u32(list,1);u32(list,1);
                rowBytes+=size.width()*(c.second.type==1?2:4);
            }
            list+=char(0);attribute("channels","chlist",list);
            attribute("compression","compression",QByteArray(1,char(0)));
            QByteArray window;u32(window,0);u32(window,0);u32(window,size.width()-1);u32(window,size.height()-1);
            attribute("dataWindow","box2i",window);attribute("displayWindow","box2i",window);
            attribute("lineOrder","lineOrder",QByteArray(1,char(0)));
            QByteArray number;f32(number,1);attribute("pixelAspectRatio","float",number);
            QByteArray center;f32(center,0);f32(center,0);attribute("screenWindowCenter","v2f",center);
            attribute("screenWindowWidth","float",number);
            QByteArray chroma;for(float v:{.64f,.33f,.30f,.60f,.15f,.06f,.3127f,.3290f})f32(chroma,v);
            attribute("chromaticities","chromaticities",chroma);
            auto metadata=settings;metadata["samples"]=int(samples);metadata["version"]=double(version);
            metadata["space"]="scene-linear Rec.709 D65";
            metadata["aovSemantics"]="normal/albedo are averaged denoising guides; depth.center is pixel-center nearest geometry range, zero means background; variance is unbiased luminance sample variance, zero below two valid samples; sampleCount is valid samples";
            attribute("learnqtSettings","string",QJsonDocument(metadata).toJson(QJsonDocument::Compact));
            header+=char(0);
            QSaveFile file(path);file.setDirectWriteFallback(false);
            if(!file.open(QIODevice::WriteOnly))throw std::runtime_error(file.errorString().toStdString());
            auto put=[&](const QByteArray &b){if(file.write(b)!=b.size())throw std::runtime_error(file.errorString().toStdString());};
            put(header);QByteArray offsets;
            quint64 offset=quint64(header.size())+quint64(size.height())*8;
            for(int y=0;y<size.height();++y){u64(offsets,offset);offset+=quint64(rowBytes)+8;}
            put(offsets);
            for(int y=0;y<size.height();++y) {
                QByteArray row;row.reserve(rowBytes+8);u32(row,y);u32(row,rowBytes);
                for(const auto &c:channels)for(int x=0;x<size.width();++x) {
                    const auto &ch=c.second;const float value=(*ch.values)[(size_t(y)*size.width()+x)*ch.stride+ch.component];
                    if(!std::isfinite(value))throw std::runtime_error("Non-finite EXR channel value");
                    if(ch.type==1) {
                        if(std::abs(value)>65504)throw std::runtime_error("HALF range exceeded; export FLOAT EXR");
                        quint16 bits=qToLittleEndian(renderHalfBits(value));row.append(reinterpret_cast<const char*>(&bits),2);
                    }else f32(row,value);
                }
                put(row);
            }
            if(!file.commit())throw std::runtime_error(file.errorString().toStdString());
            error.clear();return true;
        }catch(const std::exception &e){error=QString::fromUtf8(e.what());return false;}
    }
};
using RenderResultPtr=std::shared_ptr<const RenderResult>;
Q_DECLARE_METATYPE(RenderResultPtr)
