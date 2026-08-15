#include <QtTest/QtTest>

#include "avatar/AvatarMat4.h"
#include "avatar/AvatarView3D.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QSurfaceFormat>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace cad::avatar;

namespace {

std::string assetsDir() {
    const char* env = std::getenv("GCAD_ASSETS_DIR");
    if (env && *env) return env;
    return AVATAR_ASSETS_DIR;
}

} // namespace

class TestAvatarView : public QObject {
    Q_OBJECT

private slots:
    void mat4Multiply();
    void mat4LookAtKnownFrame();
    void mat4PerspectiveFrustum();
    void mat4InverseRoundTrip();
    void viewConstructsAndHoldsMesh();
    void viewPresetSwitchesCamera();
    void viewPresetBarExists();
    void pickVertexHitsFrontSurface();
    void pickVertexRoundTrip();
    void pickSurfacePointMatchesCursor();
    void shoulderPickDiagnostic();
    void neckSilhouettePick();
    void glSmokeIfAvailable();

private:
    Mesh3D loadBase() const { return loadObjFile(assetsDir() + "/base.obj"); }
};

void TestAvatarView::mat4Multiply()
{
    // 已知数值：纯平移 × 纯缩放（沿轴缩放 2）
    Mat4 a; // 单位阵
    a.m[12] = 3.f; a.m[13] = 4.f; a.m[14] = 5.f; // 平移 (3,4,5)
    Mat4 s; // 单位阵
    s.m[0] = 2.f; s.m[5] = 2.f; s.m[10] = 2.f;   // 缩放 2
    const Mat4 r = Mat4::multiply(a, s);
    QVERIFY(std::fabs(r.m[0] - 2.f) < 1e-6f);
    QVERIFY(std::fabs(r.m[5] - 2.f) < 1e-6f);
    QVERIFY(std::fabs(r.m[10] - 2.f) < 1e-6f);
    QVERIFY(std::fabs(r.m[12] - 3.f) < 1e-6f); // 平移保留
    QVERIFY(std::fabs(r.m[14] - 5.f) < 1e-6f);
    QVERIFY(std::fabs(r.m[3]) < 1e-6f);        // 底行保持 0 0 0 1
    QVERIFY(std::fabs(r.m[15] - 1.f) < 1e-6f);
}

void TestAvatarView::mat4LookAtKnownFrame()
{
    // 相机在 (0,0,10) 看向原点：视图空间 Y 轴不变，Z 轴反转
    const Mat4 v = Mat4::lookAt(0, 0, 10, 0, 0, 0, 0, 1, 0);
    QVERIFY(std::fabs(v.m[0] - 1.f) < 1e-5f);   // 右 = +X
    QVERIFY(std::fabs(v.m[5] - 1.f) < 1e-5f);   // 上 = +Y
    QVERIFY(std::fabs(v.m[10] - 1.f) < 1e-5f);  // 前 = -Z（看向原点）
    QVERIFY(std::fabs(v.m[14] + 10.f) < 1e-5f); // 平移 -10
}

void TestAvatarView::mat4PerspectiveFrustum()
{
    const Mat4 p = Mat4::perspective(0.785f, 2.0f, 1.0f, 10.0f);
    QVERIFY(std::fabs(p.m[0] - p.m[5] / 2.0f) < 1e-5f); // aspect 生效：f/aspect
    QVERIFY(p.m[11] == -1.f);                            // 透视除法项
    QVERIFY(p.m[10] < 0.f);                              // 深度压缩方向
}

void TestAvatarView::mat4InverseRoundTrip()
{
    // 视图 × 投影的合成矩阵，乘其逆应回到单位阵
    const Mat4 v = Mat4::lookAt(0, 5, 30, 0, 0, 0, 0, 1, 0);
    const Mat4 p = Mat4::perspective(0.785f, 1.5f, 0.3f, 300.f);
    const Mat4 m = Mat4::multiply(p, v);
    const Mat4 inv = Mat4::inverse(m);
    const Mat4 ident = Mat4::multiply(m, inv);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            const float expect = (r == c) ? 1.f : 0.f;
            QVERIFY2(std::fabs(ident.m[c * 4 + r] - expect) < 1e-3f,
                     "m * inverse(m) should be identity");
        }
}

