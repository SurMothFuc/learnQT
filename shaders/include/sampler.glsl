// Fixed domains, independent of branches, tile layout and execution backend.
// Camera: 0..1; lens: 2..3 (reserved); each bounce owns twelve dimensions.
// Sobol dimensions use a per-pixel/dimension/seed digital shift. Higher
// dimensions and variable-length alpha/boundary events use a counter hash.
uint SamplerHash(uint x) {
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;
}
bool UnifiedSamplerEnabled() {
#ifdef UNIFIED_SAMPLER
    return true;
#elif defined(LEGACY_SAMPLER)
    return false;
#else
    return useUnifiedSampler; // Standalone numerical fixtures switch explicitly.
#endif
}
uint SamplerPixelKey() {
    return SamplerHash(uint(TRACE_PIXEL_COORD.x)^SamplerHash(uint(TRACE_PIXEL_COORD.y)+0x9e3779b9u)^samplerSeed);
}
float SamplerFloat(uint bits) { return float(bits>>8)*(1.0/16777216.0); }
float SampleDimension(uint dimension) {
    if(!UnifiedSamplerEnabled())return rand();
    uint key=SamplerHash(SamplerPixelKey()^SamplerHash(dimension+0x68bc21ebu));
    uint bits=dimension<120u?samplerSobol[dimension/4u][int(dimension%4u)]^key:
        SamplerHash(key^SamplerHash(samplerIndex));
    return SamplerFloat(bits);
}
float SampleBounce(int bounce,int dimension) {return SampleDimension(4u+uint(bounce)*12u+uint(dimension));}
float SampleEvent(uint domain,uint event) {
    if(!UnifiedSamplerEnabled())return rand();
    return SamplerFloat(SamplerHash(SamplerPixelKey()^SamplerHash(samplerIndex)^SamplerHash(domain)^SamplerHash(event)));
}
uint samplerAlphaQuery=0u;
void BeginAlphaQuery() {++samplerAlphaQuery;}
float SampleAlpha(int surface) {return SampleEvent(0xa17a0000u^uint(surface),samplerAlphaQuery);}
