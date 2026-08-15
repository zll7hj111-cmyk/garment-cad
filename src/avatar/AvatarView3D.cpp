#include "AvatarView3D.h"

#include "MeasureSystem.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>
#include <cstdlib>

namespace cad::avatar {

namespace {

constexpr float kFovDeg = 45.f;
constexpr float kPitchClamp = 89.f;
constexpr double kMeasureOffset = 0.05; ///< 测量线沿法线外移（0.1m 单位 = 0.5cm，深度测试下防 Z-fighting）
constexpr double kSectionOffset = 0.08; ///< 截面环（腰围/下胸围）沿法线外移（0.8cm，腹部凹陷需更大偏移防埋）
constexpr double kAnnotationOffset = 0.015; ///< 标注点圆点沿法线外移（0.1m 单位 = 1.5mm，防 Z-fighting）
constexpr double kMeasureSampleStep = 0.4; ///< 测地线重采样步长（0.1m 单位 = 4cm，让线笔直不贴噪）
constexpr double kAnnotationGrabRadius = 10.0; ///< 拖动已有标注点的抓取半径（px）

const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uMvp;
uniform mat3 uNormal;
uniform mat4 uView;
out vec3 vNormal;
out vec3 vViewPos;
out vec2 vUv;
void main() {
    vNormal = uNormal * aNormal;
    vViewPos = (uView * vec4(aPos, 1.0)).xyz;
    vUv = aUv;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

// 光照模型：时尚展示棚拍方案（参考 CLO3D/Style3D 等服装 CAD 的展示打光：
// 三点布光 + wrap 柔光 + 半球环境反光）。
// - 半球环境光：天空暖白 / 地面反光板中性灰（提高地面端亮度，腋下/下巴阴影不落死黑）
// - 主光 wrap diffuse（Wrapped Lambert，柔化明暗交界，避免硬阴影）
// - 补光对侧弱光（同 wrap，消除死黑）
// - Blinn spec 弱且暖白（皮肤油脂感，非塑料反光）+ 柔和 rim 轮廓光
// 皮肤质感：matcap（MH litsphere 环境光响应查询表，view-space 法线查表，1 次采样），
// 对比度压平，立体感保留但深沟阴影减轻。
const char* kFragmentShader = R"(
#version 330 core
in vec3 vNormal;
in vec3 vViewPos;
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uMatcap;  // MH 皮肤 litsphere（matcap）
uniform vec3 uLightDir;     // 视图空间主光方向
uniform vec3 uColor;        // 纯色兜底色
uniform float uAmbient;
uniform float uShininess;
uniform int uUseMatcap;     // 1 = matcap 皮肤模式

// Wrapped Lambert：把法线与光夹角向背光侧包裹，明暗交界柔和过渡
float wrapDiffuse(vec3 n, vec3 l, float w) {
    return clamp((dot(n, l) + w) / (1.0 + w), 0.0, 1.0);
}

void main() {
    vec3 n = normalize(vNormal);
    vec3 base = uColor;
    if (uUseMatcap == 1) {
        // matcap：view-space 法线 -> 贴图 uv（litsphere 中心 = 法线朝向相机）
        // matcap：litsphere 只提供明暗响应（MH 官方管线：shading 通道 × 肤色），
        // 肤色由 uColor 独立控制，避免 litsphere 棕底色直接污染肤色；
        // 对比度压平（0.55 + 0.9*lum）减轻深沟阴影
        vec3 mc = texture(uMatcap, n.xy * 0.5 + 0.5).rgb;
        float lum = dot(mc, vec3(0.299, 0.587, 0.114));
        base = uColor * clamp(0.55 + 0.9 * lum, 0.0, 1.15);
    }
    // 半球环境光：上（天空暖白）下（地面反光板中性灰，抬高亮度防止阴影死黑）
    float h = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = vec3(0.90, 0.87, 0.84);
    vec3 bounce = vec3(0.46, 0.44, 0.43);
    vec3 ambient = mix(bounce, sky, h);
    // 主光（斜上方，wrap 柔化）+ 补光（对侧前方弱光，wrap 柔化）
    vec3 l = normalize(uLightDir);
    float diff = wrapDiffuse(n, l, 0.4);
    vec3 fillDir = normalize(vec3(-0.45, 0.10, -0.55));
    float diffFill = wrapDiffuse(n, fillDir, 0.4) * 0.55;
    // Blinn-Phong 高光：弱 + 暖白，接近皮肤油脂反光而非塑料
    vec3 viewDir = normalize(-vViewPos);
    vec3 halfDir = normalize(l + viewDir);
    float spec = pow(max(dot(n, halfDir), 0.0), uShininess) * 0.55;
    // rim 边缘光：柔和分离剪影（强度压低，冷白）
    float rim = pow(1.0 - max(dot(n, viewDir), 0.0), 4.0) * 0.18;
    vec3 col = base * (uAmbient * ambient
                       + (1.0 - uAmbient) * (diff * 0.65 + diffFill))
             + vec3(1.0, 0.95, 0.88) * spec * 0.18
             + vec3(0.65, 0.70, 0.78) * rim;
    fragColor = vec4(col, 1.0);
}
)";

// 地面：世界空间网格线（次/主两级）+ 脚底接触阴影圆盘 + 边缘渐隐。
// 颜色贴近背景 clear 色（0.16,0.17,0.20），网格线提亮形成层次。
const char* kGroundVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
out vec3 vWorld;
void main() {
    vWorld = aPos;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kGroundFragmentShader = R"(
#version 330 core
in vec3 vWorld;
out vec4 fragColor;
uniform float uExtent;   // 地面半边长（网格渐隐用）
uniform vec2 uCenterXZ;  // 模型中心 XZ（网格原点跟随模型）
float gridLine(vec2 p, float step) {
    vec2 f = abs(fract(p / step) - 0.5) / fwidth(p);
    return 1.0 - clamp(min(f.x, f.y), 0.0, 1.0);
}
void main() {
    vec2 p = vWorld.xz - uCenterXZ;
    float d = length(p);
    // 1dm 棋盘微差打底，避免纯色呆板
    float checker = mod(floor(p.x) + floor(p.y), 2.0);
    vec3 col = vec3(0.135, 0.150, 0.170) + checker * 0.010;
    // 次网格 1dm、主网格 5dm（MH 0.1m 单位）
    col = mix(col, vec3(0.34, 0.37, 0.41), gridLine(p, 1.0) * 0.30);
    col = mix(col, vec3(0.55, 0.58, 0.63), gridLine(p, 5.0) * 0.75);
    // 脚底接触阴影：中心暗、向外恢复
    float shadow = 1.0 - 0.55 * exp(-d * d / (2.0 * 5.0 * 5.0));
    col *= shadow;
    // 边缘渐隐融入背景
    float fade = 1.0 - smoothstep(uExtent * 0.55, uExtent, d);
    fragColor = vec4(col, fade);
}
)";

// 测量线：纯色线段（每顶点携带颜色），用于在网格表面叠加体围/长度测量线。
const char* kLineVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMvp;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

const char* kLineFragmentShader = R"(
#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

// 标注点：3D GL_POINTS 圆点。位置贴在体表（沿法线略外移），深度测试自然遮挡——
// 转到模型背面时圆点位于模型之后，被深度缓冲自动挡住消失，无需软件遮挡判断。
const char* kAnnVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
uniform float uPointSize;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
)";

const char* kAnnFragmentShader = R"(
#version 330 core
out vec4 fragColor;
void main() {
    // 方形点裁剪成圆盘（中心亮黄点），圆外片元丢弃。gl_PointCoord 为 [0,1] 点内坐标。
    // 圆点减淡一点白色描边感：内圈亮黄，贴外缘黑边由后续名称/对比体现，这里纯色即可。
    vec2 d = gl_PointCoord - vec2(0.5);
    if (dot(d, d) > 0.25)
        discard;
    // 亮黄 (255,220,60)
    fragColor = vec4(1.0, 0.8627, 0.2353, 1.0);
}
)";

/// 投影世界坐标 -> 屏幕像素（NDC -> viewport）。相机后/奇异返回极小值。
QPointF projectToScreen(const Mat4& mvp, const Vec3& p, int w, int h) {
    const float px = static_cast<float>(p.x), py = static_cast<float>(p.y),
                pz = static_cast<float>(p.z);
    const float cw = mvp.m[3] * px + mvp.m[7] * py + mvp.m[11] * pz + mvp.m[15];
    if (cw <= 1e-6f)
        return QPointF(-1e9, -1e9);
    const float cx = mvp.m[0] * px + mvp.m[4] * py + mvp.m[8] * pz + mvp.m[12];
    const float cy = mvp.m[1] * px + mvp.m[5] * py + mvp.m[9] * pz + mvp.m[13];
    const float ndcx = cx / cw, ndcy = cy / cw;
    return QPointF((ndcx * 0.5 + 0.5) * w, (1.0 - (ndcy * 0.5 + 0.5)) * h);
}
} // namespace

AvatarView3D::AvatarView3D(QWidget* parent)
    : QOpenGLWidget(parent)
{
    // 请求 3.3 core profile（QOpenGLWidget 默认兼容 profile）
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    setFormat(fmt);

    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true); // 标注模式下悬停投影圆圈需要无按键鼠标移动事件

    setupViewBar();
    setupMeasureBar();
}

