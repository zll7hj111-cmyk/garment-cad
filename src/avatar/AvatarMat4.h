#pragma once

#include <cmath>

namespace cad::avatar {

/// 4x4 列主序（column-major）矩阵，OpenGL 上传友好。
/// 仅实现 3D 渲染所需的最小集：乘法 / lookAt / perspective。
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // 单位阵

    static Mat4 multiply(const Mat4& a, const Mat4& b)
    {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float sum = 0.f;
                for (int k = 0; k < 4; ++k)
                    sum += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = sum;
            }
        return r;
    }

    /// 视图矩阵：eye 看向 target，up 为近似上方向（右手系，OpenGL -Z 前向）。
    static Mat4 lookAt(float ex, float ey, float ez, float tx, float ty, float tz,
                       float upx, float upy, float upz)
    {
        float fx = tx - ex, fy = ty - ey, fz = tz - ez;
        const float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
        fx /= flen; fy /= flen; fz /= flen; // f = normalize(target - eye)
        float sx = fy * upz - fz * upy, sy = fz * upx - fx * upz, sz = fx * upy - fy * upx;
        const float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
        sx /= slen; sy /= slen; sz /= slen; // s = normalize(f × up)
        const float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;

        Mat4 v;
        v.m[0] = sx;  v.m[4] = sy;  v.m[8] = sz;  v.m[12] = -(sx * ex + sy * ey + sz * ez);
        v.m[1] = ux;  v.m[5] = uy;  v.m[9] = uz;  v.m[13] = -(ux * ex + uy * ey + uz * ez);
        v.m[2] = -fx; v.m[6] = -fy; v.m[10] = -fz; v.m[14] = fx * ex + fy * ey + fz * ez;
        v.m[3] = 0.f; v.m[7] = 0.f; v.m[11] = 0.f; v.m[15] = 1.f;
        return v;
    }

    /// 透视投影：fovY 弧度，aspect = w/h，near/far 裁剪面。
    static Mat4 perspective(float fovY, float aspect, float nearZ, float farZ)
    {
        const float f = 1.f / std::tan(0.5f * fovY);
        const float inv = 1.f / (nearZ - farZ);
        Mat4 p;
        p.m[0] = f / aspect;
        p.m[5] = f;
        p.m[10] = (farZ + nearZ) * inv;
        p.m[11] = -1.f;
        p.m[14] = 2.f * farZ * nearZ * inv;
        p.m[15] = 0.f;
        return p;
    }

    /// 通用 4x4 逆矩阵（列主序）。奇异（det≈0）返回单位阵。
    /// 用于拾取反投影（NDC -> 世界空间）。
    static Mat4 inverse(const Mat4& m)
    {
        float inv[16];
        inv[0] = m.m[5] * m.m[10] * m.m[15] - m.m[5] * m.m[11] * m.m[14] -
                 m.m[9] * m.m[6] * m.m[15] + m.m[9] * m.m[7] * m.m[14] +
                 m.m[13] * m.m[6] * m.m[11] - m.m[13] * m.m[7] * m.m[10];
        inv[4] = -m.m[4] * m.m[10] * m.m[15] + m.m[4] * m.m[11] * m.m[14] +
                 m.m[8] * m.m[6] * m.m[15] - m.m[8] * m.m[7] * m.m[14] -
                 m.m[12] * m.m[6] * m.m[11] + m.m[12] * m.m[7] * m.m[10];
        inv[8] = m.m[4] * m.m[9] * m.m[15] - m.m[4] * m.m[11] * m.m[13] -
                 m.m[8] * m.m[5] * m.m[15] + m.m[8] * m.m[7] * m.m[13] +
                 m.m[12] * m.m[5] * m.m[11] - m.m[12] * m.m[7] * m.m[9];
        inv[12] = -m.m[4] * m.m[9] * m.m[14] + m.m[4] * m.m[10] * m.m[13] +
                  m.m[8] * m.m[5] * m.m[14] - m.m[8] * m.m[6] * m.m[13] -
                  m.m[12] * m.m[5] * m.m[10] + m.m[12] * m.m[6] * m.m[9];
        inv[1] = -m.m[1] * m.m[10] * m.m[15] + m.m[1] * m.m[11] * m.m[14] +
                 m.m[9] * m.m[2] * m.m[15] - m.m[9] * m.m[3] * m.m[14] -
                 m.m[13] * m.m[2] * m.m[11] + m.m[13] * m.m[3] * m.m[10];
        inv[5] = m.m[0] * m.m[10] * m.m[15] - m.m[0] * m.m[11] * m.m[14] -
                 m.m[8] * m.m[2] * m.m[15] + m.m[8] * m.m[3] * m.m[14] +
                 m.m[12] * m.m[2] * m.m[11] - m.m[12] * m.m[3] * m.m[10];
        inv[9] = -m.m[0] * m.m[9] * m.m[15] + m.m[0] * m.m[11] * m.m[13] +
                 m.m[8] * m.m[1] * m.m[15] - m.m[8] * m.m[3] * m.m[13] -
                 m.m[12] * m.m[1] * m.m[11] + m.m[12] * m.m[3] * m.m[9];
        inv[13] = m.m[0] * m.m[9] * m.m[14] - m.m[0] * m.m[10] * m.m[13] -
                  m.m[8] * m.m[1] * m.m[14] + m.m[8] * m.m[2] * m.m[13] +
                  m.m[12] * m.m[1] * m.m[10] - m.m[12] * m.m[2] * m.m[9];
        inv[2] = m.m[1] * m.m[6] * m.m[15] - m.m[1] * m.m[7] * m.m[14] -
                 m.m[5] * m.m[2] * m.m[15] + m.m[5] * m.m[3] * m.m[14] +
                 m.m[13] * m.m[2] * m.m[7] - m.m[13] * m.m[3] * m.m[6];
        inv[6] = -m.m[0] * m.m[6] * m.m[15] + m.m[0] * m.m[7] * m.m[14] +
                 m.m[4] * m.m[2] * m.m[15] - m.m[4] * m.m[3] * m.m[14] -
                 m.m[12] * m.m[2] * m.m[7] + m.m[12] * m.m[3] * m.m[6];
        inv[10] = m.m[0] * m.m[5] * m.m[15] - m.m[0] * m.m[7] * m.m[13] -
                  m.m[4] * m.m[1] * m.m[15] + m.m[4] * m.m[3] * m.m[13] +
                  m.m[12] * m.m[1] * m.m[7] - m.m[12] * m.m[3] * m.m[5];
        inv[14] = -m.m[0] * m.m[5] * m.m[14] + m.m[0] * m.m[6] * m.m[13] +
                  m.m[4] * m.m[1] * m.m[14] - m.m[4] * m.m[2] * m.m[13] -
                  m.m[12] * m.m[1] * m.m[6] + m.m[12] * m.m[2] * m.m[5];
        inv[3] = -m.m[1] * m.m[6] * m.m[11] + m.m[1] * m.m[7] * m.m[10] +
                 m.m[5] * m.m[2] * m.m[11] - m.m[5] * m.m[3] * m.m[10] -
                 m.m[9] * m.m[2] * m.m[7] + m.m[9] * m.m[3] * m.m[6];
        inv[7] = m.m[0] * m.m[6] * m.m[11] - m.m[0] * m.m[7] * m.m[10] -
                 m.m[4] * m.m[2] * m.m[11] + m.m[4] * m.m[3] * m.m[10] +
                 m.m[8] * m.m[2] * m.m[7] - m.m[8] * m.m[3] * m.m[6];
        inv[11] = -m.m[0] * m.m[5] * m.m[11] + m.m[0] * m.m[7] * m.m[9] +
                  m.m[4] * m.m[1] * m.m[11] - m.m[4] * m.m[3] * m.m[9] -
                  m.m[8] * m.m[1] * m.m[7] + m.m[8] * m.m[3] * m.m[5];
        inv[15] = m.m[0] * m.m[5] * m.m[10] - m.m[0] * m.m[6] * m.m[9] -
                  m.m[4] * m.m[1] * m.m[10] + m.m[4] * m.m[2] * m.m[9] +
                  m.m[8] * m.m[1] * m.m[6] - m.m[8] * m.m[2] * m.m[5];

        const float det = m.m[0] * inv[0] + m.m[1] * inv[4] + m.m[2] * inv[8] +
                          m.m[3] * inv[12];
        if (std::fabs(det) < 1e-12f)
            return Mat4();
        const float rdet = 1.f / det;
        Mat4 r;
        for (int i = 0; i < 16; ++i)
            r.m[i] = inv[i] * rdet;
        return r;
    }
};

} // namespace cad::avatar
