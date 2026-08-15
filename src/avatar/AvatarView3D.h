#pragma once

#include "AvatarMat4.h"
#include "AvatarRenderMesh.h"
#include "MeasureSystem.h"

#include <QColor>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class QCheckBox;
class QEvent;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace cad::avatar {

    /// 预设视图方位（MH 风格一键切换）。
enum class ViewPreset {
    Front,        ///< 正面（面向 +Z）
    Back,         ///< 背面
    Left,         ///< 模特左侧
    Right,        ///< 模特右侧
    Top,          ///< 顶部俯视
    Bottom,       ///< 底部仰视
    ThreeQuarter, ///< 默认 3/4 视角（yaw=32, pitch=14）
};

/// 标注点：光标射线与网格表面的精确交点的命名标记（三角原型基准点标定用）。
/// 贴皮肤绑定：记录命中三角形 tri + 重心坐标 (bcU, bcV)，体型 morph 变形时
/// 按重心插值跟随体表移动；pos 为当前世界坐标（随绑定刷新）。
/// 标注连线：两个标注点索引之间的一条线（画线工具）。
struct AnnotationLine {
    int a = -1; ///< 起点标注索引
    int b = -1; ///< 终点标注索引
};

struct AnnotationPoint {
    Vec3 pos;              ///< 当前表面交点（世界坐标，随绑定刷新）
    int vertex = -1;       ///< 最近顶点索引（对齐/参考，0-based）
    int tri = -1;          ///< 绑定三角形索引（m_mesh.faces 下标；-1 = 无绑定，仅顶点参考）
    double bcU = 0.0;      ///< 重心坐标 u（三角形 f[0] 权重）
    double bcV = 0.0;      ///< 重心坐标 v（三角形 f[1] 权重），w = 1-u-v
    std::string name;      ///< 名称（如 BP / SNP / SP）
    std::string heightKey;///< 借用水平面的测量线 key（非空 = 高度约束：Y 跟随该线高度，X/Z 随贴肤 tri/bc）

    /// 是否有有效贴皮肤绑定。
    bool bound() const { return tri >= 0 && !name.empty(); }
};

/// 3D 试衣视口：网格渲染 + 轨道相机。
/// - 左键拖动：环绕旋转（yaw/pitch）；标注模式下左键点击放置标注点、拖住已有标注点可调整位置
/// - 右键拖动：环绕旋转（标注模式下改用右键旋转，左键被放点占用）
/// - 中键拖动：平移观察目标（抓住场景式 pan）
/// - 滚轮：缩放距离
/// - 标注模式下鼠标悬停会在模型表面投影圆圈，指示吸附顶点（消除视差）
/// - 无可用 OpenGL 时自动降级：绘制提示文字而非崩溃（参考 CanvasView 软渲染策略）。
/// 用法：setMesh() 提供最新网格（引擎应用 morph 后调用），内部重建渲染数据并上传。
class AvatarView3D : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit AvatarView3D(QWidget* parent = nullptr);
    ~AvatarView3D() override;

    /// 更新渲染网格（内部拷贝 Mesh3D 并重建 VBO/EBO，随后请求重绘）。
    void setMesh(const Mesh3D& mesh);

    /// 最近一次 setMesh 的网格（测试/导出用）。
    const Mesh3D& mesh() const { return m_mesh; }

    /// 是否已成功初始化 GL（false = 软渲染降级中）。
    bool glReady() const { return m_glReady; }

    /// 相机对准：恢复默认视角（朝向包围盒中心）。
    void resetView();

    /// 一键切换到预设视图方位（MH 风格快捷视角）。
    void setViewPreset(ViewPreset preset);

    /// 当前方位角（度，测试/调试用）。
    double yawDeg() const { return m_yawDeg; }
    double pitchDeg() const { return m_pitchDeg; }
    double dist() const { return m_dist; }              ///< 观察距离（测试/调试用）
    const Vec3& target() const { return m_target; }     ///< 观察目标点（测试/调试用）

