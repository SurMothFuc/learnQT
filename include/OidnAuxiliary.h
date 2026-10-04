#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
inline void decodeOidnNormals(float *destination, const float *encoded, size_t values,
                              double &minimum, double &maximum)
{
    minimum = 1; maximum = -1;
    for (size_t i=0; i+2<values; i+=3)
    {
        float x=encoded[i]*2-1, y=encoded[i+1]*2-1, z=encoded[i+2]*2-1;
        const float length=std::sqrt(x*x+y*y+z*z);
        if ((encoded[i]!=0 || encoded[i+1]!=0 || encoded[i+2]!=0) && length>1e-6f && std::isfinite(length)) { x/=length; y/=length; z/=length; }
        else x=y=z=0;
        destination[i]=x; destination[i+1]=y; destination[i+2]=z;
        for (size_t c=i;c<i+3;++c) { minimum=std::min(minimum,double(destination[c])); maximum=std::max(maximum,double(destination[c])); }
    }
}
