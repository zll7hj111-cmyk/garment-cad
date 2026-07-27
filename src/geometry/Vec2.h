#pragma once

#include <cmath>

namespace cad::geo {

/// 2D vector / point, header-only, constexpr where possible.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}

    // --- Arithmetic operators ---
    constexpr Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }

    constexpr Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
    constexpr Vec2& operator/=(double s) { x /= s; y /= s; return *this; }

    // --- Comparison ---
    constexpr bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }

    // --- Products ---
    constexpr double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    constexpr double cross(const Vec2& o) const { return x * o.y - y * o.x; }

    // --- Length / Distance ---
    constexpr double lengthSquared() const { return x * x + y * y; }
    double length() const { return std::sqrt(lengthSquared()); }

    constexpr double distanceSquaredTo(const Vec2& o) const {
        return (*this - o).lengthSquared();
    }
    double distanceTo(const Vec2& o) const { return (*this - o).length(); }

    // --- Normalization ---
    Vec2 normalized() const {
        double len = length();
        if (len < 1e-12) return {0.0, 0.0};
        return *this / len;
    }

    // --- Perpendicular (rotated 90 degrees CCW) ---
    constexpr Vec2 perpendicular() const { return {-y, x}; }

    // --- Rotation by angle (radians) ---
    Vec2 rotated(double angleRad) const {
        double c = std::cos(angleRad);
        double s = std::sin(angleRad);
        return {x * c - y * s, x * s + y * c};
    }

    // --- Angle ---
    double angle() const { return std::atan2(y, x); }

    // --- Lerp ---
    constexpr Vec2 lerp(const Vec2& o, double t) const {
        return {x + (o.x - x) * t, y + (o.y - y) * t};
    }

    // --- Constants ---
    static constexpr Vec2 zero() { return {0.0, 0.0}; }
    static constexpr Vec2 unitX() { return {1.0, 0.0}; }
    static constexpr Vec2 unitY() { return {0.0, 1.0}; }
};

// --- Free operators ---
constexpr Vec2 operator*(double s, const Vec2& v) { return v * s; }

} // namespace cad::geo