void AvatarView3D::setupViewBar()
{
    m_viewBar = new QWidget(this);
    auto* lay = new QHBoxLayout(m_viewBar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);

    struct PresetEntry {
        ViewPreset preset;
        const char* label;
    };
    // MH 风格：正面/背面/左侧/右侧/顶部/底部 + 默认 3/4 视角
    const PresetEntry kPresets[] = {
        {ViewPreset::Front, "\xe6\xad\xa3\xe9\x9d\xa2"},
        {ViewPreset::Back, "\xe8\x83\x8c\xe9\x9d\xa2"},
        {ViewPreset::Left, "\xe5\xb7\xa6\xe4\xbe\xa7"},
        {ViewPreset::Right, "\xe5\x8f\xb3\xe4\xbe\xa7"},
        {ViewPreset::Top, "\xe9\xa1\xb6\xe9\x83\xa8"},
        {ViewPreset::Bottom, "\xe5\xba\x95\xe9\x83\xa8"},
        {ViewPreset::ThreeQuarter, "\xe9\xbb\x98\xe8\xae\xa4"},
    };
    for (const auto& entry : kPresets) {
        auto* btn = new QToolButton(m_viewBar);
        btn->setText(QString::fromUtf8(entry.label));
        btn->setObjectName(QStringLiteral("viewPresetBtn"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setAutoRaise(true);
        connect(btn, &QToolButton::clicked, this, [this, p = entry.preset] {
            setViewPreset(p);
        });
        lay->addWidget(btn);
    }

    // 半透明悬浮底：不挡模型，按钮可点
    m_viewBar->setStyleSheet(QStringLiteral(
        "QWidget#viewPresetBar { background: rgba(24, 24, 28, 150);"
        " border-radius: 6px; }"
        "QToolButton#viewPresetBtn { color: #DDD; background: transparent;"
        " border: none; padding: 3px 7px; border-radius: 4px; font-size: 12px; }"
        "QToolButton#viewPresetBtn:hover { background: rgba(255, 255, 255, 30); }"
        "QToolButton#viewPresetBtn:pressed { background: rgba(255, 255, 255, 55); }"));
    m_viewBar->setObjectName(QStringLiteral("viewPresetBar"));
    m_viewBar->adjustSize();
    m_viewBar->move(8, 8);
}

void AvatarView3D::setupMeasureBar()
{
    m_measureBar = new QScrollArea(this);
    m_measureBar->setObjectName(QStringLiteral("measureBar"));
    m_measureBar->setWidgetResizable(true);
    m_measureBar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_measureBar->setFrameShape(QFrame::NoFrame);

    auto* host = new QWidget(m_measureBar);
    host->setObjectName(QStringLiteral("measureBarHost"));
    m_measureHostLayout = new QVBoxLayout(host);
    m_measureHostLayout->setContentsMargins(6, 4, 6, 4);
    m_measureHostLayout->setSpacing(0);

    auto* title = new QLabel(QStringLiteral("测量线"), host);
    title->setObjectName(QStringLiteral("measureBarTitle"));
    m_measureHostLayout->addWidget(title);

    auto* row = new QHBoxLayout();
    m_measureAllBtn = new QToolButton(host);
    m_measureAllBtn->setObjectName(QStringLiteral("measureAllBtn"));
    m_measureAllBtn->setText(QStringLiteral("全开"));
    m_measureAllBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_measureAllBtn->setAutoRaise(true);
    m_measureAllBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_measureAllBtn, &QToolButton::clicked, this, [this]() { setAllMeasureVisible(true); });
    m_measureNoneBtn = new QToolButton(host);
    m_measureNoneBtn->setObjectName(QStringLiteral("measureNoneBtn"));
    m_measureNoneBtn->setText(QStringLiteral("全关"));
    m_measureNoneBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_measureNoneBtn->setAutoRaise(true);
    m_measureNoneBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_measureNoneBtn, &QToolButton::clicked, this, [this]() { setAllMeasureVisible(false); });
    m_measureLabelBtn = new QToolButton(host);
    m_measureLabelBtn->setObjectName(QStringLiteral("measureLabelBtn"));
    m_measureLabelBtn->setText(QStringLiteral("名称"));
    m_measureLabelBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_measureLabelBtn->setAutoRaise(true);
    m_measureLabelBtn->setCheckable(true);
    m_measureLabelBtn->setChecked(true); // 默认显示名称
    m_measureLabelBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_measureLabelBtn, &QToolButton::toggled, this,
            [this](bool on) { setShowMeasureLabels(on); });
    row->addWidget(m_measureAllBtn);
    row->addWidget(m_measureNoneBtn);
    row->addWidget(m_measureLabelBtn);
    m_measureHostLayout->addLayout(row);

    m_measureHostLayout->addStretch(1);
    m_measureBar->setWidget(host);
    m_measureBar->setFixedWidth(150);

    m_measureBar->setStyleSheet(QStringLiteral(
        "QScrollArea#measureBar { background: rgba(24, 24, 28, 150); border-radius: 6px; }"
        "QWidget#measureBarHost { background: transparent; }"
        "QLabel#measureBarTitle { color: #EEE; font-weight: bold; padding: 2px; }"
        "QCheckBox { color: #DDD; font-size: 11px; spacing: 2px; background: transparent; }"
        "QToolButton#measureAllBtn, QToolButton#measureNoneBtn, QToolButton#measureLabelBtn {"
        " color: #CCC; background: rgba(255, 255, 255, 18); border: none; padding: 2px 4px;"
        " border-radius: 4px; font-size: 11px; margin: 2px; }"
        "QToolButton#measureAllBtn:hover, QToolButton#measureNoneBtn:hover,"
        " QToolButton#measureLabelBtn:hover { background: rgba(255, 255, 255, 40); }"
        "QToolButton#measureLabelBtn:checked { background: rgba(120, 200, 255, 70);"
        " color: #EAF6FF; }"));
    m_measureBar->setVisible(false); // 无链时隐藏
}

void AvatarView3D::setMeasureChains(const std::map<std::string, std::vector<int>>& chains,
                                    const std::map<std::string, std::string>& names,
                                    const std::map<std::string, std::vector<Vec3>>& pointChains,
                                    const std::map<std::string, cad::avatar::SectionChain>& sectionChains,
                                    const std::map<std::string, cad::avatar::StraightChain>& straightChains)
{
    m_measureChains = chains;
    m_measurePointChains = pointChains;
    m_measureSectionChains = sectionChains;
    m_measureStraightChains = straightChains;
    m_measureNames = names;
    m_measureVisible.clear();

    // 清空旧勾选项，重建
    for (auto& [key, cb] : m_measureBoxes)
        cb->deleteLater();
    m_measureBoxes.clear();
    const auto addCheck = [&](const std::string& key) {
        m_measureVisible.insert(key); // 默认全部可见
        auto* cb = new QCheckBox(QString::fromStdString(displayName(key)));
        cb->setObjectName(QStringLiteral("measureChk"));
        cb->setChecked(true);
        connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
            setMeasureVisible(key, on);
        });
        m_measureHostLayout->insertWidget(m_measureHostLayout->count() - 1, cb);
        m_measureBoxes[key] = cb;
    };
    for (const auto& [key, chain] : m_measureChains)
        addCheck(key);
    for (const auto& [key, pts] : m_measurePointChains)
        if (!m_measureChains.count(key))
            addCheck(key); // 坐标链独立成项（不与索引链重名）
    for (const auto& [key, sc] : m_measureSectionChains)
        if (!m_measureChains.count(key) && !m_measurePointChains.count(key))
            addCheck(key); // 截面链独立成项
    for (const auto& [key, sc] : m_measureStraightChains)
        if (!m_measureChains.count(key) && !m_measurePointChains.count(key) &&
            !m_measureSectionChains.count(key))
            addCheck(key); // 直线链独立成项
    m_measureBar->setVisible(!m_measureChains.empty() || !m_measurePointChains.empty() ||
                             !m_measureSectionChains.empty() || !m_measureStraightChains.empty());
    m_measureDirty = true;
    update();
}

void AvatarView3D::setMeasureVisible(const std::string& key, bool visible)
{
    if (!m_measureChains.count(key) && !m_measurePointChains.count(key) &&
        !m_measureSectionChains.count(key) && !m_measureStraightChains.count(key))
        return;
    if (visible)
        m_measureVisible.insert(key);
    else
        m_measureVisible.erase(key);
    m_measureDirty = true;
    update();
}

std::vector<std::string> AvatarView3D::measureKeys() const
{
    std::vector<std::string> keys;
    keys.reserve(m_measureChains.size() + m_measurePointChains.size() + m_measureSectionChains.size());
    for (const auto& [key, chain] : m_measureChains)
        keys.push_back(key);
    for (const auto& [key, pts] : m_measurePointChains)
        if (!m_measureChains.count(key))
            keys.push_back(key);
    for (const auto& [key, sc] : m_measureSectionChains)
        if (!m_measureChains.count(key) && !m_measurePointChains.count(key))
            keys.push_back(key);
    return keys;
}

void AvatarView3D::setAllMeasureVisible(bool visible)
{
    for (const auto& [key, chain] : m_measureChains)
        setMeasureVisible(key, visible);
    for (const auto& [key, pts] : m_measurePointChains)
        setMeasureVisible(key, visible);
    for (const auto& [key, sc] : m_measureSectionChains)
        setMeasureVisible(key, visible);
    for (const auto& [key, sc] : m_measureStraightChains)
        setMeasureVisible(key, visible);
    // 同步勾选框状态（setMeasureVisible 不回调 QCheckBox，避免逐项触发重绘）
    for (auto& [key, cb] : m_measureBoxes) {
        if (cb->isChecked() != visible)
            cb->setChecked(visible);
    }
}

void AvatarView3D::setShowMeasureLabels(bool show)
{
    if (m_showMeasureLabels == show)
        return;
    m_showMeasureLabels = show;
    update();
}

AvatarView3D::~AvatarView3D()
{
    makeCurrent();
    if (m_program) m_gl->glDeleteProgram(m_program);
    if (m_groundProgram) m_gl->glDeleteProgram(m_groundProgram);
    if (m_lineProgram) m_gl->glDeleteProgram(m_lineProgram);
    if (m_matcapTex) m_gl->glDeleteTextures(1, &m_matcapTex);
    if (m_vao) m_gl->glDeleteVertexArrays(1, &m_vao);
    if (m_vbo) m_gl->glDeleteBuffers(1, &m_vbo);
    if (m_ebo) m_gl->glDeleteBuffers(1, &m_ebo);
    if (m_groundVao) m_gl->glDeleteVertexArrays(1, &m_groundVao);
    if (m_groundVbo) m_gl->glDeleteBuffers(1, &m_groundVbo);
    if (m_lineVao) m_gl->glDeleteVertexArrays(1, &m_lineVao);
    if (m_lineVbo) m_gl->glDeleteBuffers(1, &m_lineVbo);
    if (m_annProgram) m_gl->glDeleteProgram(m_annProgram);
    if (m_annVao) m_gl->glDeleteVertexArrays(1, &m_annVao);
    if (m_annVbo) m_gl->glDeleteBuffers(1, &m_annVbo);
    if (m_annLineVao) m_gl->glDeleteVertexArrays(1, &m_annLineVao);
    if (m_annLineVbo) m_gl->glDeleteBuffers(1, &m_annLineVbo);
    doneCurrent();
}

void AvatarView3D::setMesh(const Mesh3D& mesh)
{
    m_mesh = mesh;
    m_meshDirty = true;
    if (!m_deferMeasure)
        m_measureDirty = true; // 延迟测量时保持旧测量线，不逐帧重算
    if (!m_mesh.empty()) {
        // 初始视角对齐新网格包围盒（仅首次 / resetView 时由调用方触发）
        if (!m_glReady) resetView();
    }
    refreshBoundAnnotations();
    update();
}

void AvatarView3D::setDeferMeasure(bool defer)
{
    if (m_deferMeasure == defer)
        return;
    m_deferMeasure = defer;
    if (defer) {
        // 拖动开始：测量线 + 值标签整体隐藏，留给用户干净视野（松开再显示）。
        // 线几何与标签信息都清空并重传，画布立即不绘制；测量 cm 值保留给下拉框显示。
        m_lineVerts.clear();
        m_lineVertexCount = 0;
        m_measureInfo.clear();
        if (m_glReady)
            uploadMeasureLines();
        update();
    } else {
        // 松开：取消延迟，强制重算一次最终测量值并重新显示测量线/标签。
        m_measureDirty = true;
        update();
    }
}