void TestAvatarView::pickVertexHitsFrontSurface()
{
    AvatarView3D view;
    view.setMesh(loadBase());
    view.resetView();
    view.resize(480, 360);
    view.setViewPreset(ViewPreset::Front); // 相机在 +Z 看向包围盒中心

    // 屏幕中心射线必穿过模型；命中顶点应为有效索引且位于前表面（脸朝 +Z）
    const int v = view.pickVertex(QPoint(240, 180));
    const int vc = view.mesh().vertexCount();
    QVERIFY2(v >= 0 && v < vc, "pickVertex should hit the model at screen center");
    const Vec3 p = view.mesh().verts[static_cast<size_t>(v)];
    QVERIFY2(p.z > 0.3, "hit vertex should be on the front surface (+Z)");
}

void TestAvatarView::pickVertexRoundTrip()
{
    // 往返一致性：把正面顶点投影到屏幕 -> 拾取 -> 返回顶点投影应贴近原屏幕点。
    // 用于排查"标注漂移"（吸附顶点远离光标）。
    AvatarView3D view;
    view.setMesh(loadBase());
    view.resetView();
    view.resize(480, 360);
    view.setViewPreset(ViewPreset::Front);

    const auto& mesh = view.mesh();
    const double yaw = view.yawDeg() * M_PI / 180.0;
    const double pitch = view.pitchDeg() * M_PI / 180.0;
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const Vec3 t = view.target();
    const double dist = view.dist();
    const Vec3 eye = t + Vec3(dist * cp * sy, dist * sp, dist * cp * cy);
    const Mat4 vmat = Mat4::lookAt(static_cast<float>(eye.x), static_cast<float>(eye.y),
                                   static_cast<float>(eye.z), static_cast<float>(t.x),
                                   static_cast<float>(t.y), static_cast<float>(t.z),
                                   0.f, 1.f, 0.f);
    const double aspect = 480.0 / 360.0;
    const Mat4 pmat = Mat4::perspective(static_cast<float>(45.0 * M_PI / 180.0),
                                        static_cast<float>(aspect),
                                        static_cast<float>(dist * 0.01),
                                        static_cast<float>(std::max(dist * 100.0, 500.0)));
    const Mat4 mvp = Mat4::multiply(pmat, vmat);

    const auto project = [&](const Vec3& pt) -> QPointF {
        const double px = pt.x, py = pt.y, pz = pt.z;
        const double cw = mvp.m[3] * px + mvp.m[7] * py + mvp.m[11] * pz + mvp.m[15];
        if (cw <= 1e-6)
            return QPointF(-1e9, -1e9);
        const double cx = mvp.m[0] * px + mvp.m[4] * py + mvp.m[8] * pz + mvp.m[12];
        const double cyy = mvp.m[1] * px + mvp.m[5] * py + mvp.m[9] * pz + mvp.m[13];
        const double nx = cx / cw, ny = cyy / cw;
        return QPointF((nx * 0.5 + 0.5) * 480.0, (1.0 - (ny * 0.5 + 0.5)) * 360.0);
    };

    int tested = 0;
    double worst = 0.0;
    for (int vi = 0; vi < static_cast<int>(mesh.verts.size()) && tested < 500; ++vi) {
        const Vec3& pt = mesh.verts[static_cast<size_t>(vi)];
        if (pt.z <= 0.3)
            continue;
        const QPointF sp = project(pt);
        if (sp.x() < 0.0 || sp.x() >= 480.0 || sp.y() < 0.0 || sp.y() >= 360.0)
            continue;
        const int got = view.pickVertex(QPoint(static_cast<int>(sp.x()),
                                              static_cast<int>(sp.y())));
        QVERIFY2(got >= 0, "pickVertex should hit at a projected front vertex");
        const QPointF sp2 = project(mesh.verts[static_cast<size_t>(got)]);
        const double d = std::hypot(sp2.x() - sp.x(), sp2.y() - sp.y());
        worst = std::max(worst, d);
        ++tested;
    }
    QVERIFY2(tested > 50, "should sample enough front vertices");
    QVERIFY2(worst < 30.0, qPrintable(QStringLiteral("pickVertex roundtrip drift too large: %1 px")
                                          .arg(worst)));
}