    /// 设置测量链（key -> 顶点索引折线），用于在网格表面叠加测量线。默认全部可见。
    /// @param names 可选的 key -> 显示名 映射（缺省时用 key 本身）。
    void setMeasureChains(const std::map<std::string, std::vector<int>>& chains,
                          const std::map<std::string, std::string>& names = {},
                          const std::map<std::string, std::vector<Vec3>>& pointChains = {},
                          const std::map<std::string, cad::avatar::SectionChain>& sectionChains = {},
                          const std::map<std::string, cad::avatar::StraightChain>& straightChains = {});

    /// 切换某条测量线的可见性（无此 key 忽略）。
    void setMeasureVisible(const std::string& key, bool visible);

    /// 全部测量链 key（字典序稳定）。
    std::vector<std::string> measureKeys() const;

    /// 一键设置全部测量线可见性（全开/全关）。
    void setAllMeasureVisible(bool visible);

    /// 测量值标签（名称 + cm 文字）显示开关。
    void setShowMeasureLabels(bool show);
    bool showMeasureLabels() const { return m_showMeasureLabels; }

    /// 测量计算延迟开关：拖动体型滑杆时置 true（跳过测量线重算与测地线，保持旧值），
    /// 松开后置 false 并强制重算最终值。用于拖动去卡顿。
    void setDeferMeasure(bool defer);

    /// 标注模式开关：开启后左键点击放置标注点（而非环绕旋转）。
    void setAnnotationMode(bool on);
    bool annotationMode() const { return m_annotationMode; }

    /// 设置标注点列表（替换全部，随后重绘）。
    void setAnnotations(const std::vector<AnnotationPoint>& points);
    /// 当前标注点列表。
    const std::vector<AnnotationPoint>& annotations() const { return m_annotations; }

    /// 画线模式开关：开启后依次点两个标注点连一条线（一次一条）。
    void setLineMode(bool on);
    bool lineMode() const { return m_lineMode; }
    /// 设置连线列表（替换全部）。
    void setAnnotationLines(const std::vector<AnnotationLine>& lines);
    /// 当前连线列表。
    const std::vector<AnnotationLine>& annotationLines() const { return m_annotationLines; }

    /// 用当前网格顶点，按贴皮肤绑定（tri + 重心坐标）刷新所有标注点位置。
    /// 体型 morph 变形后调用，让标注点跟随体表移动。
    void refreshBoundAnnotations();

    /// 屏幕坐标拾取：发射射线与网格求交，返回最近顶点索引（无命中返回 -1）。
    /// 纯 CPU 计算，不依赖 GL 状态（测试可用）。顶点用于对齐/参考。
    int pickVertex(const QPoint& screenPos) const;

    /// 屏幕坐标拾取：发射射线与网格求交，返回精确表面交点（世界坐标）。
    /// outVertex 可选输出最近顶点索引；outTri 可选输出命中三角形索引（faces 下标）；
    /// outU 可选输出命中三角形内的重心坐标 u（outV 同），第三分量 = 1-u-v。
    /// 重心坐标 + 三角形索引用于"贴皮肤绑定"（标注点随体型 morph 变形跟随）。
    /// 无命中返回 false。
    bool pickSurfacePoint(const QPoint& screenPos, Vec3& outPoint, int* outVertex = nullptr,
                          int* outTri = nullptr, double* outU = nullptr,
                          double* outV = nullptr) const;

signals:
    /// GL 初始化结果（面板可据此提示降级原因）。
    void glStatusChanged(bool ready);

    /// 标注模式下拾取到表面交点（面板据此询问名称并加入标注列表）。
    /// 附带绑定：tri=命中三角形索引，u/v=三角形内重心坐标（贴皮肤跟随变形）。
    /// Ctrl 借用水平面时 heightKey 非空（Y 跟随该测量线高度，X/Z 随贴肤绑定）；
    /// snapName 为命中的测量线显示名（空 = 未命中，面板据此提示反馈）。
    void annotationPicked(const Vec3& pos, int vertexIdx, int tri, double u, double v,
                          const std::string& heightKey, const QString& snapName);

    /// 标注点被拖动调整到新表面交点（面板据此刷新列表，index 为标注点序号）。
    void annotationMoved(int index, const Vec3& pos, int vertexIdx);