void AvatarView3D::refreshBoundAnnotations()
{
    if (m_mesh.empty())
        return;
    const auto& f = m_mesh.faces;
    const auto& v = m_mesh.verts;
    for (auto& a : m_annotations) {
        // 借用水平面绑定（Ctrl 点击借用测量线水平面）：Y 跟随该测量线当前高度，
        // X/Z 随贴肤重心绑定跟随 morph。
        if (!a.heightKey.empty()) {
            const double H = measureLineHeight(a.heightKey);
            Vec3 body = a.pos;
            if (a.tri >= 0 && a.tri < static_cast<int>(f.size())) {
                const auto& tri = f[static_cast<size_t>(a.tri)];
                if (tri[0] >= 0 && tri[1] >= 0 && tri[2] >= 0
                    && tri[0] < static_cast<int>(v.size())
                    && tri[1] < static_cast<int>(v.size())
                    && tri[2] < static_cast<int>(v.size())) {
                    const double w = 1.0 - a.bcU - a.bcV;
                    body = v[static_cast<size_t>(tri[0])] * a.bcU
                         + v[static_cast<size_t>(tri[1])] * a.bcV
                         + v[static_cast<size_t>(tri[2])] * w;
                }
            }
            a.pos = Vec3{body.x, H, body.z};
            continue;
        }
        if (a.tri < 0 || a.tri >= static_cast<int>(f.size()))
            continue; // 无绑定或三角形越界
        const auto& tri = f[static_cast<size_t>(a.tri)];
        if (tri[0] < 0 || tri[1] < 0 || tri[2] < 0
            || tri[0] >= static_cast<int>(v.size())
            || tri[1] >= static_cast<int>(v.size())
            || tri[2] >= static_cast<int>(v.size()))
            continue;
        const double w = 1.0 - a.bcU - a.bcV;
        a.pos = v[static_cast<size_t>(tri[0])] * a.bcU
              + v[static_cast<size_t>(tri[1])] * a.bcV
              + v[static_cast<size_t>(tri[2])] * w;
    }
}

double AvatarView3D::sectionHeight(const cad::avatar::SectionChain& sc) const
{
    const auto& verts = m_mesh.verts;
    const int vn = static_cast<int>(verts.size());
    double Y = 0.0;
    int cnt = 0;
    for (const auto& sp : sc.points) {
        if (sp.a < 0 || sp.a >= vn || sp.b < 0 || sp.b >= vn)
            continue;
        Y += verts[static_cast<size_t>(sp.a)].y * (1.0 - sp.t) +
             verts[static_cast<size_t>(sp.b)].y * sp.t;
        ++cnt;
    }
    if (cnt == 0)
        return 0.0;
    Y /= static_cast<double>(cnt);
    if (sc.anchor >= 0 && sc.anchor < vn)
        Y = Y * 0.5 + verts[static_cast<size_t>(sc.anchor)].y * 0.5;
    return Y;
}

double AvatarView3D::measureLineHeight(const std::string& key) const
{
    const auto& verts = m_mesh.verts;
    const int vn = static_cast<int>(verts.size());
    // 截面链：动态截面高度（严格水平环）
    if (auto it = m_measureSectionChains.find(key); it != m_measureSectionChains.end())
        return sectionHeight(it->second);
    // 顶点链：顶点平均 y（围度/宽度线的代表水平高度）
    if (auto it = m_measureChains.find(key); it != m_measureChains.end()) {
        double Y = 0.0;
        int n = 0;
        for (const int idx : it->second) {
            if (idx < 0 || idx >= vn)
                continue;
            Y += verts[static_cast<size_t>(idx)].y;
            ++n;
        }
        return n > 0 ? Y / static_cast<double>(n) : 0.0;
    }
    // 坐标链：点平均 y
    if (auto it = m_measurePointChains.find(key); it != m_measurePointChains.end()) {
        double Y = 0.0;
        for (const Vec3& p : it->second)
            Y += p.y;
        return it->second.empty() ? 0.0 : Y / static_cast<double>(it->second.size());
    }
    // 直线链：两端点平均 y
    if (auto it = m_measureStraightChains.find(key); it != m_measureStraightChains.end()) {
        const auto& sc = it->second;
        double Y = 0.0;
        int n = 0;
        const auto addY = [&](int v, const SectionPoint& sp) {
            if (v >= 0 && v < vn) {
                Y += verts[static_cast<size_t>(v)].y;
                ++n;
            } else if (sp.a >= 0 && sp.a < vn && sp.b >= 0 && sp.b < vn) {
                Y += verts[static_cast<size_t>(sp.a)].y * (1.0 - sp.t) +
                     verts[static_cast<size_t>(sp.b)].y * sp.t;
                ++n;
            }
        };
        addY(sc.fromVertex, sc.fromSection);
        addY(sc.toVertex, sc.toSection);
        return n > 0 ? Y / static_cast<double>(n) : 0.0;
    }
    return 0.0;
}

bool AvatarView3D::findNearestMeasurePlane(const Vec3& p, std::string& outKey, double& outH) const
{
    double best = 1e30;
    bool found = false;
    // 只有当前勾选可见的测量线才可被「借用水平面」。
    for (const std::string& key : m_measureVisible) {
        if (!m_measureChains.count(key) && !m_measurePointChains.count(key) &&
            !m_measureSectionChains.count(key) && !m_measureStraightChains.count(key))
            continue;
        const double H = measureLineHeight(key);
        const double d = std::fabs(p.y - H);
        if (d < best) {
            best = d;
            outKey = key;
            outH = H;
            found = true;
        }
    }
    return found;
}

void AvatarView3D::resetView()
{
    AvatarRenderMesh rm;
    rm.build(m_mesh);
    m_target = rm.center();
    const double r = rm.radius();
    m_dist = r / std::tan(kFovDeg * 0.5 * M_PI / 180.0) * 1.35;
    if (m_dist < 1.0) m_dist = 1.0;
    m_yawDeg = 32.0;
    m_pitchDeg = 14.0;
    update();
}

void AvatarView3D::initializeGL()
{
    m_gl = std::make_unique<QOpenGLFunctions_3_3_Core>();
    if (!m_gl->initializeOpenGLFunctions()) {
        qWarning("[avatar3d] cannot load OpenGL 3.3 core functions");
        m_glReady = false;
        emit glStatusChanged(false);
        return;
    }
    loadMatcapTexture();
    compileShaders();
    compileGroundShader();
    compileLineShader();
    compileAnnotationShader();
    if (m_program) {
        m_glReady = true;
        emit glStatusChanged(true);
    } else {
        emit glStatusChanged(false);
    }
}

void AvatarView3D::loadMatcapTexture()
{
    m_hasMatcap = false;
    const char* dir = std::getenv("GCAD_ASSETS_DIR");
    const QString baseDir = (dir && *dir) ? QString::fromUtf8(dir)
                                          : QString::fromUtf8(AVATAR_ASSETS_DIR);
    // MH 皮肤 litsphere（环境光响应查询表），与模型同目录
    const QString path = baseDir + QStringLiteral("/skin_litsphere.png");
    QImage img(path);
    if (img.isNull()) {
        qWarning("[avatar3d] matcap texture not found: %s",
                 path.toUtf8().constData());
        return;
    }
    img = img.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
    m_gl->glGenTextures(1, &m_matcapTex);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_matcapTex);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(),
                       0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_hasMatcap = true;
}

void AvatarView3D::compileShaders()
{
    const auto compile = [this](GLenum type, const char* src) -> GLuint {
        const GLuint s = m_gl->glCreateShader(type);
        m_gl->glShaderSource(s, 1, &src, nullptr);
        m_gl->glCompileShader(s);
        GLint ok = GL_FALSE;
        m_gl->glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            m_gl->glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            qWarning("[avatar3d] shader compile failed: %s", log);
            m_gl->glDeleteShader(s);
            return 0;
        }
        return s;
    };

    const GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) {
        if (vs) m_gl->glDeleteShader(vs);
        if (fs) m_gl->glDeleteShader(fs);
        return;
    }
    const GLuint prog = m_gl->glCreateProgram();
    m_gl->glAttachShader(prog, vs);
    m_gl->glAttachShader(prog, fs);
    m_gl->glLinkProgram(prog);
    m_gl->glDeleteShader(vs);
    m_gl->glDeleteShader(fs);
    GLint ok = GL_FALSE;
    m_gl->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        m_gl->glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        qWarning("[avatar3d] program link failed: %s", log);
        m_gl->glDeleteProgram(prog);
        return;
    }
    m_program = prog;
    m_uMvp = m_gl->glGetUniformLocation(prog, "uMvp");
    m_uNormal = m_gl->glGetUniformLocation(prog, "uNormal");
    m_uView = m_gl->glGetUniformLocation(prog, "uView");
    m_uLightDir = m_gl->glGetUniformLocation(prog, "uLightDir");
    m_uColor = m_gl->glGetUniformLocation(prog, "uColor");
    m_uAmbient = m_gl->glGetUniformLocation(prog, "uAmbient");
    m_uShininess = m_gl->glGetUniformLocation(prog, "uShininess");
    m_uUseMatcap = m_gl->glGetUniformLocation(prog, "uUseMatcap");
    m_uMatcap = m_gl->glGetUniformLocation(prog, "uMatcap");

    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_vbo);
    m_gl->glGenBuffers(1, &m_ebo);
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                sizeof(RenderVertex), reinterpret_cast<void*>(0));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                sizeof(RenderVertex),
                                reinterpret_cast<void*>(3 * sizeof(float)));
    m_gl->glEnableVertexAttribArray(2);
    m_gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                                sizeof(RenderVertex),
                                reinterpret_cast<void*>(6 * sizeof(float)));
    m_gl->glBindVertexArray(0);
}

void AvatarView3D::compileGroundShader()
{
    const auto compile = [this](GLenum type, const char* src) -> GLuint {
        const GLuint s = m_gl->glCreateShader(type);
        m_gl->glShaderSource(s, 1, &src, nullptr);
        m_gl->glCompileShader(s);
        GLint ok = GL_FALSE;
        m_gl->glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            m_gl->glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            qWarning("[avatar3d] ground shader compile failed: %s", log);
            m_gl->glDeleteShader(s);
            return 0;
        }
        return s;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, kGroundVertexShader);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kGroundFragmentShader);
    if (!vs || !fs) {
        if (vs) m_gl->glDeleteShader(vs);
        if (fs) m_gl->glDeleteShader(fs);
        return;
    }
    const GLuint prog = m_gl->glCreateProgram();
    m_gl->glAttachShader(prog, vs);
    m_gl->glAttachShader(prog, fs);
    m_gl->glLinkProgram(prog);
    m_gl->glDeleteShader(vs);
    m_gl->glDeleteShader(fs);
    GLint ok = GL_FALSE;
    m_gl->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        m_gl->glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        qWarning("[avatar3d] ground program link failed: %s", log);
        m_gl->glDeleteProgram(prog);
        return;
    }
    m_groundProgram = prog;
    m_groundU_mvp = m_gl->glGetUniformLocation(prog, "uMvp");
    m_groundU_extent = m_gl->glGetUniformLocation(prog, "uExtent");
    m_groundU_center = m_gl->glGetUniformLocation(prog, "uCenterXZ");

    m_gl->glGenVertexArrays(1, &m_groundVao);
    m_gl->glGenBuffers(1, &m_groundVbo);
    m_gl->glBindVertexArray(m_groundVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_groundVbo);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                                reinterpret_cast<void*>(0));
    m_gl->glBindVertexArray(0);
}