void TestAvatarView::pickSurfacePointMatchesCursor()
{
    // 表面交点一致性：投影一个正面三角形内部点，拾取得到的精确交点应投影回原屏幕点附近。
    // 用于排查"标注漂移"——标注点应落在光标射线上，而非吸附到稀疏顶点。
    AvatarView3D view;
    view.setMesh(loadBase());
    view.resetView();
    view.resize(480, 360);
    view.setViewPreset(ViewPreset::Front);

    const auto& mesh = view.mesh();
    const double yaw = view.yawDeg() * M_PI / 180.0;
    const double pitch = view.pitchDeg() * M_PI / 180.0;
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const Vec3 t = view.target();
    const double dist = view.dist();
    const Vec3 eye = t + Vec3(dist * cp * sy, dist * sp, dist * cp * cy);
    const Mat4 vmat = Mat4::lookAt(static_cast<float>(eye.x), static_cast<float>(eye.y),
                                   static_cast<float>(eye.z), static_cast<float>(t.x),
                                   static_cast<float>(t.y), static_cast<float>(t.z),
                                   0.f, 1.f, 0.f);
    const double aspect = 480.0 / 360.0;
    const Mat4 pmat = Mat4::perspective(static_cast<float>(45.0 * M_PI / 180.0),
                                        static_cast<float>(aspect),
                                        static_cast<float>(dist * 0.01),
                                        static_cast<float>(std::max(dist * 100.0, 500.0)));
    const Mat4 mvp = Mat4::multiply(pmat, vmat);

    const auto project = [&](const Vec3& pt) -> QPointF {
        const double px = pt.x, py = pt.y, pz = pt.z;
        const double cw = mvp.m[3] * px + mvp.m[7] * py + mvp.m[11] * pz + mvp.m[15];
        if (cw <= 1e-6)
            return QPointF(-1e9, -1e9);
        const double cx = mvp.m[0] * px + mvp.m[4] * py + mvp.m[8] * pz + mvp.m[12];
        const double cyy = mvp.m[1] * px + mvp.m[5] * py + mvp.m[9] * pz + mvp.m[13];
        const double nx = cx / cw, ny = cyy / cw;
        return QPointF((nx * 0.5 + 0.5) * 480.0, (1.0 - (ny * 0.5 + 0.5)) * 360.0);
    };

    // 取正面三角形的重心（必然落在表面内部，非顶点），验证拾取点投影回原位
    int tested = 0;
    double worst = 0.0;
    for (const auto& f : mesh.faces) {
        if (tested >= 300)
            break;
        const Vec3 centroid = (mesh.verts[f[0]] + mesh.verts[f[1]] + mesh.verts[f[2]]) / 3.0;
        if (centroid.z <= 0.3)
            continue;
        const QPointF sp = project(centroid);
        if (sp.x() < 0.0 || sp.x() >= 480.0 || sp.y() < 0.0 || sp.y() >= 360.0)
            continue;
        Vec3 got;
        QVERIFY2(view.pickSurfacePoint(QPoint(static_cast<int>(sp.x()),
                                              static_cast<int>(sp.y())), got),
                 "pickSurfacePoint should hit at a projected front centroid");
        const QPointF sp2 = project(got);
        const double d = std::hypot(sp2.x() - sp.x(), sp2.y() - sp.y());
        worst = std::max(worst, d);
        ++tested;
    }
    QVERIFY2(tested > 20, "should sample enough front centroids");
    QVERIFY2(worst < 3.0, qPrintable(QStringLiteral("surface point drift too large: %1 px")
                                          .arg(worst)));
}