    /// 画线模式下完成一条连线（起点/终点标注索引）。
    void annotationLineDrawn(int a, int b);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void compileShaders();
    void uploadMesh();
    void loadMatcapTexture(); // 加载 MH 皮肤 litsphere 贴图（失败则退纯色模式）
    void renderFallback(); // 无 GL 时的提示文字
    void setupViewBar();   // 构建预设视图快捷按钮条
    void setupMeasureBar();                       // 构建测量线勾选悬浮条
    void compileGroundShader();                 // 地面网格 shader（独立小管线）
    void uploadGround(double groundY, double radius); // 重建地面四边形
    void compileLineShader();                      // 测量线 shader（独立小管线）
    void compileAnnotationShader();                // 标注点 GL_POINTS shader（独立小管线）
    void uploadAnnotationPoints();                 // 上传标注点顶点到 VBO（随列表变化）
    void uploadAnnotationLines();                  // 上传标注连线顶点到 VBO（随列表变化）
    void rebuildMeasureLines();                    // 重建测量线几何 + cm 值
    void uploadMeasureLines();                     // 上传测量线 VBO
    /// 截面链的当前截面高度（固定 t 平均 + 锚点各半，随 morph 平滑移动）。
    double sectionHeight(const SectionChain& sc) const;
    /// 某测量线的代表水平高度：截面链=动态截面高度；顶点链=顶点平均 y；
    /// 坐标链=点平均 y；直线链=两端点平均 y。用于「借用水平面」。
    double measureLineHeight(const std::string& key) const;
    /// 找离 p 高度最近、且当前可见的测量线，返回其 key 与代表高度。
    /// 无可见测量线返回 false。
    bool findNearestMeasurePlane(const Vec3& p, std::string& outKey, double& outH) const;
    void refreshMeasureLabels();                   // 回写勾选条 cm 文本
    std::string displayName(const std::string& key) const; // 显示名（无映射回退 key）
    void computeViewProjection(Mat4& view, Mat4& proj, Mat4& mvp) const; // 轨道参数 -> 矩阵（渲染/拾取共用）

    std::unique_ptr<QOpenGLFunctions_3_3_Core> m_gl;
    Mesh3D m_mesh;               ///< 最近一次 setMesh 的网格
    bool m_meshDirty = true;     ///< 需重新上传
    bool m_glReady = false;      ///< shader 编译 + VAO 就绪
    bool m_hasMatcap = false;    ///< 皮肤 litsphere 贴图加载成功

    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLuint m_program = 0;
    GLuint m_matcapTex = 0;
    GLint m_uMvp = -1, m_uNormal = -1, m_uView = -1;
    GLint m_uLightDir = -1, m_uColor = -1, m_uAmbient = -1;
    GLint m_uShininess = -1, m_uUseMatcap = -1, m_uMatcap = -1;
    int m_indexCount = 0;

    // 地面：网格线 + 脚底接触阴影 + 边缘渐隐（模型下方）
    GLuint m_groundProgram = 0;
    GLuint m_groundVao = 0, m_groundVbo = 0;
    GLint m_groundU_mvp = -1, m_groundU_extent = -1, m_groundU_center = -1;
    float m_groundY = 0.f;       ///< 地面高度（包围盒 minY）
    float m_groundExtent = 60.f; ///< 地面半边长（0.1m 单位）

    // 测量线：链顶点沿法线外移的线段覆盖（勾选式 + cm 标签）
    struct MeasureLineInfo {
        Vec3 labelPos;   ///< 标签锚点（世界空间）
        Vec3 normal{0,1,0}; ///< 标签处表面法线（用于朝向相机判定，决定标签显隐）
        double cm = 0.0; ///< 当前链长（cm）
        QColor color;    ///< 该链显示颜色
    };
    std::map<std::string, std::vector<int>> m_measureChains; ///< key -> 顶点索引折线
    std::map<std::string, std::vector<Vec3>> m_measurePointChains; ///< key -> 精确坐标折线（手工标定关键点）
    std::map<std::string, cad::avatar::SectionChain> m_measureSectionChains; ///< key -> 水平截面环（边插值）
    std::map<std::string, cad::avatar::StraightChain> m_measureStraightChains; ///< key -> 直线距离链（中线）
    std::map<std::string, std::string> m_measureNames;       ///< key -> 显示名（可选）
    std::set<std::string> m_measureVisible;                  ///< 勾选可见的 key
    std::map<std::string, MeasureLineInfo> m_measureInfo;    ///< 标签/颜色/长度缓存（仅可见链）
    std::map<std::string, double> m_measureCm;               ///< 全部链 cm 值（含不可见，勾选条显示用）
    std::vector<float> m_lineVerts;   ///< 合并线段顶点（xyz+rgb，6 float/顶点）
    int m_lineVertexCount = 0;        ///< 线段顶点总数（GL_LINES）
    std::vector<Vec3> m_normals;      ///< 顶点法线缓存（m_mesh 无 normals 时计算）
    GeodesicSolver m_geodes;          ///< 测地求解器（缓存拓扑，避免每帧重建邻接表）
    bool m_measureDirty = false;      ///< 测量线几何需重建