void AvatarView3D::compileLineShader()
{
    const auto compile = [this](GLenum type, const char* src) -> GLuint {
        const GLuint s = m_gl->glCreateShader(type);
        m_gl->glShaderSource(s, 1, &src, nullptr);
        m_gl->glCompileShader(s);
        GLint ok = GL_FALSE;
        m_gl->glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            m_gl->glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            qWarning("[avatar3d] line shader compile failed: %s", log);
            m_gl->glDeleteShader(s);
            return 0;
        }
        return s;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, kLineVertexShader);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kLineFragmentShader);
    if (!vs || !fs) {
        if (vs) m_gl->glDeleteShader(vs);
        if (fs) m_gl->glDeleteShader(fs);
        return;
    }
    const GLuint prog = m_gl->glCreateProgram();
    m_gl->glAttachShader(prog, vs);
    m_gl->glAttachShader(prog, fs);
    m_gl->glLinkProgram(prog);
    m_gl->glDeleteShader(vs);
    m_gl->glDeleteShader(fs);
    GLint ok = GL_FALSE;
    m_gl->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        m_gl->glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        qWarning("[avatar3d] line program link failed: %s", log);
        m_gl->glDeleteProgram(prog);
        return;
    }
    m_lineProgram = prog;
    m_lineU_mvp = m_gl->glGetUniformLocation(prog, "uMvp");

    m_gl->glGenVertexArrays(1, &m_lineVao);
    m_gl->glGenBuffers(1, &m_lineVbo);
    m_gl->glBindVertexArray(m_lineVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                reinterpret_cast<void*>(0));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                reinterpret_cast<void*>(3 * sizeof(float)));
    m_gl->glBindVertexArray(0);

    // 标注连线 VBO：与测量线同一格式（pos3+color3），复用 line shader
    m_gl->glGenVertexArrays(1, &m_annLineVao);
    m_gl->glGenBuffers(1, &m_annLineVbo);
    m_gl->glBindVertexArray(m_annLineVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_annLineVbo);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                reinterpret_cast<void*>(0));
    m_gl->glEnableVertexAttribArray(1);
    m_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                reinterpret_cast<void*>(3 * sizeof(float)));
    m_gl->glBindVertexArray(0);
}

void AvatarView3D::compileAnnotationShader()
{
    const auto compile = [this](GLenum type, const char* src) -> GLuint {
        const GLuint s = m_gl->glCreateShader(type);
        m_gl->glShaderSource(s, 1, &src, nullptr);
        m_gl->glCompileShader(s);
        GLint ok = GL_FALSE;
        m_gl->glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {};
            m_gl->glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            qWarning("[avatar3d] annotation shader compile failed: %s", log);
            m_gl->glDeleteShader(s);
            return 0;
        }
        return s;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, kAnnVertexShader);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kAnnFragmentShader);
    if (!vs || !fs) {
        if (vs) m_gl->glDeleteShader(vs);
        if (fs) m_gl->glDeleteShader(fs);
        return;
    }
    const GLuint prog = m_gl->glCreateProgram();
    m_gl->glAttachShader(prog, vs);
    m_gl->glAttachShader(prog, fs);
    m_gl->glLinkProgram(prog);
    m_gl->glDeleteShader(vs);
    m_gl->glDeleteShader(fs);
    GLint ok = GL_FALSE;
    m_gl->glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        m_gl->glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        qWarning("[avatar3d] annotation program link failed: %s", log);
        m_gl->glDeleteProgram(prog);
        return;
    }
    m_annProgram = prog;
    m_annU_mvp = m_gl->glGetUniformLocation(prog, "uMvp");
    m_annU_scale = m_gl->glGetUniformLocation(prog, "uPointSize");

    m_gl->glGenVertexArrays(1, &m_annVao);
    m_gl->glGenBuffers(1, &m_annVbo);
    m_gl->glBindVertexArray(m_annVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_annVbo);
    m_gl->glEnableVertexAttribArray(0);
    m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                                reinterpret_cast<void*>(0));
    m_gl->glBindVertexArray(0);
}
void AvatarView3D::uploadGround(double groundY, double radius)
{
    if (!m_glReady || !m_groundProgram) return;
    m_groundY = static_cast<float>(groundY);
    m_groundExtent = static_cast<float>(std::clamp(radius * 6.0, 30.0, 150.0));
    const float e = m_groundExtent;
    const float y = m_groundY;
    // 以世界原点为中心的大四边形（覆盖模型半径 + 余量），仅位置属性
    const float verts[] = {
        -e, y, -e,  e, y, -e,  e, y, e,
        -e, y, -e,  e, y, e,   -e, y, e,
    };
    m_gl->glBindVertexArray(m_groundVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_groundVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    m_gl->glBindVertexArray(0);
}

void AvatarView3D::uploadMesh()
{
    if (!m_glReady || !m_program) return;
    AvatarRenderMesh rm;
    rm.build(m_mesh);
    m_indexCount = rm.indexCount();
    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(rm.vertices().size() * sizeof(RenderVertex)),
                       rm.vertices().data(), GL_DYNAMIC_DRAW);
    m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(rm.indices().size() * sizeof(uint32_t)),
                       rm.indices().data(), GL_DYNAMIC_DRAW);
    m_gl->glBindVertexArray(0);

    // 地面跟随脚底高度（morph 后 minY 可能变化）
    uploadGround(rm.minY(), rm.radius());

    // 相机初始对准（首次有数据时）
    if (m_dist <= 1.0) {
        m_target = rm.center();
        m_dist = rm.radius() / std::tan(kFovDeg * 0.5 * M_PI / 180.0) * 1.35;
    }
    m_meshDirty = false;
}