void TestAvatarView::shoulderPickDiagnostic()
{
    // 回归：肩胛被 helper-tights（紧身衣）壳包裹，紧身衣在渲染时被白名单隐藏，
    // 但拾取若不过滤会命中紧身衣壳导致标注点"浮空"。修复后拾取与渲染共享
    // isRenderableGroup 白名单，应命中 body 正面前表面而非隐藏壳/后表面。
    AvatarView3D view;
    view.setMesh(loadBase());
    view.resetView();
    view.resize(480, 360);
    view.setViewPreset(ViewPreset::ThreeQuarter);

    const auto& mesh = view.mesh();
    const double yaw = view.yawDeg() * M_PI / 180.0;
    const double pitch = view.pitchDeg() * M_PI / 180.0;
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const Vec3 t = view.target();
    const double dist = view.dist();
    const Vec3 eye = t + Vec3(dist * cp * sy, dist * sp, dist * cp * cy);
    const Mat4 vmat = Mat4::lookAt(static_cast<float>(eye.x), static_cast<float>(eye.y),
                                   static_cast<float>(eye.z), static_cast<float>(t.x),
                                   static_cast<float>(t.y), static_cast<float>(t.z),
                                   0.f, 1.f, 0.f);
    const double aspect = 480.0 / 360.0;
    const Mat4 pmat = Mat4::perspective(static_cast<float>(45.0 * M_PI / 180.0),
                                        static_cast<float>(aspect),
                                        static_cast<float>(dist * 0.01),
                                        static_cast<float>(std::max(dist * 100.0, 500.0)));
    const Mat4 mvp = Mat4::multiply(pmat, vmat);
    const auto project = [&](const Vec3& pt) -> QPointF {
        const double cw = mvp.m[3] * pt.x + mvp.m[7] * pt.y + mvp.m[11] * pt.z + mvp.m[15];
        if (cw <= 1e-6) return QPointF(-1e9, -1e9);
        const double cx = mvp.m[0] * pt.x + mvp.m[4] * pt.y + mvp.m[8] * pt.z + mvp.m[12];
        const double cyy = mvp.m[1] * pt.x + mvp.m[5] * pt.y + mvp.m[9] * pt.z + mvp.m[13];
        return QPointF((cx / cw * 0.5 + 0.5) * 480.0, (1.0 - (cyy / cw * 0.5 + 0.5)) * 360.0);
    };

    // 取肩胛正面顶点（z>0，在 3/4 视角可见），拾取应命中它附近而非远端后表面
    double worst = 0.0;
    int tested = 0;
    for (size_t i = 0; i < mesh.verts.size(); ++i) {
        const Vec3& v = mesh.verts[i];
        if (v.y < 5.2 || v.y > 5.7 || v.x < 1.3 || v.x > 2.4 || v.z < 0.2)
            continue;
        const QPointF sp = project(v);
        if (sp.x() < 0.0 || sp.x() >= 480.0 || sp.y() < 0.0 || sp.y() >= 360.0)
            continue;
        Vec3 got;
        if (!view.pickSurfacePoint(QPoint(static_cast<int>(sp.x()), static_cast<int>(sp.y())), got))
            continue;
        worst = std::max(worst, got.distanceTo(v));
        ++tested;
    }
    QVERIFY2(tested > 5, "should sample enough visible shoulder-front vertices");
    // 命中 body 正面前表面误差应 <1 单位(10cm)；若命中隐藏紧身衣壳（修复前）
    // 或后表面/远端会 >1 单位(10cm)。阈值放宽到 1.0 以容纳无显示环境下
    // QOpenGLWidget resize 尺寸与 480x360 投影的轻微失配（仍命中前表面）。
    QVERIFY2(worst < 1.0, qPrintable(QStringLiteral("shoulder front pick floats: %1 unit")
                                          .arg(worst)));
}

