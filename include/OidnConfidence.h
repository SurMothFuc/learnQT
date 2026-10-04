#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

// Bound post-filter displacement by the measured uncertainty of the raw film.
// This protects converged/emissive texture detail; it does not make denoising
// unbiased and does not change the raw accumulator. Low-spp estimates remain
// unguarded until eight valid samples are available. A single pixel can miss
// rare illumination and report deceptively small variance. Compatible nearby
// guides provide a spatial floor for the uncertainty of its RGB mean.
inline size_t protectOidnOutput(const float *raw,float *filtered,size_t pixels,
                               const std::vector<float> &secondMoment,
                               const std::vector<float> &counts,
                               const std::vector<unsigned char> *eligible=nullptr,
                               int width=0,const float *albedo=nullptr,const float *normal=nullptr) {
    if(secondMoment.size()!=pixels || counts.size()!=pixels)throw std::runtime_error("OIDN confidence dimensions mismatch");
    if(eligible && eligible->size()!=pixels)throw std::runtime_error("OIDN eligibility dimensions mismatch");
    if(width<0 || (width && pixels%size_t(width)))throw std::runtime_error("OIDN confidence image dimensions mismatch");
    const int height=width?int(pixels/size_t(width)):0;
    auto uncertainty=[&](size_t p) {
        const double mean=raw[p*3]*.212671+raw[p*3+1]*.715160+raw[p*3+2]*.072169;
        return counts[p]>1?std::max(0.,double(secondMoment[p])-mean*mean)/(counts[p]-1):0.;
    };
    size_t protectedPixels=0;
    for(size_t p=0;p<pixels;++p) {
        double weight=1;
        for(int c=0;c<3;++c)if(!std::isfinite(filtered[p*3+c]))weight=0;
        if(counts[p]<=0)weight=0;
        else if(weight>0 && counts[p]>=8 && (!eligible || (*eligible)[p])) {
            double variance=uncertainty(p);
            if(width && albedo && normal) {
                double sum[3]={},sumSquares=0,pooled=0;int neighbors=0;
                const int x=int(p%size_t(width)),y=int(p/size_t(width));
                for(int yy=std::max(0,y-1);yy<=std::min(height-1,y+1);++yy)
                    for(int xx=std::max(0,x-1);xx<=std::min(width-1,x+1);++xx) {
                        const size_t q=size_t(yy)*width+xx;
                        if(counts[q]<8 || (eligible && !(*eligible)[q]))continue;
                        double colorDistance=0,colorScale=0,dot=0,lengthP=0,lengthQ=0;
                        for(int c=0;c<3;++c) {
                            const double a=albedo[p*3+c],b=albedo[q*3+c];
                            colorDistance+=(a-b)*(a-b);colorScale+=std::max(a*a,b*b);
                            const double n=normal[p*3+c],m=normal[q*3+c];
                            dot+=n*m;lengthP+=n*n;lengthQ+=m*m;
                        }
                        if(colorDistance> .0025+.01*colorScale ||
                           lengthP<.25 || lengthQ<.25 || dot<.95*std::sqrt(lengthP*lengthQ))continue;
                        ++neighbors;pooled+=uncertainty(q);
                        for(int c=0;c<3;++c) {const double v=raw[q*3+c];sum[c]+=v;sumSquares+=v*v;}
                    }
                // Require several compatible pixels; do not cross texture/normal
                // edges or borrow confidence from unsupported transport paths.
                if(neighbors>=3) {
                    double dispersion=sumSquares/neighbors;
                    for(int c=0;c<3;++c)dispersion-=std::pow(sum[c]/neighbors,2);
                    variance=std::max(variance,std::max(pooled/neighbors,std::max(0.,dispersion)));
                }
            }
            const double limit=3*std::sqrt(variance);
            double displacement=0;for(int c=0;c<3;++c)displacement+=std::pow(double(filtered[p*3+c])-raw[p*3+c],2);
            weight=displacement>0?std::min(1.,limit/std::sqrt(displacement)):1;
        }
        if(weight<1) {
            ++protectedPixels;
            for(int c=0;c<3;++c)filtered[p*3+c]=weight==0?raw[p*3+c]:float(raw[p*3+c]+weight*(double(filtered[p*3+c])-raw[p*3+c]));
        }
    }
    return protectedPixels;
}