void AvatarView3D::rebuildMeasureLines()
{
    m_measureInfo.clear();
    m_lineVerts.clear();
    m_lineVertexCount = 0;
    // 无任何可见测量（勾选全关）时直接跳过，避免拖动时仍算法线+测地线（卡顿残留）。
    if (m_mesh.empty() || m_measureVisible.empty())
        return;

    const auto& verts = m_mesh.verts;
    // 顶点法线（m_mesh 可能未计算，则复制计算一次并缓存）
    if (m_normals.size() != verts.size()) {
        Mesh3D tmp = m_mesh;
        tmp.calcNormals();
        m_normals = std::move(tmp.normals);
    }

    // 10 色稳定调色板（按 key 字典序分配，链色不随勾选漂移）
    static constexpr std::array<std::array<float, 3>, 10> kPalette{{
        {{0.95f, 0.35f, 0.35f}}, {{0.95f, 0.62f, 0.25f}}, {{0.92f, 0.80f, 0.30f}},
        {{0.45f, 0.85f, 0.40f}}, {{0.30f, 0.80f, 0.75f}}, {{0.35f, 0.60f, 0.95f}},
        {{0.55f, 0.45f, 0.95f}}, {{0.85f, 0.50f, 0.90f}}, {{0.90f, 0.55f, 0.75f}},
        {{0.75f, 0.75f, 0.80f}},
    }};

    // 测地拓扑缓存：只在本函数首次进入时重建（faces 拓扑不随 morph 变），
    // 之后各距离链复用，避免每帧重建邻接表（拖动卡顿主因）。
    m_geodes.setMesh(m_mesh);
    int stableIndex = 0;
    for (const auto& [key, chain] : m_measureChains) {
        // 全部链都算 cm 值（关闭测量线只影响绘制，不影响测量值显示）
        m_measureCm[key] = (chain.size() >= 2) ? measureValue(key, chain, verts) : 0.0;

        if (!m_measureVisible.count(key) || chain.size() < 2) {
            ++stableIndex;
            continue;
        }

        // 颜色按 key 字典序稳定分配（不随勾选漂移）
        const auto& c = kPalette[stableIndex % kPalette.size()];
        ++stableIndex;
        const QColor color = QColor::fromRgbF(c[0], c[1], c[2]);

        // 距离类（非围度、非 custom 短线）且恰为两点：用网格测地线贴体表绕开
        // 凸起（如颈到腰被背宽吃、腰到臀被肚子吃）。custom/* 短线（胸点到胸点、
        // 侧颈点到胸点）保持直线——测地会让短斜线逐顶点弯绕、失去测量价值。
        std::vector<Vec3> geom;
        double cm = 0.0;
        bool geomPreOffset = false; // 测地线已在生成时沿法线外移（绘制分支不再重复外移）
        const bool isCustom = key.rfind("custom/", 0) == 0;
        if (!isCircularMeasure(key) && chain.size() == 2 && !isCustom) {
            const std::vector<int> geoPath = m_geodes.path(chain[0], chain[1]);
            std::vector<Vec3> raw;
            raw.reserve(geoPath.size());
            // 每个测地顶点沿其表面法线外移：让整条贴肤测地线浮在体表上方，
            // 避免中段被胸部等凸起遮挡（否则只有链头尾两点外移、中段贴肤被埋）。
            for (const int vi : geoPath) {
                Vec3 p = verts[static_cast<size_t>(vi)];
                if (vi >= 0 && vi < static_cast<int>(m_normals.size()))
                    p = p + m_normals[static_cast<size_t>(vi)] * kMeasureOffset;
                raw.push_back(p);
            }
            geomPreOffset = true;
            // 等距重采样：测地路径逐顶点太密、太贴皮肤微起伏（弯弯绕绕），
            // 按 kMeasureSampleStep 沿弧长每 4cm 取一个点，让线笔直但整体贴表。
            geom.clear();
            geom.push_back(raw.front());
            double acc = 0.0;
            double remain = kMeasureSampleStep;
            for (size_t i = 0; i + 1 < raw.size(); ++i) {
                const Vec3 seg = raw[i + 1] - raw[i];
                const double len = seg.length();
                if (len < 1e-9) continue;
                const Vec3 dir = seg / len;
                double t = 0.0;
                while (t + remain <= len) {
                    t += remain;
                    geom.push_back(raw[i] + dir * t);
                    acc += remain;
                    remain = kMeasureSampleStep;
                }
                remain -= (len - t);
            }
            // 末点确保保留（避免因步长不能整除而丢掉终点）
            if (!(geom.back() == raw.back()))
                geom.push_back(raw.back());
            cm = (geoPath.size() >= 2) ? m_geodes.distance(chain[0], chain[1]) * 10.0
                                       : 0.0;
        } else {
            geom = measureGeometry(key, chain, verts);
            cm = m_measureCm[key];
        }
        if (geom.size() < 2)
            continue;

        const auto pushSegment = [&](const Vec3& pa, const Vec3& pb) {
            const float v[12] = {
                static_cast<float>(pa.x), static_cast<float>(pa.y), static_cast<float>(pa.z),
                c[0], c[1], c[2],
                static_cast<float>(pb.x), static_cast<float>(pb.y), static_cast<float>(pb.z),
                c[0], c[1], c[2],
            };
            m_lineVerts.insert(m_lineVerts.end(), std::begin(v), std::end(v));
        };

        Vec3 labelPos;
        Vec3 labelNormal{0, 1, 0};
        if (isCircularMeasure(key)) {
            // 围度：闭合凸包环，沿截面中心径向外移防 Z-fighting
            Vec3 center{0.0, 0.0, 0.0};
            for (const auto& p : geom) center += p;
            center = center / static_cast<double>(geom.size());
            const auto offset = [&](const Vec3& p) {
                const Vec3 d{p.x - center.x, 0.0, p.z - center.z};
                const double len = d.length();
                if (len < 1e-9) return p;
                return Vec3{p.x + d.x / len * kMeasureOffset, p.y,
                            p.z + d.z / len * kMeasureOffset};
            };
            for (size_t i = 0; i < geom.size(); ++i)
                pushSegment(offset(geom[i]), offset(geom[(i + 1) % geom.size()]));
            // 标签锚点取凸包环上最靠 +X（身体右侧）顶点并径向外移，贴体表；
            // 法线取该顶点法线，用于朝向相机判定（转到背面时标签隐藏）。
            size_t anchorIdx = 0;
            for (size_t i = 1; i < geom.size(); ++i)
                if (geom[i].x > geom[anchorIdx].x) anchorIdx = i;
            labelPos = offset(geom[anchorIdx]);
            const int anchorVert = (anchorIdx < chain.size())
                                       ? chain[anchorIdx] : -1;
            if (anchorVert >= 0 && anchorVert < static_cast<int>(m_normals.size()))
                labelNormal = m_normals[static_cast<size_t>(anchorVert)];
        } else {
            // 路径：折线逐段绘制，端点沿各自顶点法线外移（多点=曲线测量如肩宽经 C7）。
            // 测地线（geomPreOffset）已在生成时整体外移，这里不再重复，避免二次偏置。
            const size_t segs = geom.size() - 1;
            for (size_t i = 0; i < segs; ++i) {
                Vec3 pa = geom[i];
                Vec3 pb = geom[i + 1];
                if (!geomPreOffset) {
                    const int aIdx = (i < chain.size()) ? chain[i] : -1;
                    const int bIdx = (i + 1 < chain.size()) ? chain[i + 1] : -1;
                    if (aIdx >= 0 && aIdx < static_cast<int>(m_normals.size()))
                        pa = pa + m_normals[aIdx] * kMeasureOffset;
                    if (bIdx >= 0 && bIdx < static_cast<int>(m_normals.size()))
                        pb = pb + m_normals[bIdx] * kMeasureOffset;
                }
                pushSegment(pa, pb);
            }
            // label：折线中点（沿路径长度一半处）
            double total = 0.0;
            for (size_t i = 0; i < segs; ++i)
                total += geom[i].distanceTo(geom[i + 1]);
            const double half = total * 0.5;
            double acc = 0.0;
            Vec3 mid = geom.front();
            size_t midSeg = 0;
            for (size_t i = 0; i < segs; ++i) {
                const double seg = geom[i].distanceTo(geom[i + 1]);
                if (acc + seg >= half) {
                    const double t = (seg > 0.0) ? (half - acc) / seg : 0.0;
                    mid = geom[i] + (geom[i + 1] - geom[i]) * t;
                    midSeg = i;
                    break;
                }
                acc += seg;
            }
            const int midIdx = (midSeg < chain.size()) ? chain[midSeg] : -1;
            Vec3 n = (midIdx >= 0 && midIdx < static_cast<int>(m_normals.size()))
                         ? m_normals[midIdx]
                         : Vec3::zero();
            labelPos = mid + n * 0.09;
            labelNormal = n;
        }
        m_measureInfo[key] = MeasureLineInfo{labelPos, labelNormal, cm, color};
    }

    // 精确坐标测量链（手工标定关键点，如前胸宽/侧颈到胸/肩距）：直接用坐标
    // 连折线，不吸附顶点、不测地。坐标是标定时的精确位置（0.1m 单位）。
    int pointColorIndex = 0;
    for (const auto& [key, pts] : m_measurePointChains) {
        if (!m_measureVisible.count(key) || pts.size() < 2)
            continue;
        const auto& c = kPalette[pointColorIndex % kPalette.size()];
        ++pointColorIndex;
        const QColor color = QColor::fromRgbF(c[0], c[1], c[2]);

        const auto pushPt = [&](const Vec3& pa, const Vec3& pb) {
            const float v[12] = {
                static_cast<float>(pa.x), static_cast<float>(pa.y), static_cast<float>(pa.z),
                c[0], c[1], c[2],
                static_cast<float>(pb.x), static_cast<float>(pb.y), static_cast<float>(pb.z),
                c[0], c[1], c[2],
            };
            m_lineVerts.insert(m_lineVerts.end(), std::begin(v), std::end(v));
        };
        double total = 0.0;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            pushPt(pts[i], pts[i + 1]);
            total += pts[i].distanceTo(pts[i + 1]);
        }
        const double cm = total * 10.0;
        // 标签锚点取链中点，法线用世界朝上（近似，足够朝向判定）
        Vec3 mid = pts[pts.size() / 2];
        m_measureCm[key] = cm;
        m_measureInfo[key] = MeasureLineInfo{mid, Vec3{0, 1, 0}, cm, color};
    }

    // 水平截面链（腰围等严格水平环）：按当前网格顶点边插值 → 截面环，
    // Newell 凸包周长与测量值一致；径向偏移防 Z-fighting（同围度线规则）。
    for (const auto& [key, sc] : m_measureSectionChains) {
        if (!m_measureVisible.count(key))
            continue;
        const auto& verts = m_mesh.verts;
        const int vn = static_cast<int>(verts.size());
        // 动态截面：高度 = 固定 t 插值点平均 y 与锚点 y 各半（平滑跟随 morph）
        const double secY = sectionHeight(sc);
        // 截面点 + 沿法线外移（腹部/背部凹陷处防埋，法线 = 边端点法线插值）
        std::vector<Vec3> secPts;
        secPts.reserve(sc.points.size());
        for (const auto& sp : sc.points) {
            if (sp.a < 0 || sp.a >= vn || sp.b < 0 || sp.b >= vn)
                continue;
            const Vec3& va = verts[static_cast<size_t>(sp.a)];
            const Vec3& vb = verts[static_cast<size_t>(sp.b)];
            const double dy = vb.y - va.y;
            if (std::fabs(dy) < 1e-12)
                continue;
            const double t = (secY - va.y) / dy;
            if (t < 0.0 || t > 1.0)
                continue;
            Vec3 n{0.0, 0.0, 0.0};
            if (sp.a < static_cast<int>(m_normals.size()))
                n = n + m_normals[static_cast<size_t>(sp.a)] * (1.0 - t);
            if (sp.b < static_cast<int>(m_normals.size()))
                n = n + m_normals[static_cast<size_t>(sp.b)] * t;
            const Vec3 p = va * (1.0 - t) + vb * t;
            secPts.push_back(p + n.normalized() * kSectionOffset);
        }
        if (secPts.size() < 3)
            continue;
        const auto& c = kPalette[pointColorIndex % kPalette.size()];
        ++pointColorIndex;
        const QColor color = QColor::fromRgbF(c[0], c[1], c[2]);

        // 凸包环（与 MeasureSystem 的测量值同一几何，此处输入已沿法线外移的环）。
        const std::vector<Vec3> hull = cad::avatar::circularSectionGeometry(secPts);
        if (hull.size() < 3)
            continue;
        const auto pushPt = [&](const Vec3& pa, const Vec3& pb) {
            const float v[12] = {
                static_cast<float>(pa.x), static_cast<float>(pa.y), static_cast<float>(pa.z),
                c[0], c[1], c[2],
                static_cast<float>(pb.x), static_cast<float>(pb.y), static_cast<float>(pb.z),
                c[0], c[1], c[2],
            };
            m_lineVerts.insert(m_lineVerts.end(), std::begin(v), std::end(v));
        };
        for (size_t i = 0; i < hull.size(); ++i)
            pushPt(hull[i], hull[(i + 1) % hull.size()]);

        double cm = 0.0;
        for (size_t i = 0; i < hull.size(); ++i)
            cm += hull[i].distanceTo(hull[(i + 1) % hull.size()]);
        cm *= 10.0;
        m_measureCm[key] = cm;

        // 标签锚点：凸包环最靠 +X 顶点（已外移）。
        size_t anchorIdx = 0;
        for (size_t i = 1; i < hull.size(); ++i)
            if (hull[i].x > hull[anchorIdx].x) anchorIdx = i;
        m_measureInfo[key] = MeasureLineInfo{hull[anchorIdx], Vec3{0, 1, 0}, cm, color};
    }

    // 直线距离链（脊柱至腰、腰至臀距）：沿中线两点直线（不贴肤测地、不偏离中心），
    // 端点可为顶点或截面插值点（对齐围度线），沿各自法线外移防埋。
    for (const auto& [key, sc] : m_measureStraightChains) {
        if (!m_measureVisible.count(key))
            continue;
        const auto& verts = m_mesh.verts;
        const int vn = static_cast<int>(verts.size());
        const auto endpoint = [&](int vertex, const cad::avatar::SectionPoint& sp,
                                  Vec3& n) -> Vec3 {
            if (vertex >= 0 && vertex < vn) {
                n = (vertex < static_cast<int>(m_normals.size()))
                        ? m_normals[static_cast<size_t>(vertex)]
                        : Vec3::zero();
                return verts[static_cast<size_t>(vertex)];
            }
            if (sp.a >= 0 && sp.a < vn && sp.b >= 0 && sp.b < vn) {
                const Vec3 na = (sp.a < static_cast<int>(m_normals.size()))
                                    ? m_normals[static_cast<size_t>(sp.a)]
                                    : Vec3::zero();
                const Vec3 nb = (sp.b < static_cast<int>(m_normals.size()))
                                    ? m_normals[static_cast<size_t>(sp.b)]
                                    : Vec3::zero();
                n = (na * (1.0 - sp.t) + nb * sp.t).normalized();
                return verts[static_cast<size_t>(sp.a)] * (1.0 - sp.t) +
                       verts[static_cast<size_t>(sp.b)] * sp.t;
            }
            n = Vec3::zero();
            return Vec3::zero();
        };
        Vec3 na{0, 0, 0}, nb{0, 0, 0};
        const Vec3 pa0 = endpoint(sc.fromVertex, sc.fromSection, na);
        const Vec3 pb0 = endpoint(sc.toVertex, sc.toSection, nb);
        const double cm = pa0.distanceTo(pb0) * 10.0; // 测量值 = 原始直线距离

        const auto& c = kPalette[pointColorIndex % kPalette.size()];
        ++pointColorIndex;
        const QColor color = QColor::fromRgbF(c[0], c[1], c[2]);
        const Vec3 pa = pa0 + na * kMeasureOffset;
        const Vec3 pb = pb0 + nb * kMeasureOffset;
        const float v[12] = {
            static_cast<float>(pa.x), static_cast<float>(pa.y), static_cast<float>(pa.z),
            c[0], c[1], c[2],
            static_cast<float>(pb.x), static_cast<float>(pb.y), static_cast<float>(pb.z),
            c[0], c[1], c[2],
        };
        m_lineVerts.insert(m_lineVerts.end(), std::begin(v), std::end(v));

        m_measureCm[key] = cm;
        const Vec3 mid = (pa + pb) * 0.5;
        const Vec3 midN = (na + nb).normalized();
        m_measureInfo[key] = MeasureLineInfo{mid, midN.length() > 1e-9 ? midN : Vec3{0, 1, 0},
                                             cm, color};
    }
    m_lineVertexCount = static_cast<int>(m_lineVerts.size()) / 6;
}