void TestAvatarView::neckSilhouettePick()
{
    // 回归：侧颈点（颈侧轮廓，法线近似垂直于视线，近掠射角）。
    // 颈侧接近轮廓边缘，若拾取命中隐藏辅助几何或后表面会导致标注点浮空。
    // 修复：拾取与渲染共享 isRenderableGroup 白名单 + det<0 背面剔除，
    // 命中的是 body 正面前表面。
    AvatarView3D view;
    view.setMesh(loadBase());
    view.resetView();
    view.resize(480, 360);
    view.setViewPreset(ViewPreset::Front);

    const auto& mesh = view.mesh();
    const double yaw = view.yawDeg() * M_PI / 180.0;
    const double pitch = view.pitchDeg() * M_PI / 180.0;
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const Vec3 t = view.target();
    const double dist = view.dist();
    const Vec3 eye = t + Vec3(dist * cp * sy, dist * sp, dist * cp * cy);
    const Mat4 vmat = Mat4::lookAt(static_cast<float>(eye.x), static_cast<float>(eye.y),
                                   static_cast<float>(eye.z), static_cast<float>(t.x),
                                   static_cast<float>(t.y), static_cast<float>(t.z),
                                   0.f, 1.f, 0.f);
    const double aspect = 480.0 / 360.0;
    const Mat4 pmat = Mat4::perspective(static_cast<float>(45.0 * M_PI / 180.0),
                                        static_cast<float>(aspect),
                                        static_cast<float>(dist * 0.01),
                                        static_cast<float>(std::max(dist * 100.0, 500.0)));
    const Mat4 mvp = Mat4::multiply(pmat, vmat);
    const auto project = [&](const Vec3& pt) -> QPointF {
        const double cw = mvp.m[3] * pt.x + mvp.m[7] * pt.y + mvp.m[11] * pt.z + mvp.m[15];
        if (cw <= 1e-6) return QPointF(-1e9, -1e9);
        const double cx = mvp.m[0] * pt.x + mvp.m[4] * pt.y + mvp.m[8] * pt.z + mvp.m[12];
        const double cyy = mvp.m[1] * pt.x + mvp.m[5] * pt.y + mvp.m[9] * pt.z + mvp.m[13];
        return QPointF((cx / cw * 0.5 + 0.5) * 480.0, (1.0 - (cyy / cw * 0.5 + 0.5)) * 360.0);
    };

    // 取颈侧轮廓正面顶点（y 在颈高区间，x 在颈宽附近，z>0 为面向相机的侧前表面）
    double worst = 0.0;
    int tested = 0;
    for (size_t i = 0; i < mesh.verts.size(); ++i) {
        const Vec3& v = mesh.verts[i];
        if (v.y < 4.6 || v.y > 5.2 || v.x < -1.3 || v.x > -0.7 || v.z < 0.0)
            continue;
        const QPointF sp = project(v);
        if (sp.x() < 0.0 || sp.x() >= 480.0 || sp.y() < 0.0 || sp.y() >= 360.0)
            continue;
        Vec3 got;
        if (!view.pickSurfacePoint(QPoint(static_cast<int>(sp.x()), static_cast<int>(sp.y())), got))
            continue;
        worst = std::max(worst, got.distanceTo(v));
        ++tested;
    }
    QVERIFY2(tested > 5, "should sample enough visible neck-side front vertices");
    // 颈侧正面顶点拾取误差应 <0.5 单位(5cm)；若命中颈背面/内腔会 >1 单位(10cm)
    QVERIFY2(worst < 0.5, qPrintable(QStringLiteral("neck side pick floats: %1 unit")
                                          .arg(worst)));
}

void TestAvatarView::viewConstructsAndHoldsMesh()
{
    // 无 GL 环境也应能安全构造与接收数据（initializeGL 在 show 后才触发）
    AvatarView3D view;
    view.setMesh(loadBase());
    QVERIFY(!view.glReady()); // 未 show 前必然未初始化
    view.resetView();
    view.resize(480, 360);
}

