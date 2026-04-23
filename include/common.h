#pragma once
#include "iostream"
#include "qvector3d.h"
#include <vector>


#define PI 3.1415926 
#define INF 1145141919.0
#define MAX_LOW_RESOLUTION 200
constexpr unsigned int MAX_BOUNCES_LIMIT = 60u;
float radians(float angle);
float* calculateHdrCache(float* HDR, int width, int height);
// 格林码 
unsigned int grayCode(unsigned int i);

// 生成第 d 维度的第 i 个 sobol 数
float sobol(unsigned int d, unsigned int i);

std::vector<float> getSobelRandomNumber(unsigned int frameCount, unsigned int maxBounce);

std::string getResourcePath(const std::string& _relativePath);

std::string getShaderPath(const std::string& _shaderName);

class vec3 {
public:
    double x, y, z;

    // 构造函数
    explicit vec3(double x_ = 0, double y_ = 0, double z_ = 0)
        : x(x_), y(y_), z(z_) {
    }
    explicit vec3(const QVector3D& _c)
        : x(_c.x()), y(_c.y()), z(_c.z()) {
    }
    QVector3D toQvector() {
        return QVector3D(x,y,z);
    }
    
    // 向量减法
    vec3 operator-(const vec3& rhs) const {
        return vec3(x - rhs.x, y - rhs.y, z - rhs.z);
    }

    // 叉乘运算
    vec3 cross(const vec3& rhs) const {
        return vec3(
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x
        );
    }

    // 向量长度平方
    double length_squared() const {
        return x * x + y * y + z * z;
    }

    // 带安全保护的归一化
    vec3 normalized(double epsilon = 1e-12) const {
        const double len_sq = length_squared();
        if (len_sq < epsilon * epsilon) {
            throw "Cannot normalize zero vector";
        }
        const double inv_len = 1.0 / std::sqrt(len_sq);
        return vec3(x * inv_len, y * inv_len, z * inv_len);
    }

    // 生成正交向量（用于退化情况）
    vec3 find_orthogonal() const {
        const double abs_x = std::abs(x);
        const double abs_y = std::abs(y);
        const double abs_z = std::abs(z);

        if (abs_z < abs_x && abs_z < abs_y) {
            return vec3(z, 0, -x).normalized();
        }
        else if (abs_y < abs_x) {
            return vec3(0, -z, y).normalized();
        }
        else {
            return vec3(-y, x, 0).normalized();
        }
    }
};