void AvatarView3D::uploadMeasureLines()
{
    if (!m_glReady || !m_lineProgram)
        return;
    m_gl->glBindVertexArray(m_lineVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(m_lineVerts.size() * sizeof(float)),
                       m_lineVerts.empty() ? nullptr : m_lineVerts.data(),
                       GL_DYNAMIC_DRAW);
    m_gl->glBindVertexArray(0);
}

void AvatarView3D::uploadAnnotationPoints()
{
    if (!m_glReady || !m_annProgram) return;
    // 顶点法线缓存（标注点绘制需要；无测量链时也保证可用）
    if (m_normals.size() != m_mesh.verts.size()) {
        Mesh3D tmp = m_mesh;
        tmp.calcNormals();
        m_normals = std::move(tmp.normals);
    }
    // 每个标注点一个顶点（沿其最近顶点法线略外移，防与体表 Z-fighting）
    std::vector<float> pts;
    pts.reserve(m_annotations.size() * 3u);
    for (const auto& a : m_annotations) {
        Vec3 n{0, 0, 0};
        if (a.vertex >= 0 && a.vertex < static_cast<int>(m_normals.size()))
            n = m_normals[static_cast<size_t>(a.vertex)];
        Vec3 p = a.pos + n * kAnnotationOffset;
        pts.push_back(static_cast<float>(p.x));
        pts.push_back(static_cast<float>(p.y));
        pts.push_back(static_cast<float>(p.z));
    }
    m_annPointCount = static_cast<int>(m_annotations.size());
    m_gl->glBindVertexArray(m_annVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_annVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(pts.size() * sizeof(float)),
                       pts.empty() ? nullptr : pts.data(),
                       GL_DYNAMIC_DRAW);
    m_gl->glBindVertexArray(0);
}
std::string AvatarView3D::displayName(const std::string& key) const
{
    const auto it = m_measureNames.find(key);
    if (it != m_measureNames.end() && !it->second.empty())
        return it->second;
    return key;
}

void AvatarView3D::refreshMeasureLabels()
{
    for (auto& [key, cb] : m_measureBoxes) {
        const auto it = m_measureCm.find(key);
        const double cm = (it != m_measureCm.end()) ? it->second : 0.0;
        cb->setText(QStringLiteral("%1  %2 cm")
                        .arg(QString::fromStdString(displayName(key)))
                        .arg(cm, 0, 'f', 1));
    }
}

void AvatarView3D::resizeGL(int w, int h)
{
    m_gl->glViewport(0, 0, w, h);
}

void AvatarView3D::computeViewProjection(Mat4& view, Mat4& proj, Mat4& mvp) const
{
    const double yaw = m_yawDeg * M_PI / 180.0;
    const double pitch = m_pitchDeg * M_PI / 180.0;
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    const Vec3 eye = m_target + Vec3(m_dist * cp * sy, m_dist * sp, m_dist * cp * cy);

    view = Mat4::lookAt(
        static_cast<float>(eye.x), static_cast<float>(eye.y), static_cast<float>(eye.z),
        static_cast<float>(m_target.x), static_cast<float>(m_target.y),
        static_cast<float>(m_target.z), 0.f, 1.f, 0.f);
    const float aspect = height() > 0 ? static_cast<float>(width()) / height() : 1.f;
    proj = Mat4::perspective(kFovDeg * M_PI / 180.f, aspect,
                             static_cast<float>(m_dist * 0.01),
                             static_cast<float>(std::max(m_dist * 100.0, 500.0)));
    mvp = Mat4::multiply(proj, view);
}

int AvatarView3D::pickVertex(const QPoint& screenPos) const
{
    Vec3 point;
    int vertex = -1;
    if (!pickSurfacePoint(screenPos, point, &vertex))
        return -1;
    return vertex;
}

bool AvatarView3D::pickSurfacePoint(const QPoint& screenPos, Vec3& outPoint, int* outVertex,
                                      int* outTri, double* outU, double* outV) const
{
    if (m_mesh.empty())
        return false;

    Mat4 view, proj, mvp;
    computeViewProjection(view, proj, mvp);
    const Mat4 inv = Mat4::inverse(mvp);

    const int w = std::max(width(), 1);
    const int h = std::max(height(), 1);
    const double nx = 2.0 * screenPos.x() / w - 1.0;
    const double ny = 1.0 - 2.0 * screenPos.y() / h;

    // NDC -> 世界空间（近/远平面两点定射线）
    const auto unproject = [&](double ndcZ) -> Vec3 {
        const double cx = inv.m[0] * nx + inv.m[4] * ny + inv.m[8] * ndcZ + inv.m[12];
        const double cy = inv.m[1] * nx + inv.m[5] * ny + inv.m[9] * ndcZ + inv.m[13];
        const double cz = inv.m[2] * nx + inv.m[6] * ny + inv.m[10] * ndcZ + inv.m[14];
        const double cw = inv.m[3] * nx + inv.m[7] * ny + inv.m[11] * ndcZ + inv.m[15];
        if (std::fabs(cw) < 1e-12)
            return Vec3{cx, cy, cz};
        return Vec3{cx / cw, cy / cw, cz / cw};
    };
    const Vec3 origin = unproject(-1.0);
    const Vec3 dir = (unproject(1.0) - origin).normalized();

    // Möller-Trumbore 遍历三角形，取最近命中。返回精确表面交点
    // （origin + dir*t），而非吸附到顶点——消除标注漂移。
    double bestT = 1e30;
    int bestVertex = -1;
    int bestTri = -1;
    double bestU = 0.0, bestV = 0.0;
    const auto& verts = m_mesh.verts;
    const bool hasGroups = m_mesh.faceGroups.size() == m_mesh.faces.size();
    for (size_t fi = 0; fi < m_mesh.faces.size(); ++fi) {
        const auto& f = m_mesh.faces[fi];
        // 与渲染共享白名单：拾取跳过被渲染隐藏的辅助几何（头发/裙子/紧身衣/
        // 生殖器/骨骼关节标记），否则射线会命中这些看不见的薄壳，标注点浮空。
        if (hasGroups && !isRenderableGroup(m_mesh.faceGroups[fi]))
            continue;
        const Vec3& v0 = verts[f[0]];
        const Vec3& v1 = verts[f[1]];
        const Vec3& v2 = verts[f[2]];
        const Vec3 e1 = v1 - v0;
        const Vec3 e2 = v2 - v0;
        const Vec3 p = dir.cross(e2);
        const double det = e1.dot(p);
        if (std::fabs(det) < 1e-12)
            continue;
        // 背面剔除：网格为外翻绕序（有符号体积>0），det<0 表示射线命中几何
        // 背面（背朝相机一侧），跳过以避免命中模型远侧/内腔表面导致浮空。
        if (det < 0.0)
            continue;
        const double invDet = 1.0 / det;
        const Vec3 s = origin - v0;
        const double u = s.dot(p) * invDet;
        if (u < 0.0 || u > 1.0)
            continue;
        const Vec3 q = s.cross(e1);
        const double v = dir.dot(q) * invDet;
        if (v < 0.0 || u + v > 1.0)
            continue;
        const double t = e2.dot(q) * invDet;
        if (t < 1e-9 || t >= bestT)
            continue;
        bestT = t;
        bestTri = static_cast<int>(fi);
        bestU = u; bestV = v;
        const double wb = 1.0 - u - v;
        if (u >= v && u >= wb)
            bestVertex = f[0];
        else if (v >= u && v >= wb)
            bestVertex = f[1];
        else
            bestVertex = f[2];
    }
    if (bestVertex < 0)
        return false;
    outPoint = origin + dir * bestT;
    if (outVertex)
        *outVertex = bestVertex;
    if (outTri)
        *outTri = bestTri;
    if (outU)
        *outU = bestU;
    if (outV)
        *outV = bestV;
    return true;
}