    GLuint m_lineProgram = 0;
    GLuint m_lineVao = 0, m_lineVbo = 0;
    GLint m_lineU_mvp = -1;

    // 标注点圆点：GL_POINTS 真 3D（深度测试自然遮挡，转背面自动隐藏）。
    GLuint m_annProgram = 0;
    GLuint m_annVao = 0, m_annVbo = 0;
    GLuint m_annLineVao = 0, m_annLineVbo = 0; ///< 标注连线 VBO（GL_LINES）
    int m_annLineVertexCount = 0;              ///< 连线顶点数
    GLint m_annU_mvp = -1;
    GLint m_annU_scale = -1;   ///< gl_PointSize 随屏幕缩放
    int m_annPointCount = 0;   ///< 标注点顶点数（动态，随标注列表变化）

    QScrollArea* m_measureBar = nullptr;  ///< 测量线勾选悬浮条（右上角）
    QVBoxLayout* m_measureHostLayout = nullptr;
    std::map<std::string, QCheckBox*> m_measureBoxes;
    QToolButton* m_measureAllBtn = nullptr;  ///< 全开按钮
    QToolButton* m_measureNoneBtn = nullptr; ///< 全关按钮
    QToolButton* m_measureLabelBtn = nullptr; ///< 名称显示开关按钮
    bool m_showMeasureLabels = true;          ///< 是否绘制测量值标签（名称+cm）
    bool m_deferMeasure = false;               ///< 测量计算延迟（拖动体型时 true）

    // 标注点：光标射线与网格表面精确交点的命名标记
    std::vector<AnnotationPoint> m_annotations; ///< 标注点列表
    bool m_annotationMode = false;              ///< 标注模式（左键点击拾取放点）
    Vec3 m_hoverPos{0, 0, 0};                   ///< 悬停表面交点（标注模式下投影圆圈）
    bool m_hoverValid = false;                  ///< 悬停是否命中模型
    int m_dragAnnotation = -1;                  ///< 正在拖动的标注索引（-1 = 无）
    bool m_snapPreviewValid = false;            ///< Ctrl 悬停吸附预览：命中测量线时 true
    Vec3 m_snapPreviewPos{0, 0, 0};             ///< 吸附预览点（世界坐标）
    std::string m_snapPreviewKey;               ///< 吸附预览命中的测量线 key（显示名用）

    // 标注连线：画线模式下点两个标注点连一条线（一次一条）
    std::vector<AnnotationLine> m_annotationLines; ///< 连线列表（标注点索引对）
    bool m_lineMode = false;                    ///< 画线模式（点两点连线）
    int m_lineStartIdx = -1;                    ///< 画线起点标注索引（-1 = 未选）

    // 轨道相机
    double m_yawDeg = 32.0;      ///< 环绕方位角（度）
    double m_pitchDeg = 14.0;    ///< 俯仰角（度）
    double m_dist = 30.0;        ///< 观察距离（0.1m 单位）
    Vec3 m_target{0, 0, 0};      ///< 观察目标点

    bool m_dragging = false;     ///< 左键/右键环绕中
    bool m_panning = false;      ///< 中键平移中
    QPoint m_lastPos;

    QWidget* m_viewBar = nullptr; ///< 预设视图快捷按钮条（悬浮于视口左上角）
};

} // namespace cad::avatar
