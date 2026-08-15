#pragma once

#include <cmath>

namespace cad::avatar {

/// 3D vector / point, header-only, double precision.
/// 单位约定：与 MakeHuman 一致，1 单位 = 0.1 米；测量结果对外换算为厘米（cm）。
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    // --- Arithmetic operators ---
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }

    constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    constexpr Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }

    // --- Comparison ---
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }

    // --- Products ---
    constexpr double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }

    // --- Length / Distance ---
    constexpr double lengthSquared() const { return x * x + y * y + z * z; }
    double length() const { return std::sqrt(lengthSquared()); }

    double distanceTo(const Vec3& o) const { return (*this - o).length(); }

    // --- Normalization ---
    Vec3 normalized() const {
        double len = length();
        if (len < 1e-12) return {0.0, 0.0, 0.0};
        return *this / len;
    }

    // --- Constants ---
    static constexpr Vec3 zero() { return {0.0, 0.0, 0.0}; }
};

/// 2D vector（纹理坐标用），header-only。
struct Vec2 {
    double u = 0.0;
    double v = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double u_, double v_) : u(u_), v(v_) {}
};

// --- Free operators ---
constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }

} // namespace cad::avatar