void AvatarView3D::setAnnotationMode(bool on)
{
    if (m_annotationMode == on)
        return;
    m_annotationMode = on;
    if (on)
        m_lineMode = false; // 标注模式与画线模式互斥
    m_hoverValid = false;
    m_snapPreviewValid = false;
    m_snapPreviewKey.clear();
    m_dragAnnotation = -1;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void AvatarView3D::setLineMode(bool on)
{
    if (m_lineMode == on)
        return;
    m_lineMode = on;
    m_lineStartIdx = -1;
    if (on) {
        m_annotationMode = false; // 画线模式与标注模式互斥
        m_hoverValid = false;
        m_dragAnnotation = -1;
    }
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void AvatarView3D::setAnnotationLines(const std::vector<AnnotationLine>& lines)
{
    m_annotationLines = lines;
    update();
}

void AvatarView3D::uploadAnnotationLines()
{
    if (!m_glReady || !m_annLineVao)
        return;
    // 每线 2 顶点，pos3 + color3（紫线，与测量线 VBO 格式一致，复用 line shader）
    const float c[3] = {0.78f, 0.42f, 0.95f}; // 亮紫
    std::vector<float> pts;
    pts.reserve(m_annotationLines.size() * 12);
    for (const auto& l : m_annotationLines) {
        if (l.a < 0 || l.a >= static_cast<int>(m_annotations.size()) ||
            l.b < 0 || l.b >= static_cast<int>(m_annotations.size()))
            continue;
        const Vec3& pa = m_annotations[static_cast<size_t>(l.a)].pos;
        const Vec3& pb = m_annotations[static_cast<size_t>(l.b)].pos;
        const float v[12] = {
            static_cast<float>(pa.x), static_cast<float>(pa.y), static_cast<float>(pa.z),
            c[0], c[1], c[2],
            static_cast<float>(pb.x), static_cast<float>(pb.y), static_cast<float>(pb.z),
            c[0], c[1], c[2],
        };
        pts.insert(pts.end(), std::begin(v), std::end(v));
    }
    m_annLineVertexCount = static_cast<int>(pts.size()) / 6;
    m_gl->glBindVertexArray(m_annLineVao);
    m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_annLineVbo);
    m_gl->glBufferData(GL_ARRAY_BUFFER,
                       static_cast<GLsizeiptr>(pts.size() * sizeof(float)),
                       pts.empty() ? nullptr : pts.data(), GL_DYNAMIC_DRAW);
    m_gl->glBindVertexArray(0);
}

void AvatarView3D::setAnnotations(const std::vector<AnnotationPoint>& points)
{
    m_annotations = points;
    update();
}

void AvatarView3D::paintGL()
{
    if (!m_glReady || !m_program) {
        renderFallback();
        return;
    }
    if (m_meshDirty) uploadMesh();
    if (m_measureDirty) {
        rebuildMeasureLines();
        uploadMeasureLines();
        refreshMeasureLabels();
        m_measureDirty = false;
    }

    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glClearColor(0.16f, 0.17f, 0.20f, 1.f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m_indexCount <= 0) return;

    // 相机矩阵（轨道参数 → 视图/投影）
    Mat4 view, proj, mvp;
    computeViewProjection(view, proj, mvp);

    // 法线矩阵：视图矩阵旋转部分（模型矩阵为单位阵）
    float normalM[9];
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            normalM[c * 3 + r] = view.m[c * 4 + r];

    m_gl->glUseProgram(m_program);
    m_gl->glUniformMatrix4fv(m_uMvp, 1, GL_FALSE, mvp.m);
    m_gl->glUniformMatrix3fv(m_uNormal, 1, GL_FALSE, normalM);
    m_gl->glUniformMatrix4fv(m_uView, 1, GL_FALSE, view.m);

    // 地面先画（深度写入，模型覆盖其上）；alpha 渐隐边缘融入背景
    if (m_groundProgram) {
        m_gl->glUseProgram(m_groundProgram);
        m_gl->glUniformMatrix4fv(m_groundU_mvp, 1, GL_FALSE, mvp.m);
        m_gl->glUniform1f(m_groundU_extent, m_groundExtent);
        m_gl->glUniform2f(m_groundU_center, static_cast<float>(m_target.x),
                          static_cast<float>(m_target.z));
        m_gl->glEnable(GL_BLEND);
        m_gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_gl->glBindVertexArray(m_groundVao);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
        m_gl->glBindVertexArray(0);
        m_gl->glDisable(GL_BLEND);
        m_gl->glUseProgram(m_program);
    }

    // 主光斜上方（视图空间）：棚拍 key 光 30-45° 仰角，偏暖白
    m_gl->glUniform3f(m_uLightDir, 0.35f, 0.65f, 0.55f);
    // 展示肤色：浅暖人台色（提亮降饱和，柔光棚拍下不偏红不偏灰）
    m_gl->glUniform3f(m_uColor, 0.94f, 0.82f, 0.72f);
    m_gl->glUniform1f(m_uAmbient, 0.62f);
    m_gl->glUniform1f(m_uShininess, 24.f);
    m_gl->glUniform1i(m_uUseMatcap, m_hasMatcap ? 1 : 0);
    if (m_hasMatcap) {
        m_gl->glActiveTexture(GL_TEXTURE0);
        m_gl->glBindTexture(GL_TEXTURE_2D, m_matcapTex);
        m_gl->glUniform1i(m_uMatcap, 0);
    }
    m_gl->glBindVertexArray(m_vao);
    m_gl->glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    m_gl->glBindVertexArray(0);

    // 测量线：开启深度测试（与标注点同一规则）——测量线贴在体表、沿法线略
    // 外移，转到背面时背后的线被模型自然遮挡消失。不写深度（避免污染后续绘制）。
    if (m_lineProgram && m_lineVertexCount > 0) {
        m_gl->glUseProgram(m_lineProgram);
        m_gl->glUniformMatrix4fv(m_lineU_mvp, 1, GL_FALSE, mvp.m);
        m_gl->glDepthMask(GL_FALSE); // 测量线不写深度，仅做深度测试（不遮挡后画的其余线）
        m_gl->glBindVertexArray(m_lineVao);
        m_gl->glDrawArrays(GL_LINES, 0, m_lineVertexCount);
        m_gl->glBindVertexArray(0);
        m_gl->glDepthMask(GL_TRUE);
        m_gl->glUseProgram(m_program);
    }

    // 测量值标签（Qt 文本覆盖）。用标签处法线做朝向相机判定：转到背面时
    // 法线背离相机，标签随测量线一起隐藏（与标注点名称同一规则）。
    // 若名称显示开关关闭，则不画任何标签。
    if (m_showMeasureLabels && !m_measureInfo.empty()) {
        const double yaw = m_yawDeg * M_PI / 180.0;
        const double pitch = m_pitchDeg * M_PI / 180.0;
        const double cp = std::cos(pitch), sp = std::sin(pitch);
        const double cy = std::cos(yaw), sy = std::sin(yaw);
        const Vec3 eye = m_target + Vec3(m_dist * cp * sy, m_dist * sp, m_dist * cp * cy);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        const int w = width(), h = height();
        for (const auto& [key, info] : m_measureInfo) {
            if (info.cm <= 0.0)
                continue;
            // 朝向相机判定：法线朝相机才显示，背面隐藏。
            if (info.normal.length() > 1e-9 && info.normal.dot(eye - info.labelPos) <= 0.0)
                continue;
            const QPointF sp = projectToScreen(mvp, info.labelPos, w, h);
            if (sp.x() < -1e8)
                continue;
            const QString text =
                QString::fromStdString(displayName(key)) + QStringLiteral("\n") +
                QStringLiteral("%1 cm").arg(info.cm, 0, 'f', 1);
            const QRectF r(sp.x() + 6.0, sp.y() - 14.0, 200.0, 40.0);
            p.setPen(QColor(0, 0, 0, 180));
            p.drawText(r.translated(1, 1), Qt::AlignLeft | Qt::AlignTop, text);
            p.setPen(info.color);
            p.drawText(r, Qt::AlignLeft | Qt::AlignTop, text);
        }
    }

    // 标注连线：GL_LINES（画线工具），贴体表 + 深度测试自然遮挡（同测量线规则）
    if (m_lineProgram && !m_annotationLines.empty()) {
        uploadAnnotationLines();
        m_gl->glUseProgram(m_lineProgram);
        m_gl->glUniformMatrix4fv(m_lineU_mvp, 1, GL_FALSE, mvp.m);
        m_gl->glDepthMask(GL_FALSE);
        m_gl->glBindVertexArray(m_annLineVao);
        m_gl->glDrawArrays(GL_LINES, 0, m_annLineVertexCount);
        m_gl->glBindVertexArray(0);
        m_gl->glDepthMask(GL_TRUE);
        m_gl->glUseProgram(m_program);
    }

    // 标注点圆点：真 3D GL_POINTS。贴在体表、深度测试自然遮挡——转到模型
    // 背面时圆点位于模型之后，被深度缓冲自动挡住消失（零软件遮挡判断）。
    if (m_annProgram && !m_annotations.empty()) {
        uploadAnnotationPoints();
        m_gl->glEnable(GL_PROGRAM_POINT_SIZE);
        m_gl->glUseProgram(m_annProgram);
        m_gl->glUniformMatrix4fv(m_annU_mvp, 1, GL_FALSE, mvp.m);
        // 圆点屏幕像素直径（固定 UI 大小，与原 QPainter drawEllipse 半径 5px 一致）
        m_gl->glUniform1f(m_annU_scale, 10.0f);
        m_gl->glBindVertexArray(m_annVao);
        m_gl->glDrawArrays(GL_POINTS, 0, m_annPointCount);
        m_gl->glBindVertexArray(0);
        m_gl->glDisable(GL_PROGRAM_POINT_SIZE);
        m_gl->glUseProgram(m_program);
    }

    // 标注点名称标签（QPainter，文字清晰）。显隐用相机相关朝向判断：标注点
    // 所在表面法线朝向相机才显示，转到背面（法线背离相机）则随圆点一起隐藏。
    if (!m_annotations.empty()) {
        // 相机眼位（与 computeViewProjection 一致，用于朝向判断）
        const double yaw = m_yawDeg * M_PI / 180.0;
        const double pitch = m_pitchDeg * M_PI / 180.0;
        const double cp = std::cos(pitch), sp = std::sin(pitch);
        const double cy = std::cos(yaw), sy = std::sin(yaw);
        const Vec3 eye = m_target + Vec3(m_dist * cp * sy, m_dist * sp, m_dist * cp * cy);
        const int w = width(), h = height();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9));
        int index = 1;
        for (const auto& a : m_annotations) {
            const QPointF sp = projectToScreen(mvp, a.pos, w, h);
            if (sp.x() < -1e8) {
                ++index;
                continue;
            }
            // 法线朝向：n·(eye-pos) > 0 => 面朝相机（当前视角可见）
            Vec3 n{0, 0, 0};
            if (a.vertex >= 0 && a.vertex < static_cast<int>(m_normals.size()))
                n = m_normals[static_cast<size_t>(a.vertex)];
            if (n.length() > 1e-9 && n.dot(eye - a.pos) <= 0.0) {
                ++index; // 背面：与圆点一起隐藏
                continue;
            }
            const QString text =
                QStringLiteral("%1 %2").arg(index).arg(QString::fromStdString(a.name));
            const QRectF r(sp.x() + 8.0, sp.y() - 10.0, 240.0, 18.0);
            p.setPen(QColor(0, 0, 0, 180));
            p.drawText(r.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, text);
            p.setPen(QColor(255, 220, 60));
            p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, text);
            ++index;
        }
    }

    // 悬停投影圆圈（标注模式下，指示当前表面交点，消除视差）
    if (m_hoverValid) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF sp = projectToScreen(mvp, m_hoverPos, width(), height());
        if (sp.x() > -1e8) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(0, 225, 255), 1.5));
            p.drawEllipse(sp, 7.0, 7.0);
        }
    }

    // Ctrl 悬停吸附预览：命中测量线时，在吸附目标处画醒目高亮环 + 线名，
    // 明确告知「Ctrl 已被识别、将吸附到该测量线」。
    if (m_snapPreviewValid && m_annotationMode) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF sp = projectToScreen(mvp, m_snapPreviewPos, width(), height());
        if (sp.x() > -1e8) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 235, 90), 2.0)); // 亮黄粗环
            p.drawEllipse(sp, 11.0, 11.0);
            p.setPen(QPen(QColor(255, 235, 90), 1.0));
            p.drawLine(QPointF(sp.x() - 15, sp.y()), QPointF(sp.x() - 9, sp.y()));
            p.drawLine(QPointF(sp.x() + 9, sp.y()), QPointF(sp.x() + 15, sp.y()));
            p.drawLine(QPointF(sp.x(), sp.y() - 15), QPointF(sp.x(), sp.y() - 9));
            p.drawLine(QPointF(sp.x(), sp.y() + 9), QPointF(sp.x(), sp.y() + 15));
            if (!m_snapPreviewKey.empty()) {
                const QString name = QString::fromStdString(displayName(m_snapPreviewKey));
                const QRectF r(sp.x() + 16.0, sp.y() - 9.0, 220.0, 18.0);
                p.setPen(QColor(0, 0, 0, 180));
                p.drawText(r.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("水平面：%1").arg(name));
                p.setPen(QColor(255, 235, 90));
                p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("水平面：%1").arg(name));
            }
        }
    }
}

