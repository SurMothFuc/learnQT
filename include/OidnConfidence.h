#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

// Bound post-filter displacement by the measured uncertainty of the raw film.
// This protects converged/emissive texture detail; it does not make denoising
// unbiased and does not change the raw accumulator. Low-spp estimates remain
// unguarded until eight valid samples are available.
inline size_t protectOidnOutput(const float *raw,float *filtered,size_t pixels,
                               const std::vector<float> &secondMoment,
                               const std::vector<float> &counts,
                               const std::vector<unsigned char> *eligible=nullptr) {
    if(secondMoment.size()!=pixels || counts.size()!=pixels)throw std::runtime_error("OIDN confidence dimensions mismatch");
    if(eligible && eligible->size()!=pixels)throw std::runtime_error("OIDN eligibility dimensions mismatch");
    size_t protectedPixels=0;
    for(size_t p=0;p<pixels;++p) {
        double weight=1;
        for(int c=0;c<3;++c)if(!std::isfinite(filtered[p*3+c]))weight=0;
        if(counts[p]<=0)weight=0;
        else if(weight>0 && counts[p]>=8 && (!eligible || (*eligible)[p])) {
            const double mean=raw[p*3]*.212671+raw[p*3+1]*.715160+raw[p*3+2]*.072169;
            const double variance=std::max(0.,double(secondMoment[p])-mean*mean);
            const double limit=3*std::sqrt(variance/(counts[p]-1));
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