void TestAvatarView::viewPresetSwitchesCamera()
{
    // 相机约定（paintGL）：eye = target + (dist*cp*sy, dist*sp, dist*cp*cy)
    // Front: yaw=0/pitch=0 -> eye 落在 +Z（模型脸朝 +Z，看到正面）
    AvatarView3D view;
    view.setViewPreset(ViewPreset::Front);
    QCOMPARE(view.yawDeg(), 0.0);
    QCOMPARE(view.pitchDeg(), 0.0);

    view.setViewPreset(ViewPreset::Back);
    QCOMPARE(view.yawDeg(), 180.0);
    QCOMPARE(view.pitchDeg(), 0.0);

    // 模特右手侧 = +X：Right 视角相机在 +X（yaw=90）
    view.setViewPreset(ViewPreset::Right);
    QCOMPARE(view.yawDeg(), 90.0);
    QCOMPARE(view.pitchDeg(), 0.0);
    view.setViewPreset(ViewPreset::Left);
    QCOMPARE(view.yawDeg(), -90.0);
    QCOMPARE(view.pitchDeg(), 0.0);

    // 顶部/底部受 pitch clamp（±89）限制，避免 lookAt 退化
    view.setViewPreset(ViewPreset::Top);
    QCOMPARE(view.yawDeg(), 0.0);
    QCOMPARE(view.pitchDeg(), 89.0);
    view.setViewPreset(ViewPreset::Bottom);
    QCOMPARE(view.pitchDeg(), -89.0);

    // 默认 3/4 视角
    view.setViewPreset(ViewPreset::ThreeQuarter);
    QCOMPARE(view.yawDeg(), 32.0);
    QCOMPARE(view.pitchDeg(), 14.0);
}

void TestAvatarView::viewPresetBarExists()
{
    // 悬浮快捷按钮条：7 个预设视角按钮 + 点击切换生效
    AvatarView3D view;
    const auto buttons = view.findChildren<QToolButton*>(QStringLiteral("viewPresetBtn"));
    QCOMPARE(buttons.size(), 7);

    const auto frontBtn = std::find_if(buttons.begin(), buttons.end(),
                                       [](const QToolButton* b) {
        return b->text() == QStringLiteral("正面");
    });
    QVERIFY(frontBtn != buttons.end());
    (*frontBtn)->click();
    QCOMPARE(view.yawDeg(), 0.0);
    QCOMPARE(view.pitchDeg(), 0.0);
}

void TestAvatarView::glSmokeIfAvailable()
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create())
        QSKIP("no OpenGL 3.3 core context available (software rendering env)");
    QOffscreenSurface surf;
    surf.setFormat(ctx.format());
    surf.create();
    if (!ctx.makeCurrent(&surf))
        QSKIP("cannot make offscreen surface current");
    QOpenGLFunctions_3_3_Core f;
    if (!f.initializeOpenGLFunctions())
        QSKIP("cannot load GL 3.3 functions");

    const char* vsSrc =
        "#version 330 core\n"
        "layout(location=0) in vec3 aPos;\n"
        "uniform mat4 uMvp;\n"
        "void main(){ gl_Position = uMvp * vec4(aPos,1.0); }\n";
    const char* fsSrc =
        "#version 330 core\n"
        "out vec4 c;\n"
        "void main(){ c = vec4(1.0,0.5,0.5,1.0); }\n";
    const GLuint vs = f.glCreateShader(GL_VERTEX_SHADER);
    f.glShaderSource(vs, 1, &vsSrc, nullptr);
    f.glCompileShader(vs);
    GLint ok = GL_FALSE;
    f.glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    QVERIFY2(ok == GL_TRUE, "minimal 330 core vertex shader must compile");
    f.glDeleteShader(vs);
    const GLuint fs = f.glCreateShader(GL_FRAGMENT_SHADER);
    f.glShaderSource(fs, 1, &fsSrc, nullptr);
    f.glCompileShader(fs);
    f.glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    QVERIFY2(ok == GL_TRUE, "minimal 330 core fragment shader must compile");
    f.glDeleteShader(fs);
    ctx.doneCurrent();
}

QTEST_MAIN(TestAvatarView)
#include "test_avatar_view.moc"