void AvatarView3D::renderFallback()
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x2A, 0x2B, 0x30));
    p.setPen(QColor(0xCC, 0xCC, 0xCC));
    p.drawText(rect(), Qt::AlignCenter,
               QStringLiteral("3D 预览不可用：\n当前环境无 OpenGL 3.3 上下文"));
}

void AvatarView3D::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        if (m_lineMode) {
            // 画线模式：点标注点——第一次选起点，第二次选终点连线（一次一条）
            Mat4 view, proj, mvp;
            computeViewProjection(view, proj, mvp);
            int hitIdx = -1;
            double bestDist = kAnnotationGrabRadius;
            for (int i = 0; i < static_cast<int>(m_annotations.size()); ++i) {
                const auto& a = m_annotations[static_cast<size_t>(i)];
                const QPointF sp = projectToScreen(mvp, a.pos, width(), height());
                if (sp.x() < -1e8)
                    continue;
                const double d = std::hypot(sp.x() - e->pos().x(), sp.y() - e->pos().y());
                if (d < bestDist) {
                    bestDist = d;
                    hitIdx = i;
                }
            }
            if (hitIdx >= 0) {
                if (m_lineStartIdx < 0) {
                    m_lineStartIdx = hitIdx; // 选起点
                } else if (hitIdx != m_lineStartIdx) {
                    m_annotationLines.push_back({m_lineStartIdx, hitIdx});
                    emit annotationLineDrawn(m_lineStartIdx, hitIdx);
                    m_lineStartIdx = -1; // 完成一条，重置
                }
                update();
            }
            e->accept();
            return;
        }
        if (m_annotationMode) {
            // 优先判断是否点中已有标注点（屏幕距离 < 抓取半径 → 拖动调整，而非放新点）
            Mat4 view, proj, mvp;
            computeViewProjection(view, proj, mvp);
            const int w = width(), h = height();
            int hitIdx = -1;
            double bestDist = kAnnotationGrabRadius;
            for (int i = 0; i < static_cast<int>(m_annotations.size()); ++i) {
                const auto& a = m_annotations[static_cast<size_t>(i)];
                const QPointF sp = projectToScreen(mvp, a.pos, w, h);
                if (sp.x() < -1e8)
                    continue;
                const double d = std::hypot(sp.x() - e->pos().x(), sp.y() - e->pos().y());
                if (d < bestDist) {
                    bestDist = d;
                    hitIdx = i;
                }
            }
            if (hitIdx >= 0) {
                m_dragAnnotation = hitIdx;
                m_hoverPos = m_annotations[static_cast<size_t>(hitIdx)].pos;
                m_hoverValid = true;
                m_lastPos = e->pos();
                e->accept();
                return;
            }
            Vec3 point;
            int vertex = -1;
            int tri = -1;
            double u = 0.0, v = 0.0;
            if (pickSurfacePoint(e->pos(), point, &vertex, &tri, &u, &v)) {
                std::string heightKey;
                QString snapName;
                if (e->modifiers() & Qt::ControlModifier) {
                    // Ctrl：借用最近可见测量线的水平面——只约束高度（Y），
                    // X/Z 保持点击位置（自由），不再吸附到线的轮廓点。
                    double H = 0.0;
                    if (findNearestMeasurePlane(point, heightKey, H)) {
                        point.y = H;
                        snapName = QString::fromStdString(displayName(heightKey));
                    } else {
                        snapName = QStringLiteral("（未命中测量线）");
                    }
                }
                emit annotationPicked(point, vertex, tri, u, v, heightKey, snapName);
            }
            e->accept();
            return;
        }
        m_dragging = true;
        m_lastPos = e->pos();
        e->accept();
    } else if (e->button() == Qt::RightButton) {
        // 右键环绕：标注模式下左键被放点占用，用右键旋转视角
        m_dragging = true;
        m_lastPos = e->pos();
        e->accept();
    } else if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPos = e->pos();
        e->accept();
    }
}

void AvatarView3D::mouseMoveEvent(QMouseEvent* e)
{
    const QPoint delta = e->pos() - m_lastPos;
    m_lastPos = e->pos();

    if (m_dragging) {
        m_yawDeg -= delta.x() * 0.45;
        m_pitchDeg += delta.y() * 0.45;
        m_pitchDeg = std::clamp(m_pitchDeg, -static_cast<double>(kPitchClamp),
                                static_cast<double>(kPitchClamp));
        update();
        e->accept();
        return;
    }
    if (m_panning) {
        // 平移：水平方向沿相机 right 在水平面（XZ）的投影，垂直方向沿世界 Y。
        // 避免斜视角下沿倾斜屏幕平面平移导致模型"斜着飘"（高度被意外改变）。
        const double fovY = kFovDeg * M_PI / 180.0;
        const double worldPerPixel =
            2.0 * m_dist * std::tan(fovY * 0.5) / std::max(height(), 1);
        Mat4 view, proj, mvp;
        computeViewProjection(view, proj, mvp);
        const Vec3 right(view.m[0], view.m[1], view.m[2]);
        Vec3 horiz(right.x, 0.0, right.z);
        const double hl = horiz.length();
        if (hl > 1e-9)
            horiz = horiz / hl;
        m_target = m_target - horiz * (delta.x() * worldPerPixel);
        m_target.y += delta.y() * worldPerPixel; // 屏幕向下拖 = 目标抬升 = 模型下移
        update();
        e->accept();
        return;
    }
    if (m_dragAnnotation >= 0) {
        // 拖动调整已有标注点：标注点实时跟随光标表面交点。拖动即解除「借用水平面」
        // 约束、重新做贴肤重心绑定（否则 morph 刷新时会被高度约束拉回原测量线高度）。
        Vec3 point;
        int vertex = -1;
        int tri = -1;
        double u = 0.0, v = 0.0;
        if (pickSurfacePoint(e->pos(), point, &vertex, &tri, &u, &v)) {
            m_hoverPos = point;
            m_hoverValid = true;
            auto& a = m_annotations[static_cast<size_t>(m_dragAnnotation)];
            a.pos = point;
            a.vertex = vertex;
            a.tri = tri;
            a.bcU = u;
            a.bcV = v;
            a.heightKey.clear();
        }
        update();
        e->accept();
        return;
    }
    if (m_annotationMode) {
        // 悬停投影：非拖动时更新悬停表面交点，绘制圆圈消除视差
        Vec3 point;
        const bool hit = pickSurfacePoint(e->pos(), point);
        const bool changed = (hit != m_hoverValid) || (hit && !(point == m_hoverPos));
        if (hit) {
            m_hoverPos = point;
            m_hoverValid = true;
        } else {
            m_hoverValid = false;
        }
        // Ctrl 悬停「借用水平面」预览：命中可见测量线时，在点击 X/Z、该线高度处
        // 画高亮环 + 线名，明确 Ctrl 是否被识别、将借用哪条线的水平面。
        bool previewValid = false;
        if (hit && (e->modifiers() & Qt::ControlModifier)) {
            std::string key;
            double H = 0.0;
            previewValid = findNearestMeasurePlane(point, key, H);
            m_snapPreviewPos = Vec3{point.x, H, point.z};
            m_snapPreviewKey = previewValid ? key : std::string{};
        }
        if (changed || (previewValid != m_snapPreviewValid)) {
            m_snapPreviewValid = previewValid;
            update();
        }
        e->accept();
    }
}

void AvatarView3D::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton || e->button() == Qt::RightButton) {
        if (m_dragAnnotation >= 0 && e->button() == Qt::LeftButton) {
            // 结束拖动：通知面板刷新位置
            const int idx = m_dragAnnotation;
            const auto& a = (idx < static_cast<int>(m_annotations.size()))
                                ? m_annotations[static_cast<size_t>(idx)]
                                : AnnotationPoint{};
            m_dragAnnotation = -1;
            emit annotationMoved(idx, a.pos, a.vertex);
        }
        m_dragging = false;
        e->accept();
    } else if (e->button() == Qt::MiddleButton) {
        m_panning = false;
        e->accept();
    }
}

void AvatarView3D::leaveEvent(QEvent* e)
{
    if (m_hoverValid || m_snapPreviewValid) {
        m_hoverValid = false;
        m_snapPreviewValid = false;
        m_snapPreviewKey.clear();
        update();
    }
    QOpenGLWidget::leaveEvent(e);
}

void AvatarView3D::wheelEvent(QWheelEvent* e)
{
    const double factor = (e->angleDelta().y() > 0) ? 0.88 : 1.14;
    m_dist = std::clamp(m_dist * factor, 0.5, 2000.0);
    update();
    e->accept();
}

void AvatarView3D::setViewPreset(ViewPreset preset)
{
    // 模型脸朝 +Z（鼻尖 Z 最大）：eye = target + (dist*cp*sy, dist*sp, dist*cp*cy)
    // Front/Back 即相机站在 +Z/-Z；Left/Right 按模特左右手侧（右手=+X）
    switch (preset) {
    case ViewPreset::Front:        m_yawDeg = 0.0;   m_pitchDeg = 0.0;  break;
    case ViewPreset::Back:         m_yawDeg = 180.0; m_pitchDeg = 0.0;  break;
    case ViewPreset::Left:         m_yawDeg = -90.0; m_pitchDeg = 0.0;  break;
    case ViewPreset::Right:        m_yawDeg = 90.0;  m_pitchDeg = 0.0;  break;
    case ViewPreset::Top:          m_yawDeg = 0.0;   m_pitchDeg = 89.0; break;
    case ViewPreset::Bottom:       m_yawDeg = 0.0;   m_pitchDeg = -89.0; break;
    case ViewPreset::ThreeQuarter: m_yawDeg = 32.0;  m_pitchDeg = 14.0; break;
    }
    m_pitchDeg = std::clamp(m_pitchDeg, -static_cast<double>(kPitchClamp),
                            static_cast<double>(kPitchClamp));
    update();
}

void AvatarView3D::resizeEvent(QResizeEvent* e)
{
    QOpenGLWidget::resizeEvent(e);
    if (m_viewBar)
        m_viewBar->move(8, 8);
    if (m_measureBar) {
        m_measureBar->setFixedHeight(std::max(60, height() - 16));
        m_measureBar->move(width() - m_measureBar->width() - 8, 8);
    }
}

} // namespace cad::avatar
