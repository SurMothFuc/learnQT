#pragma once
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <vector>
#include <cmath>
#include <stdexcept>

// Opt-in GPU event capture. Read only after a completed round; no production
// per-frame readback or change to beauty/auxiliary accumulation.
struct PathDiagnosticCapture {
    QSize size;
    quint64 rounds=0;
    std::array<quint64,7> events{};
    std::vector<unsigned> pixelFlags;
    QJsonObject first;
    void reset(QSize s) { size=s; rounds=0; events={}; first={}; pixelFlags.assign(size_t(s.width())*s.height(),0); }
    void append(const std::array<std::vector<float>,5> &a,unsigned sample) {
        const size_t pixels=size_t(size.width())*size.height();
        for(const auto &v:a) if(v.size()!=pixels*4)throw std::runtime_error("Diagnostic capture size mismatch");
        auto number=[](float v)->QJsonValue{return std::isfinite(v)?QJsonValue(double(v)):QJsonValue(std::isnan(v)?"NaN":v>0?"+Inf":"-Inf");};
        auto vector=[&](int attachment,size_t p){return QJsonArray{number(a[attachment][p*4]),number(a[attachment][p*4+1]),number(a[attachment][p*4+2])};};
        for(size_t p=0;p<pixels;++p) {
            unsigned flags=0;
            for(int c=0;c<7;++c) {
                const float n=a[c<4?0:1][p*4+(c<4?c:c-4)];
                if(!std::isfinite(n)||n<0)throw std::runtime_error("Invalid diagnostic event count");
                events[c]+=quint64(n);if(n>0)flags|=1u<<c;
            }
            pixelFlags[p]|=flags;
            if(flags && first.isEmpty()) {
                const unsigned meta=unsigned(a[1][p*4+3]);
                first={{"pixel",QJsonArray{int(p)%size.width(),int(p)/size.width()}},{"sampleIndex",int(sample)},
                    {"imagePixel",QJsonArray{int(p)%size.width(),size.height()-1-int(p)/size.width()}},
                    {"flags",int(meta&127u)},{"mediumCount",int((meta>>8)&15u)},
                    {"depth",int((meta>>12)&127u)},{"stage",int((meta>>19)&15u)},
                    {"rayOrigin",vector(2,p)},{"rayDirection",vector(3,p)},
                    {"throughput",vector(4,p)},{"surface",double(a[2][p*4+3])},
                    {"eta",number(a[3][p*4+3])},{"etaScale",number(a[4][p*4+3])}};
            }
        }
        ++rounds;
    }
    QJsonObject json() const {
        QJsonObject counts;const char *names[]={"nonFinite","rejected","invalidRay","bvhOverflow","mediumOverflow","boundaryLimit","boundaryMismatch"};
        for(int c=0;c<7;++c)counts[names[c]]=double(events[c]);
        return {{"capturedRounds",double(rounds)},{"eventCounts",counts},{"firstEvent",first},
            {"stateMeaning","First event by sample index then framebuffer pixel order; owning path state before the failing operation. Stage 0 initialization, 1 traversal, 2 medium, 3 surface, 4 sample validation."}};
    }
    QImage image() const {
        QImage im(size,QImage::Format_RGB32);im.fill(Qt::black);
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x) {
            const unsigned f=pixelFlags[size_t(y)*size.width()+x];
            if(f)im.setPixel(x,size.height()-1-y,qRgb((f&7)?255:80,(f&64)?255:0,(f&56)?255:0));
        }
        return im;
    }
};
