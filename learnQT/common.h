#pragma once
#include <vector>


#define PI 3.1415926 
#define INF 1145141919.0
#define MAX_LOW_RESOLUTION 200
float radians(float angle);
float* calculateHdrCache(float* HDR, int width, int height);
// 格林码 
unsigned int grayCode(unsigned int i);

// 生成第 d 维度的第 i 个 sobol 数
float sobol(unsigned int d, unsigned int i);

std::vector<float> getSobelRandomNumber(unsigned int frameCount, unsigned int maxBounce);

