#include "SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>

#include "ElaCheckBox.h"
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QSignalBlocker>
#include <QComboBox>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "canvas/CanvasScene.h"
#include "PointRefEdit.h"

namespace cad::tools {

namespace {

/// Format an angle in degrees for display: integers render without a trailing
/// ".0" (e.g. 22 -> "22", 22.5 -> "22.5").
QString formatAngleDeg(double deg)
{
    QString s = QString::number(deg, 'f', 1);
    if (s.endsWith(QLatin1String(".0")))
        s.chop(2);
    return s;
}

// ── 角度显示约定（2026-08 v3 定稿，用户拍板）────────────────────────────
// 与 ToolRotate.cpp 同步：存储域 α ∈ [0, 360°)；连接线显示 = 带符号折角
// [−180°, +180°]（折叠 0 / 垂直 ±90 / 开平 ±180，符号 = 折向）；自由线
// 绝对角度 = 0~360° 逆时针为正（行业默认，AutoCAD 同）。
double signedFoldDeg(double alphaDeg)
{
    double a = std::fmod(alphaDeg, 360.0);
    if (a < 0.0) a += 360.0;
    return a > 180.0 ? a - 360.0 : a;
}
double alphaFromSignedFold(double foldDeg)
{
    double a = std::fmod(foldDeg, 360.0);
    if (a < 0.0) a += 360.0;
    return a;
}

/// Cross-layer badge text for an attachment whose follower and leader live on
/// different layer kinds (合法方向: aux follower → working leader): returns
/// "→ <leader 所在层名>"; empty for same-layer attachments.
QString crossLayerBadge(cad::param::ParamDocument* doc,
                        const cad::param::Attachment& att)
{
    if (!doc) return QString();
    const cad::param::Block* from = doc->findBlock(att.fromBlockId);
    const cad::param::Block* to   = doc->findBlock(att.toBlockId);
    if (!from || !to) return QString();
    if (doc->isAuxBlock(*from) == doc->isAuxBlock(*to)) return QString();
    const auto* leaderLayer = doc->layerById(to->layer);
    if (!leaderLayer)
        return QString();
    return QStringLiteral("\u2192 ")  // → <层名>
         + leaderLayer->name;
}

/// Toast text when a freshly established attachment crosses layers:
/// "已建立跨层连接（测量层→操作层1）" (real layer names). Empty when
/// same-layer or blocks are gone.
QString crossLayerToast(cad::param::ParamDocument* doc,
                        const cad::param::Block& from,
                        const cad::param::Block& to)
{
    if (!doc) return QString();
    if (doc->isAuxBlock(from) == doc->isAuxBlock(to)) return QString();
    auto name = [doc](const QUuid& layerId) {
        const auto* l = doc->layerById(layerId);
        return l ? l->name : QStringLiteral("?");
    };
    return QString::fromUtf8("\xe5\xb7\xb2\xe5\xbb\xba\xe7\xab\x8b"
                             "\xe8\xb7\xa8\xe5\xb1\x82\xe8\xbf\x9e\xe6\x8e\xa5"
                             "\xef\xbc\x88%1\u2192%2\xef\xbc\x89")  // 已建立跨层连接（%1→%2）
        .arg(name(from.layer), name(to.layer));
}

} // namespace

SegmentConnectionCard::SegmentConnectionCard(cad::param::ParamDocument* doc,
                                             CanvasScene* scene, QWidget* parent)
    : ElaScrollPageArea(parent)
    , m_doc(doc)
    , m_scene(scene)
{
    // ElaScrollPageArea's constructor hard-codes setFixedHeight(75); lift it
    // so the card sizes itself from its content layout.
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    auto* angleLayout = new QVBoxLayout(this);
    angleLayout->setContentsMargins(10, 8, 10, 10);
    angleLayout->setSpacing(6);

    m_titleLabel = new ElaText(QString::fromUtf8("绝对角度 · 连接"), 13, this);
    m_titleLabel->setStyleSheet("font-weight:600;");
    angleLayout->addWidget(m_titleLabel);

    // Connected-state row: 基准线 label + 指向点 ref (editable).
    m_connRow = new QWidget(this);
    auto* connLayout = new QHBoxLayout(m_connRow);
    connLayout->setContentsMargins(0, 0, 0, 0);
    connLayout->addWidget(new ElaText(QString::fromUtf8("\u57fa\u51c6\u7ebf:"), 13, m_connRow));  // 基准线:
    m_lblLeaderRef = new ElaText(QString(), 13, m_connRow);
    m_lblLeaderRef->setStyleSheet("font-size:12px;");
    connLayout->addWidget(m_lblLeaderRef, 1);
    m_lblLayerBadge = new ElaText(QString(), 13, m_connRow);
    // Stylesheet set ONCE at construction (每帧 setStyleSheet 会致卡顿).
    m_lblLayerBadge->setStyleSheet(QStringLiteral(
        "color:#8e44ad; background:#f3e8ff; border-radius:3px;"
        "padding:0 4px; font-size:11px;"));
    m_lblLayerBadge->setToolTip(QString::fromUtf8(
        "\u8de8\u5c42\u8fde\u63a5\uff1a\u57fa\u51c6\u7ebf\u4f4d\u4e8e\u53e6\u4e00\u56fe\u5c42"));  // 跨层连接：基准线位于另一图层
    m_lblLayerBadge->setVisible(false);
    connLayout->addWidget(m_lblLayerBadge);
    // 拆开保留角度 badge ("仅角度 · 位置自由"): shown while the connection is
    // angle-only (position constraint released, angle following kept).
    m_lblAngleOnlyBadge = new ElaText(QString(), 13, m_connRow);
    m_lblAngleOnlyBadge->setStyleSheet(QStringLiteral(
        "color:#0F766E; background:#E6F4F2; border-radius:3px;"
        "padding:0 4px; font-size:11px;"));
    m_lblAngleOnlyBadge->setToolTip(QString::fromUtf8(
        "\u62c6\u5f00\u4fdd\u7559\u89d2\u5ea6\uff1a\u4f4d\u7f6e\u5438\u9644\u5df2\u89e3\u9664"
        "\uff08\u7ebf\u6bb5\u53ef\u81ea\u7531\u79fb\u52a8\uff09\uff0c\u4f46\u89d2\u5ea6\u4ecd"
        "\u8ddf\u968f\u57fa\u51c6\u7ebf\uff1b\u57fa\u51c6\u7ebf\u65cb\u8f6c\u65f6\u672c\u7ebf"
        "\u8ddf\u7740\u8f6c\u3002\u518d\u6b21\u52fe\u9009\u201c\u8ddf\u968f\u5bbf\u4e3b\u201d"
        "\u6062\u590d\u4f4d\u7f6e\u5438\u9644\u3002"));  // 拆开保留角度：位置吸附已解除（线段可自由移动），但角度仍跟随基准线；基准线旋转时本线跟着转。再次勾选“跟随宿主”恢复位置吸附。
    m_lblAngleOnlyBadge->setVisible(false);
    connLayout->addWidget(m_lblAngleOnlyBadge);
    connLayout->addWidget(new ElaText(QString::fromUtf8("\u6307\u5411\u70b9:"), 13, m_connRow));  // 指向点:
    m_refConnPoint = new PointRefEdit(m_doc, m_connRow);
    m_refConnPoint->setMaximumWidth(150);
    connLayout->addWidget(m_refConnPoint);
    m_btnClearConn = new ElaPushButton(QString::fromUtf8("\u6e05\u9664"), m_connRow);  // 清除
    m_btnClearConn->setToolTip(QString::fromUtf8(
        "\u62c6\u9664\u8fde\u63a5\uff08\u5220\u9664\u9644\u7740\uff09\uff0c\u7ebf\u6bb5\u6062\u590d\u4e3a\u81ea\u7531\u72b6\u6001"));  // 拆除连接（删除附着），线段恢复为自由状态
    m_btnClearConn->setCursor(Qt::PointingHandCursor);
    connLayout->addWidget(m_btnClearConn);
    angleLayout->addWidget(m_connRow);

    // Free-state row: 跟随宿主 input (typed P number establishes the follow).
    m_freeConnRow = new QWidget(this);
    auto* freeConnLayout = new QHBoxLayout(m_freeConnRow);
    freeConnLayout->setContentsMargins(0, 0, 0, 0);
    auto* lblConnectTo = new ElaText(QString::fromUtf8("\u8ddf\u968f\u5bbf\u4e3b:"), 13, m_freeConnRow);  // 跟随宿主:
    lblConnectTo->setToolTip(QString::fromUtf8(
        "\u8f93\u5165\u76ee\u6807\u70b9 P \u7f16\u53f7\u5e76\u56de\u8f66\uff0c\u5373\u53ef\u5c06\u672c\u7ebf\u8d34\u9644\u5230\u8be5\u70b9\uff08\u5efa\u7acb\u8ddf\u968f\u89d2\u5ea6\u8fde\u63a5\uff09\uff1b"
        "\u4e0b\u65b9\u201c\u8ddf\u968f\u5bbf\u4e3b\u201d\u590d\u9009\u6846\u53d6\u6d88\u52fe\u9009 = \u62c6\u5f00\uff08\u4fdd\u7559\u89d2\u5ea6\u8ddf\u968f\uff0c\u4f4d\u7f6e\u81ea\u7531\uff09\uff1b"
        "\u5f7b\u5e95\u65ad\u5f00\u7528\u300c\u6e05\u9664\u300d"));
    // 输入目标点 P 编号并回车，即可将本线贴附到该点（建立跟随角度连接）；下方“跟随宿主”复选框取消勾选 = 拆开（保留角度跟随，位置自由）；彻底断开用「清除」
    freeConnLayout->addWidget(lblConnectTo);
    m_refConnectTo = new PointRefEdit(m_doc, m_freeConnRow);
    m_refConnectTo->setMaximumWidth(150);
    freeConnLayout->addWidget(m_refConnectTo);
    freeConnLayout->addStretch();
    angleLayout->addWidget(m_freeConnRow);

    // Angle value row: [mode] [caption] [fx] [input] [world-angle readout]
    auto* angleRow = new QHBoxLayout();
    angleRow->setSpacing(6);
    m_btnAngleMode = new ElaPushButton(this);
    m_btnAngleMode->setFixedSize(26, 22);
    m_btnAngleMode->setCursor(Qt::PointingHandCursor);
    m_btnAngleMode->setToolTip(QString::fromUtf8("\u5207\u6362\u89d2\u5ea6/\u5f27\u957f\u6a21\u5f0f"));  // 切换角度/弧长模式
    m_btnAngleMode->setVisible(false);
    angleRow->addWidget(m_btnAngleMode);
    m_lblAngleCaption = new ElaText(QString::fromUtf8("\u89d2\u5ea6(\u00b0):"), 13, this);  // 角度(°):
    angleRow->addWidget(m_lblAngleCaption);
    m_lblFxAngle = new ElaText(QStringLiteral("<i style='color:#2F6FED;'>fx</i>"), 13, this);
    m_lblFxAngle->setVisible(false);
    m_lblFxAngle->setFixedWidth(18);
    angleRow->addWidget(m_lblFxAngle);
    m_editAngle = new ElaLineEdit(this);
    m_editAngle->setMinimumWidth(120);
    m_editAngle->setPlaceholderText(
        QString::fromUtf8("\u6570\u503c(\u00b0)\u6216\u516c\u5f0f"));  // 数值(°)或公式
    m_editAngle->setToolTip(QString::fromUtf8(
        "\u81ea\u7531\u7ebf\uff1a\u4e16\u754c\u89d2\u5ea6\uff1b\u8ddf\u968f\u7ebf\uff1a\u6784\u9020\u89d2\u3002\u9006\u65f6\u9488\u4e3a\u6b63\uff0c\u56de\u8f66\u786e\u8ba4"));
    angleRow->addWidget(m_editAngle, 1);
    // Formula current-value readout: when the input holds a formula (fx),
    // show the evaluated follower angle / arc length so the value is not
    // hidden behind the expression (表达式不直观 → 显示当前计算值).
    m_lblFollowValue = new ElaText(QString(), 13, this);
    m_lblFollowValue->setObjectName(QStringLiteral("followValueLabel"));
    m_lblFollowValue->setStyleSheet(QStringLiteral("font-size:11px;"));
    m_lblFollowValue->setVisible(false);
    angleRow->addWidget(m_lblFollowValue);
    m_lblWorldAngle = new ElaText(QString(), 13, this);
    m_lblWorldAngle->setStyleSheet(QStringLiteral("font-size:11px;"));
    m_lblWorldAngle->setVisible(false);
    angleRow->addWidget(m_lblWorldAngle);
    angleLayout->addLayout(angleRow);

    // Follow-host checkbox (ALL lines): checked = start point follows the
    // connection target; unchecking detaches (visible via refreshCard()).
    m_chkFollowHost = new ElaCheckBox(this);
    
    angleLayout->addWidget(m_chkFollowHost);

    // 省道线 row (用户拍板 2026-08): 起点 A + 偏移点 B（只读）+ 反算跟随
    // 角度（灰只读）+ 偏移 d / 角度 β 编辑。只有 block->isDart() 时可见。
    m_dartRow = new QWidget(this);
    auto* dartLayout = new QVBoxLayout(m_dartRow);
    dartLayout->setContentsMargins(0, 0, 0, 0);
    dartLayout->setSpacing(4);
    auto* dartTop = new QHBoxLayout();
    dartTop->setSpacing(6);
    dartTop->addWidget(new ElaText(QString::fromUtf8("\u8d77\u70b9 A:"), 13, m_dartRow));  // 起点 A:
    m_dartStartRef = new ElaText(QString(), 13, m_dartRow);
    m_dartStartRef->setStyleSheet("font-size:12px;");
    dartTop->addWidget(m_dartStartRef, 1);
    dartTop->addWidget(new ElaText(QString::fromUtf8("\u504f\u79fb\u70b9 B:"), 13, m_dartRow));  // 偏移点 B:
    m_dartRefLabel = new ElaText(QString(), 13, m_dartRow);
    m_dartRefLabel->setStyleSheet("font-size:12px;");
    m_dartRefLabel->setToolTip(QString::fromUtf8(
        "\u504f\u79fb\u70b9 B \u6240\u5728\u7ebf\u6bb5\u5373\u89d2\u5ea6\u57fa\u51c6\uff1a"
        "\u7ebf\u6bb5\u65cb\u8f6c\u65f6\u672c\u7ebf\u8ddf\u7740\u8f6c\u3002"
        "\u7701\u9053\u7ebf\u5355\u65b9\u9762\u6302\u9760\uff0cB \u6240\u5728\u7ebf\u6bb5\u4e0d\u663e\u793a\u4e0d\u4fee\u6539\u3002"));
    // 偏移点 B 所在线段即角度基准：线段旋转时本线跟着转。省道线单方面挂靠，B 所在线段不显示不修改。
    dartTop->addWidget(m_dartRefLabel, 1);
    dartLayout->addLayout(dartTop);
    auto* dartMid = new QHBoxLayout();
    dartMid->setSpacing(6);
    auto* lblFold = new ElaText(
        QString::fromUtf8("\u8ddf\u968f\u89d2\u5ea6\uff08\u53cd\u7b97\uff09:"), 13, m_dartRow);  // 跟随角度（反算）:
    lblFold->setToolTip(QString::fromUtf8(
        "\u7ebf\u65b9\u5411\u76f8\u5bf9 B \u6240\u5728\u7ebf\u6bb5\u65b9\u5411\u7684\u89d2\u5ea6\uff08"
        "\u7531 A/B/d/\u03b2 \u81ea\u52a8\u53cd\u7b97\uff0c\u53ea\u8bfb\u4e0d\u53ef\u7f16\u8f91\uff09\u3002"));
    // 线方向相对 B 所在线段方向的角度（由 A/B/d/β 自动反算，只读不可编辑）。
    dartMid->addWidget(lblFold);
    m_dartFoldLabel = new ElaText(QString(), 13, m_dartRow);
    m_dartFoldLabel->setStyleSheet(
        "color:#8a8a8a; font-size:12px; text-decoration:line-through;");
    m_dartFoldLabel->setToolTip(QString::fromUtf8(
        "\u53cd\u7b97\u89d2\u5ea6\uff08\u6700\u7ec8\u884c\u4e3a\uff09\uff1a"
        "\u7ebf\u65b9\u5411\u76f8\u5bf9\u504f\u79fb\u70b9\u7ebf\u6bb5\u65b9\u5411\u7684\u5e26\u7b26\u53f7\u6298\u89d2\uff0c"
        "\u4e0d\u53ef\u76f4\u63a5\u7f16\u8f91\u2014\u2014\u6539\u2192\u201c\u89d2\u5ea6 \u03b2\u201d\u624d\u80fd\u6539\u53d8\u5b83\u3002"));
    // 反算角度（最终行为）：线方向相对偏移点线段方向的带符号折角，不可直接编辑——改→“角度 β”才能改变它。
    dartMid->addWidget(m_dartFoldLabel, 1);
    dartLayout->addLayout(dartMid);
    auto* dartParams = new QHBoxLayout();
    dartParams->setSpacing(6);
    dartParams->addWidget(new ElaText(QString::fromUtf8("\u504f\u79fb d:"), 13, m_dartRow));  // 偏移 d:
    m_dartOffsetEdit = new ElaLineEdit(m_dartRow);
    m_dartOffsetEdit->setMinimumWidth(90);
    m_dartOffsetEdit->setPlaceholderText(QString::fromUtf8("mm / \u516c\u5f0f(cm)"));
    m_dartOffsetEdit->setToolTip(QString::fromUtf8(
        "\u7ec8\u70b9 E \u8ddd\u504f\u79fb\u70b9 B \u7684\u8ddd\u79bb d\uff08\u6cbf B \u6240\u5728\u7ebf\u6bb5\u65b9\u5411\u8f6c \u03b2 \u89d2\uff09\uff1b"
        "\u6b63\u8d1f\u5929\u7136\u51b3\u5b9a\u65b9\u5411\u3002\u7eaf\u6570\u503c = mm\uff1b\u516c\u5f0f = cm \u57df\u3002"));
    dartParams->addWidget(m_dartOffsetEdit);
    dartParams->addWidget(new ElaText(QString::fromUtf8("\u89d2\u5ea6 \u03b2:"), 13, m_dartRow));  // 角度 β:
    m_dartAngleEdit = new ElaLineEdit(m_dartRow);
    m_dartAngleEdit->setMinimumWidth(90);
    m_dartAngleEdit->setPlaceholderText(QString::fromUtf8("\u76f8\u5bf9\u7ebf\u6bb5(\u00b0)"));
    m_dartAngleEdit->setToolTip(QString::fromUtf8(
        "\u7ec8\u70b9\u76f8\u5bf9\u504f\u79fb\u70b9\u6240\u5728\u7ebf\u6bb5\u65b9\u5411\u7684\u8f6c\u89d2"
        "\uff08\u9ed8\u8ba4 90\u00b0\uff09\uff1b\u7ebf\u6bb5\u65cb\u8f6c\u65f6\u7ec8\u70b9\u8ddf\u7740\u8f6c\u3002"));
    dartParams->addWidget(m_dartAngleEdit);
    dartLayout->addLayout(dartParams);
    angleLayout->addWidget(m_dartRow);
    connect(m_dartOffsetEdit, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onDartOffsetEdited);
    connect(m_dartAngleEdit, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onDartAngleEdited);

    // 滑轨模式 row (抽屉式滑动, 用户拍板 2026-08): 全连接 / 沿线滑动 /
    // 垂直拉出. 进入滑轨后拖动跟随线只沿对应方向动, 角度跟随始终保留.
    // 只在连接态显示 (free 态与 m_connRow 一起隐藏).
    m_slideRow = new QWidget(this);
    auto* slideLayout = new QHBoxLayout(m_slideRow);
    slideLayout->setContentsMargins(0, 0, 0, 0);
    slideLayout->setSpacing(6);
    auto* lblSlide = new ElaText(QString::fromUtf8("\u6ed1\u8f68:"), 13, m_slideRow);  // 滑轨:
    lblSlide->setToolTip(QString::fromUtf8(
        "\u62bd\u5c49\u5f0f\u5355\u5411\u6ed1\u52a8\uff08\u7528\u6237\u62cd\u677f 2026-08\uff09\uff1a"
        "\u8fde\u63a5\u59ff\u6001\u4fdd\u6301\uff08\u89d2\u5ea6\u59cb\u7ec8\u968f\u57fa\u51c6\u7ebf\uff09\uff0c"
        "\u4f46\u4f4d\u7f6e\u53ea\u7559\u4e00\u4e2a\u81ea\u7531\u5ea6\u3002\u300c\u6cbf\u7ebf\u6ed1\u52a8\u300d="
        "\u8fde\u63a5\u70b9\u6cbf\u57fa\u51c6\u7ebf\u65b9\u5411\u6ed1\uff08\u5782\u76f4\u504f\u79fb\u9501\u5b9a\uff09\uff1b"
        "\u300c\u5782\u76f4\u62c9\u51fa\u300d=\u8fde\u63a5\u70b9\u5782\u76f4\u57fa\u51c6\u7ebf\u62c9\u52a8"
        "\uff08\u6cbf\u7ebf\u4f4d\u7f6e\u9501\u5b9a\uff09\uff1b\u57fa\u51c6\u7ebf\u65cb\u8f6c\u65f6\u6ed1\u8f68\u8ddf\u7740\u8f6c\u3002"
        "\u5207\u56de\u300c\u5168\u8fde\u63a5\u300d\u4f4d\u7f6e\u91cd\u65b0\u5438\u9644\u56de\u5bbf\u4e3b\u70b9\u3002"));
    // 抽屉式单向滑动（用户拍板 2026-08）：连接姿态保持（角度始终随基准线），但位置只留一个自由度。「沿线滑动」=连接点沿基准线方向滑（垂直偏移锁定）；「垂直拉出」=连接点垂直基准线拉动（沿线位置锁定）；基准线旋转时滑轨跟着转。切回「全连接」位置重新吸附回宿主点。
    slideLayout->addWidget(lblSlide);
    m_cmbSlideMode = new QComboBox(m_slideRow);
    m_cmbSlideMode->addItem(QString::fromUtf8("\u5168\u8fde\u63a5"));        // 全连接
    m_cmbSlideMode->addItem(QString::fromUtf8("\u6cbf\u7ebf\u6ed1\u52a8"));  // 沿线滑动
    m_cmbSlideMode->addItem(QString::fromUtf8("\u5782\u76f4\u62c9\u51fa"));  // 垂直拉出
    m_cmbSlideMode->setMinimumWidth(120);
    m_cmbSlideMode->setToolTip(QString::fromUtf8(
        "\u6ed1\u8f68\u6a21\u5f0f\uff08\u62bd\u5c49\u5f0f\u5355\u5411\u6ed1\u52a8\uff09\uff1a"
        "\u5168\u8fde\u63a5 = \u4f4d\u7f6e\u5438\u9644 + \u89d2\u5ea6\u8ddf\u968f\uff08\u9ed8\u8ba4\uff09\uff1b"
        "\u6cbf\u7ebf\u6ed1\u52a8 = \u4ec5\u6cbf\u57fa\u51c6\u7ebf\u65b9\u5411\u53ef\u6ed1\uff1b"
        "\u5782\u76f4\u62c9\u51fa = \u4ec5\u5782\u76f4\u57fa\u51c6\u7ebf\u53ef\u62c9\u3002"
        "\u8fdb\u5165\u6ed1\u8f68\u540e\u62d6\u52a8\u8ddf\u968f\u7ebf\u53ea\u6cbf\u5bf9\u5e94\u65b9\u5411\u52a8\uff0c"
        "\u89d2\u5ea6\u8ddf\u968f\u59cb\u7ec8\u4fdd\u7559\u3002\u4e0e\u300c\u62c6\u5f00\uff08\u4fdd\u7559\u89d2\u5ea6\uff09\u300d"
        "\u4e92\u65a5\u3002"));
    slideLayout->addWidget(m_cmbSlideMode);
    m_lblSlideBadge = new ElaText(QString(), 13, m_slideRow);
    m_lblSlideBadge->setStyleSheet(QStringLiteral(
        "color:#0F766E; background:#E6F4F2; border-radius:3px;"
        "padding:0 4px; font-size:11px;"));
    m_lblSlideBadge->setToolTip(QString::fromUtf8(
        "\u6ed1\u8f68\u72b6\u6001\uff1a\u89d2\u5ea6\u8ddf\u968f\u4fdd\u6301\uff0c"
        "\u4f46\u4f4d\u7f6e\u53ea\u7559\u4e00\u4e2a\u81ea\u7531\u5ea6\uff08\u62bd\u5c49\u5f0f\u5355\u5411\u6ed1\u52a8\uff09\u3002"
        "\u514d\u91cd\u65b0\u8d34\u5408\u57fa\u51c6\u7ebf\u540e\uff0c\u952e\u76d8\u62d6\u52a8\u8ddf\u968f\u7ebf\u4ec5\u6cbf\u6b64\u65b9\u5411\u79fb\u52a8\u3002"));
    slideLayout->addWidget(m_lblSlideBadge);
    slideLayout->addStretch();
    angleLayout->addWidget(m_slideRow);

    // 拖动保护 checkbox (ALL lines, no hidden items): checked = the connection
    // is PROTECTED (拖动保护/焊接) — dragging cannot tear it apart, dragging
    // either side moves the whole pair. Disabled while the line is free (no
    // connection to protect); refreshCard() syncs text/state. 新建连接默认
    // 勾选 (addAttachment 统一置位), 取消勾选 = 手动解锁.
    m_chkLockConn = new ElaCheckBox(QString::fromUtf8("\u62d6\u52a8\u4fdd\u62a4"), this);  // 拖动保护
    
    m_chkLockConn->setToolTip(QString::fromUtf8(
        "\u52fe\u9009\u540e\u8fde\u63a5\u53d7\u62d6\u52a8\u4fdd\u62a4\uff1a\u62d6\u52a8\u4efb\u4e00\u7aef\u65f6\u6574\u4e2a\u5bf9\u4e00\u8d77\u79fb\u52a8\uff0c\u4e0d\u4f1a\u88ab\u62d6\u62c6\uff1b"
        "\u53d6\u6d88\u52fe\u9009\u540e\u62d6\u52a8\u8ddf\u968f\u7ebf\u5373\u53ef\u62c6\u6563\u3002\u53ea\u8981\u5efa\u7acb\u8ddf\u968f\u5c31\u9ed8\u8ba4\u52fe\u4e0a\u62d6\u52a8\u4fdd\u62a4\u3002"));
    // 勾选后连接受拖动保护：拖动任一端时整个对一起移动，不会被拖拆；取消勾选后拖动跟随线即可拆散。只要建立跟随就默认勾上拖动保护。
    angleLayout->addWidget(m_chkLockConn);

    // Angle mode toggle (angle ↔ arc length) for follower lines.
    connect(m_btnAngleMode, &QPushButton::clicked,
            this, &SegmentConnectionCard::onModeToggle);
    connect(m_refConnPoint, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onTargetResolved);
    connect(m_btnClearConn, &QPushButton::clicked,
            this, &SegmentConnectionCard::onClear);
    connect(m_refConnectTo, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onConnectToResolved);
    connect(m_chkFollowHost, &QCheckBox::toggled,
            this, &SegmentConnectionCard::onFollowHostToggled);
    connect(m_chkLockConn, &QCheckBox::toggled,
            this, &SegmentConnectionCard::onLockToggled);
    connect(m_cmbSlideMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SegmentConnectionCard::onSlideModeChanged);
    // Live readouts: the doc re-resolves per frame during drags (the dialog is
    // NON-modal, so the leader can move/rotate while the card is open) — the
    // 绝对角度 hint must track it. Lightweight slot: text only, no input.
    connect(m_doc, &cad::param::ParamDocument::resolved,
            this, &SegmentConnectionCard::onDocResolved);
    // Angle: textChanged restarts the dialog debounce; editingFinished
    // (Enter/focus-loss) applies immediately.
    connect(m_editAngle, &QLineEdit::textChanged,
            this, &SegmentConnectionCard::onAngleDirty);
    connect(m_editAngle, &QLineEdit::editingFinished,
            this, &SegmentConnectionCard::applyAngle);
}

void SegmentConnectionCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    refresh();
}

void SegmentConnectionCard::refresh()
{
    populateAngleField();
    refreshCard();
}

const cad::param::Attachment* SegmentConnectionCard::findFollowerAttachment() const
{
    if (!m_doc) return nullptr;
    for (const auto& att : m_doc->attachments()) {
        // Position pins (bridge lines) are not construction-angle followers.
        if (att.isPin) continue;
        if (att.fromBlockId == m_blockId)
            return &att;
    }
    return nullptr;
}

QString SegmentConnectionCard::leaderRefLabel(const cad::param::Attachment& att) const
{
    if (!m_doc) return QString();

    QString segPart;
    const cad::param::Block* leader = m_doc->findBlock(att.toBlockId);
    if (leader) {
        const cad::param::Segment* lseg = leader->findSegment(att.toSegmentId);
        if (lseg) {
            segPart = cad::param::Serial::tag(lseg->serial);
            if (!lseg->name.isEmpty())
                segPart += QStringLiteral("\u00b7") + lseg->name;
        }
    }
    return segPart.isEmpty() ? QStringLiteral("?") : segPart;
}

void SegmentConnectionCard::refreshCard()
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;

    const cad::param::Attachment* att = findFollowerAttachment();

    // 省道线 (用户拍板 2026-08): 起点 A 已挂、终点 = B 偏移解算 — 独立于附件
    // 体系。角度反算只读、d/β 可改、B 所在线段只读不修改（单向挂靠）。
    if (block && block->isDart()) {
        showDartState(*block, seg);
        return;
    }
    // Restore the angle-editing row for normal lines (dart state hides it).
    m_editAngle->setVisible(true);
    m_lblAngleCaption->setVisible(true);
    m_lblFollowValue->setVisible(false);
    m_lblFxAngle->setVisible(false);
    m_dartRow->setVisible(false);

    if (att) {
        // ── Connected state ──
        m_titleLabel->setText(QString::fromUtf8("跟随角度 · 连接"));  // 跟随角度 · 连接
        m_connRow->setVisible(true);
        m_freeConnRow->setVisible(false);
        m_lblLeaderRef->setText(leaderRefLabel(*att));
        // Cross-layer badge ("→ leader 层名"); hidden for same-layer.
        const QString badge = crossLayerBadge(m_doc, *att);
        m_lblLayerBadge->setText(badge);
        m_lblLayerBadge->setVisible(!badge.isEmpty());
        // 拆开保留角度 badge: 位置自由, 角度仍跟随基准线.
        m_lblAngleOnlyBadge->setText(att->angleOnly
            ? QString::fromUtf8("\u4ec5\u89d2\u5ea6 \u00b7 \u4f4d\u7f6e\u81ea\u7531")  // 仅角度 · 位置自由
            : QString());
        m_lblAngleOnlyBadge->setVisible(att->angleOnly);
        // 滑轨模式 (抽屉式滑动, 用户拍板 2026-08): selector 与 badge 同步.
        // angleOnly (拆开) 与滑轨互斥 — 选择器保持「全连接」并禁用.
        {
            const QSignalBlocker cb(m_cmbSlideMode);
            m_cmbSlideMode->setCurrentIndex(static_cast<int>(att->slideMode));
            m_cmbSlideMode->setEnabled(!att->angleOnly);
        }
        if (att->slideMode == cad::param::SlideMode::AlongLeader)
            m_lblSlideBadge->setText(QString::fromUtf8("\u6cbf\u7ebf\u6ed1\u52a8"));  // 沿线滑动
        else if (att->slideMode == cad::param::SlideMode::PerpLeader)
            m_lblSlideBadge->setText(QString::fromUtf8("\u5782\u76f4\u62c9\u51fa"));  // 垂直拉出
        else
            m_lblSlideBadge->setText(QString());
        m_lblSlideBadge->setVisible(att->slideMode
                                    != cad::param::SlideMode::None);
        m_slideRow->setVisible(true);
        m_refConnPoint->setExcludeBlock(m_blockId);
        m_refConnPoint->setPoint(att->toBlockId, att->toPointId);
        m_btnAngleMode->setVisible(true);

        // Mode-dependent caption + world-angle hint.
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            m_btnAngleMode->setText(QStringLiteral("\xe2\x8c\x92"));  // ⌒
            m_lblAngleCaption->setText(QString::fromUtf8("\u5f27\u957f(cm):"));  // 弧长(cm):
            m_lblAngleCaption->setStyleSheet(QString());

            double arcMm = att->arcLength;
            if (!att->arcLengthFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    att->arcLengthFormula, m_doc->parameters(), {});
                if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
                // 输入框是公式：右侧显示当前计算值（表达式不直观）。
                // 显示 = 带符号折角弧长（v3 定稿，与旋转 HUD 一致）。
                const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
                const double alphaDeg = (radius > 1e-9)
                    ? (arcMm / radius) * 180.0 / M_PI : 0.0;
                const double foldDeg = signedFoldDeg(alphaDeg);
                m_lblFollowValue->setText(QString::fromUtf8("= %1 cm")
                    .arg(foldDeg * M_PI / 180.0 * radius * 0.1, 0, 'f', 2));
                m_lblFollowValue->setVisible(true);
            } else {
                m_lblFollowValue->setVisible(false);
            }
            updateWorldAngleLabel(*att);
        } else {
            m_btnAngleMode->setText(QStringLiteral("\xe2\x88\xa0"));  // ∠
            m_lblAngleCaption->setText(QString::fromUtf8("\u8ddf\u968f\u89d2\u5ea6(\u00b0):"));  // 跟随角度(°):
            m_lblAngleCaption->setStyleSheet(QString());

            double constDeg = att->followerAngle;
            if (!att->followerAngleFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    att->followerAngleFormula, m_doc->parameters(), {});
                if (r.ok) constDeg = r.value;
                // 输入框是公式：右侧显示当前计算值（表达式不直观）。
                // 显示 = 带符号折角（v3 定稿，与旋转 HUD 一致）。
                m_lblFollowValue->setText(QString::fromUtf8("= %1\u00b0")
                    .arg(formatAngleDeg(signedFoldDeg(constDeg))));
                m_lblFollowValue->setVisible(true);
            } else {
                m_lblFollowValue->setVisible(false);
            }
            // 绝对角度提示 (闭合基准): 世界角 = refWorld + 180° − 线夹角.
            updateWorldAngleLabel(*att);
        }
    } else {
        // ── Free state ──
        // 自由线显示绝对角度（相对水平方向），与跟随角度明确区分 —— 旋转工具
        // 无论以起点还是终点为锚心，改的都是这个绝对角度；卡片标题与 caption
        // 直接标注“绝对角度”，不再只藏在 toolTip 里。
        m_titleLabel->setText(QString::fromUtf8("绝对角度 · 连接"));  // 绝对角度 · 连接
        m_connRow->setVisible(false);
        m_slideRow->setVisible(false);
        m_freeConnRow->setVisible(true);
        m_lblLayerBadge->setVisible(false);
        m_lblAngleOnlyBadge->setVisible(false);
        m_refConnectTo->setExcludeBlock(m_blockId);
        m_refConnectTo->clearPoint();
        m_btnAngleMode->setVisible(false);
        m_lblAngleCaption->setText(QString::fromUtf8("\u7edd\u5bf9\u89d2\u5ea6(\u00b0):"));  // 绝对角度(°):
        m_lblAngleCaption->setStyleSheet(QString());
        m_lblAngleCaption->setToolTip(QString::fromUtf8(
            "\u81ea\u7531\u7ebf\u6bb5\u7684\u7edd\u5bf9\u89d2\u5ea6\uff1a\u76f8\u5bf9\u6c34\u5e73\u65b9\u5411\uff08+X \u8f74\uff09\uff0c"
            "\u9006\u65f6\u9488\u4e3a\u6b63\u3002\u65cb\u8f6c\u5de5\u5177\u4e2d\u6309 X \u5207\u6362\u8d77\u70b9/\u7ec8\u70b9\u951a\u5fc3\uff1a"
            "\u8d77\u70b9\u951a\u5fc3\u65f6 HUD \u89d2\u5ea6 = \u7ebf\u7684\u65b9\u5411\uff1b\u7ec8\u70b9\u951a\u5fc3\u65f6 HUD \u89d2\u5ea6 = "
            "\u4ece\u7ec8\u70b9\u6307\u5411\u7ebf\u7684\u65b9\u5411\uff08= \u7ebf\u7684\u65b9\u5411 + 180\u00b0\uff09\uff0c\u7ebf\u59cb\u7ec8\u8ddf\u968f\u5149\u6807\u3002"
            "\u672c\u9762\u677f\u663e\u793a\u7684\u662f\u7ebf\u7684\u5ba2\u89c2\u65b9\u5411\uff08\u8d77\u70b9\u2192\u7ec8\u70b9\uff09\uff0c"
            "\u7ec8\u70b9\u951a\u5fc3\u65cb\u8f6c\u540e\u4e0e HUD \u5dee 180\u00b0 \u5c5e\u6b63\u5e38\u89c6\u89d2\u5dee\u5f02\u3002"));
        // 面板显示线的客观方向（起点→终点）；旋转工具终点锚心的 HUD 显示
        // 从终点指向线的方向（线方向+180°），两者差 180° 是视角差异。
        m_lblWorldAngle->setVisible(false);
        // 绝对角度表达式：输入框是公式时右侧显示当前计算值（表达式不直观）。
        const cad::param::ParamPoint* epFree =
            seg ? block->findPoint(seg->endPointId) : nullptr;
        if (epFree && !epFree->angleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                epFree->angleFormula, m_doc->parameters(), {});
            if (r.ok) {
                // 公式存的是本地 Polar 角（onAngleApply 已补偿旋转），
                // 显示时加回块旋转 = 用户输入语义的绝对角度；方向统一
                // 逆时针为正（2026-08 v3 定稿）。
                const double rotDeg = block->transform.rotation * 180.0 / M_PI;
                double deg = std::fmod(r.value + rotDeg, 360.0);
                if (deg < 0.0) deg += 360.0;
                m_lblFollowValue->setText(QString::fromUtf8("= %1\u00b0")
                    .arg(formatAngleDeg(deg)));
                m_lblFollowValue->setVisible(true);
            } else {
                m_lblFollowValue->setVisible(false);
            }
        } else {
            m_lblFollowValue->setVisible(false);
        }
    }

    // ── Follow-host checkbox (all lines): host = the connection target, or
    // the free-state 跟随宿主 input's resolved value when not yet connected.
    if (block && seg) {
        const QUuid hostBlock = att ? att->toBlockId : m_refConnectTo->resolvedBlockId();
        const QUuid hostPoint = att ? att->toPointId : m_refConnectTo->resolvedPointId();
        const auto* hostBlk = !hostBlock.isNull() ? m_doc->findBlock(hostBlock) : nullptr;
        const auto* hostPt = hostBlk ? hostBlk->findPoint(hostPoint) : nullptr;

        const QSignalBlocker b(m_chkFollowHost);
        m_chkFollowHost->setText(hostPt
            ? QString::fromUtf8("\u8ddf\u968f\u5bbf\u4e3b %1")  // 跟随宿主 %1
                .arg(cad::param::Serial::tag(hostPt->serial))
            : (att ? QString::fromUtf8("\u8ddf\u968f\u5bbf\u4e3b\uff08\u5df2\u5220\u9664\uff09")  // 跟随宿主（已删除）
                   : QString::fromUtf8("\u8ddf\u968f\u5bbf\u4e3b")));  // 跟随宿主
        if (att && att->angleOnly)
            m_chkFollowHost->setText(m_chkFollowHost->text()
                + QString::fromUtf8("\uff08\u4ec5\u89d2\u5ea6\uff09"));  // （仅角度）
        m_chkFollowHost->setChecked(att != nullptr);
        m_chkFollowHost->setEnabled(true);
        m_chkFollowHost->setToolTip(QString::fromUtf8(
            "\u52fe\u9009\u540e\u8d77\u70b9\u8fde\u63a5\u5230\u5bbf\u4e3b\u70b9\uff0c\u5bbf\u4e3b\u79fb\u52a8\u65f6\u6574\u7ebf\u5e73\u79fb\u8ddf\u968f\uff1b"
            "\u53d6\u6d88\u52fe\u9009 = \u62c6\u5f00\uff08\u4fdd\u7559\u89d2\u5ea6\u8ddf\u968f\uff0c\u4f4d\u7f6e\u81ea\u7531\uff09\uff0c\u89d2\u5ea6\u4ecd\u968f\u57fa\u51c6\u7ebf\u65cb\u8f6c\u3002"
            "\u5f7b\u5e95\u65ad\u5f00\u7528\u300c\u6e05\u9664\u300d\u3002\u672a\u8fde\u63a5\u65f6\u5728\u4e0a\u65b9\u8f93\u5165\u5bbf\u4e3b\u70b9 P \u7f16\u53f7\u56de\u8f66\u5373\u53ef\u5efa\u7acb\u8ddf\u968f"));
        // 勾选后起点连接到宿主点，宿主移动时整线平移跟随；取消勾选 = 拆开（保留角度跟随，位置自由），角度仍随基准线旋转。彻底断开用「清除」。未连接时在上方输入宿主点 P 编号回车即可建立跟随
        m_chkFollowHost->setVisible(true);

        // Lock checkbox: connected → shows the attachment's locked state and
        // is usable; free → disabled (nothing to lock yet). Angle-only /
        // 滑轨 connections keep a free (or one-axis-free) position — 无焊接
        // 可保护 → disabled. Always VISIBLE (统一显示, no hidden items).
        const QSignalBlocker lb(m_chkLockConn);
        m_chkLockConn->setChecked(att && att->isLocked);
        m_chkLockConn->setEnabled(att != nullptr && !att->angleOnly
            && att->slideMode == cad::param::SlideMode::None);
        m_chkLockConn->setVisible(true);
    }
}

void SegmentConnectionCard::populateAngleField()
{
    cad::param::Block* block = m_doc ? m_doc->findBlock(m_blockId) : nullptr;
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;

    // Bridge lines never edit the angle — the dialog mirrors the same for the
    // length editor; re-enable here so a re-target to a normal line recovers.
    m_editAngle->setEnabled(!(block && block->isBridge));

    const cad::param::Attachment* att = findFollowerAttachment();
    if (att) {
        // Follower: show the follower angle or arc length depending on mode.
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            if (!att->arcLengthFormula.isEmpty()) {
                m_editAngle->setText(att->arcLengthFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                // 显示 = 带符号折角弧长（v3 定稿，与旋转 HUD 一致）：
                // 折叠 0 / 开平 πr / 另一侧负值。编辑时按同一语义回写
                // （applyAngle 负责 折角 cm → α → 存储弧长）。
                const double radius = block ? block->segmentLengthAtPoint(att->fromPointId) : 0.0;
                const double alphaDeg = (radius > 1e-9)
                    ? (att->arcLength / radius) * 180.0 / M_PI : 0.0;
                const double foldDeg = signedFoldDeg(alphaDeg);
                m_editAngle->setText(QString::number(
                    foldDeg * M_PI / 180.0 * radius * 0.1, 'f', 2));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(
                QString::fromUtf8("\u6570\u503c(cm)\u6216\u516c\u5f0f"));  // 数值(cm)或公式
        } else {
            if (!att->followerAngleFormula.isEmpty()) {
                m_editAngle->setText(att->followerAngleFormula);
                m_lblFxAngle->setVisible(true);
            } else {
                // 显示 = 带符号折角 [−180°, +180°]（v3 定稿）：折叠 0 /
                // 垂直 ±90 / 开平 ±180，符号 = 折向。编辑时按同一语义回写
                // （applyAngle 负责 折角 → α → 存储）。
                m_editAngle->setText(formatAngleDeg(signedFoldDeg(att->followerAngle)));
                m_lblFxAngle->setVisible(false);
            }
            m_editAngle->setPlaceholderText(
                QString::fromUtf8("\u6570\u503c(\u00b0)\u6216\u516c\u5f0f"));  // 数值(°)或公式
        }
    } else if (block && seg) {
        // Free block: stored endpoint angle formula or numeric world angle.
        const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
        const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (ep && !ep->angleFormula.isEmpty()) {
            m_editAngle->setText(ep->angleFormula);
            m_lblFxAngle->setVisible(true);
        } else if (sp && ep && sp->resolved && ep->resolved) {
            // 自由线绝对角度 = 世界角（0~360°，逆时针为正，2026-08 v3 定稿）。
            cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
            cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
            double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
            angleDeg = std::fmod(angleDeg, 360.0);
            if (angleDeg < 0.0) angleDeg += 360.0;
            m_editAngle->setText(QString::number(angleDeg, 'f', 1));
            m_lblFxAngle->setVisible(false);
        }
    }
}

void SegmentConnectionCard::setBridgeReadOnly(bool bridge)
{
    if (!bridge || !m_doc) return;
    const cad::param::Block* block = m_doc->findBlock(m_blockId);
    if (!block || !block->isBridge) return;
    const cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // Show the measured world angle (read-only).
    const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
    const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
    if (sp && ep && sp->resolved && ep->resolved) {
        // 桥接线显示世界角（0~360°，逆时针为正，2026-08 v3 定稿）。
        const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
        const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
        double angleDeg = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
        angleDeg = std::fmod(angleDeg, 360.0);
        if (angleDeg < 0.0) angleDeg += 360.0;
        m_editAngle->setText(QString::number(angleDeg, 'f', 1));
    }

    m_editAngle->setEnabled(false);
    m_lblFxAngle->setVisible(false);
    m_lblWorldAngle->setVisible(false);
    m_lblFollowValue->setVisible(false);
    m_lblAngleCaption->setText(QString::fromUtf8("\u89d2\u5ea6(\u00b0):"));  // 角度(°):
    m_lblAngleCaption->setStyleSheet(QString());
    const QString tip = QString::fromUtf8(
        "\u6865\u63a5\u7ebf\uff1a\u957f\u5ea6\u4e0e\u89d2\u5ea6\u7531\u4e24\u7aef"
        "\u9489\u4f4f\u7684\u5bbf\u4e3b\u70b9\u51b3\u5b9a\uff0c\u4e0d\u53ef\u7f16\u8f91");  // 桥接线：长度与角度由两端钉住的宿主点决定，不可编辑
    m_editAngle->setToolTip(tip);
}

void SegmentConnectionCard::applyAngle()
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    QString text = m_editAngle->text().trimmed();
    if (text.isEmpty()) return;

    // Evaluate: number or formula
    bool isNumber = false;
    double targetDeg = text.toDouble(&isNumber);
    if (!isNumber) {
        auto r = cad::param::ConditionEngine::evaluate(
            text, m_doc->parameters(), {});
        if (!r.ok) return;
        targetDeg = r.value;
    }

    // Check if this block is a follower
    bool isFollower = false;
    QUuid attId;
    for (const auto& att : m_doc->attachments()) {
        if (att.fromBlockId == m_blockId) {
            isFollower = true;
            attId = att.id;
            break;
        }
    }

    if (isFollower) {
        // The angle field edits the FOLLOWER ANGLE or ARC LENGTH directly.
        // 输入 = 显示域（带符号折角，v3 定稿）→ 存储域 α ∈ [0, 360°)；
        // 公式输入原样存储（公式域 = 存储域，全角域不受限）。
        if (auto* att = m_doc->findAttachment(attId)) {
            if (att->rotationMode == cad::param::RotationMode::ArcLength) {
                const double radius = block->segmentLengthAtPoint(att->fromPointId);
                const double foldDeg = (radius > 1e-9)
                    ? targetDeg / (M_PI / 180.0 * radius * 0.1) : 0.0;
                const double alphaDeg = alphaFromSignedFold(foldDeg);
                att->arcLength = alphaDeg * M_PI / 180.0 * radius;
                att->arcLengthFormula = isNumber ? QString() : text;
            } else {
                att->followerAngle = alphaFromSignedFold(targetDeg);
                att->followerAngleFormula = isNumber ? QString() : text;
            }
        }
    } else {
        // Free block: set endpoint's Polar angle directly.
        cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (!ep) return;

        if (ep->constraint != cad::param::PointConstraint::Polar) {
            const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
            if (!sp || !sp->resolved || !ep->resolved) return;
            double dist = sp->resolvedPos.distanceTo(ep->resolvedPos);
            ep->constraint = cad::param::PointConstraint::Polar;
            ep->refPointId = seg->startPointId;
            ep->distance = dist;
        }

        // 自由线显示 = 绝对角度（0~360°，逆时针为正，2026-08 v3 定稿）；
        // Polar 存储角 = 世界角 − 块旋转，故 localDeg = 显示角 − rotDeg。
        const double rotDeg = block->transform.rotation * 180.0 / M_PI;
        const double localDeg = targetDeg - rotDeg;
        ep->angle = localDeg;
        ep->angleFormula.clear();

        if (!isNumber) {
            ep->angleFormula = (std::abs(rotDeg) > 1e-9)
                ? QStringLiteral("(%1)-%2").arg(text).arg(rotDeg, 0, 'g', 12)
                : text;
        }
    }

    // Clear dirty indicator (do NOT overwrite the user's input text)
    m_editAngle->setStyleSheet(QString());
    m_lblFxAngle->setVisible(!isNumber && !text.isEmpty());
    // 绝对角度/跟随值等读数依赖刚写入的角度值 —— 必须就地刷新, 否则
    // "= 绝对角度 xx°" 停留在旧值 (用户报告 2026-08: 不是实时刷新的)。
    // refreshCard() 不动 m_editAngle (保留用户输入), 只刷标签/行状态.
    refreshCard();
    emit changed(ChangeKind::AngleApplied);
}

void SegmentConnectionCard::onAngleDirty()
{
    m_editAngle->setStyleSheet(QString());

    QString text = m_editAngle->text().trimmed();
    bool isNumber = false;
    text.toDouble(&isNumber);
    m_lblFxAngle->setVisible(!isNumber && !text.isEmpty());

    emit angleEdited();
}

void SegmentConnectionCard::onModeToggle()
{
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att || !m_doc) return;

    // 用户可能在编辑后直接点模式切换按钮（未回车）：先把输入落盘，避免
    // 切换时输入丢失。输入是公式（非纯数值）时拒绝切换——公式必须原样
    // 保留，不参与换算（用户要求）；无效公式同样拒绝，避免输入被刷新。
    const QString text = m_editAngle->text().trimmed();
    if (!text.isEmpty()) {
        bool isNumber = false;
        text.toDouble(&isNumber);
        if (!isNumber) return;
        applyAngle();
    }

    auto* mutAtt = m_doc->findAttachment(att->id);
    if (!mutAtt) return;

    // 模型已存公式（角度/弧长表达式）：同样拒绝切换，绝不换算烘焙公式。
    const bool hasFormula =
        (mutAtt->rotationMode == cad::param::RotationMode::ArcLength)
            ? !mutAtt->arcLengthFormula.isEmpty()
            : !mutAtt->followerAngleFormula.isEmpty();
    if (hasFormula) return;

    // Geometry-preserving switch.
    cad::param::Block* blk = m_doc->findBlock(m_blockId);
    double radius = blk ? blk->segmentLengthAtPoint(mutAtt->fromPointId) : 0.0;
    double curDeg = mutAtt->followerAngle;
    if (mutAtt->rotationMode == cad::param::RotationMode::ArcLength) {
        double arcMm = mutAtt->arcLength;
        if (!mutAtt->arcLengthFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                mutAtt->arcLengthFormula, m_doc->parameters(), {});
            if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
        }
        // 弧长 = 线夹角恒等映射（2026-08 定稿）：弧长 0 = 0° 折叠、
        // πr = 180° 开平，与 Resolver 一致，不再反转。归一化 [0, 360°)。
        curDeg = (radius > 1e-9) ? (arcMm / radius) * 180.0 / M_PI : 0.0;
        curDeg = std::fmod(curDeg, 360.0);
        if (curDeg < 0.0) curDeg += 360.0;
    } else if (!mutAtt->followerAngleFormula.isEmpty()) {
        auto r = cad::param::ConditionEngine::evaluate(
            mutAtt->followerAngleFormula, m_doc->parameters(), {});
        if (r.ok) curDeg = r.value;
    }

    if (mutAtt->rotationMode == cad::param::RotationMode::Angle) {
        mutAtt->rotationMode = cad::param::RotationMode::ArcLength;
        // 弧长 = 线夹角恒等映射（2026-08 定稿）：弧长角 = 显示角，不再反转。
        mutAtt->arcLength = std::fmod(curDeg, 360.0) * M_PI / 180.0 * radius;
        mutAtt->arcLengthFormula.clear();
    } else {
        mutAtt->rotationMode = cad::param::RotationMode::Angle;
        mutAtt->followerAngle = curDeg;
        mutAtt->followerAngleFormula.clear();
    }
    m_doc->resolveAll();

    // 完整刷新（caption/按钮图标/世界角提示/跟随值一并更新，不只输入框）：
    // 不完整刷新曾导致切到弧长后界面文字毫无变化（用户报告）。
    refreshCard();
    populateAngleField();
    emit changed(ChangeKind::ModeSwitched);
}

void SegmentConnectionCard::onLockToggled(bool on)
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att || att->isLocked == on) { refreshCard(); return; }
    m_doc->setAttachmentLocked(att->id, on);
    refreshCard();
    emit changed(ChangeKind::LockToggled);
}

void SegmentConnectionCard::onSlideModeChanged(int index)
{
    if (!m_doc) return;
    const cad::param::Attachment* att = findFollowerAttachment();
    if (!att || att->angleOnly) { refreshCard(); return; }  // 拆开态禁用

    // Combo order mirrors cad::param::SlideMode (None=0 / AlongLeader=1 /
    // PerpLeader=2).
    const auto mode = static_cast<cad::param::SlideMode>(index);
    if (att->slideMode == mode) { refreshCard(); return; }
    m_doc->setAttachmentSlideMode(att->id, mode);
    refreshCard();
    emit changed(ChangeKind::SlideModeChanged);
}

// ── 绝对角度 hint (闭合基准) ─────────────────────────────────────────────
// 世界角 = refWorld + 180° − 线夹角（角度模式用 followerAngle / 公式求值,
// 弧长模式用 arcMm/radius 换算成同基准角度）。归一化 [0, 360°) 不爆表。
// 文本同值短路 —— 供 refreshCard 与每帧 onDocResolved 共用, 帧内调用安全。
void SegmentConnectionCard::updateWorldAngleLabel(const cad::param::Attachment& att)
{
    if (!m_doc) { m_lblWorldAngle->setVisible(false); return; }
    const cad::param::Block* leader = m_doc->findBlock(att.toBlockId);
    if (!leader) { m_lblWorldAngle->setVisible(false); return; }

    const double refWorldDeg = (leader->transform.rotation
        + leader->exitDirectionAtPoint(att.toPointId, att.toSegmentId))
        * 180.0 / M_PI;

    double constDeg;
    if (att.rotationMode == cad::param::RotationMode::ArcLength) {
        double arcMm = att.arcLength;
        if (!att.arcLengthFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att.arcLengthFormula, m_doc->parameters(), {});
            if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
        }
        const cad::param::Block* block = m_doc->findBlock(m_blockId);
        const double radius = block ? block->segmentLengthAtPoint(att.fromPointId) : 0.0;
        constDeg = (radius > 1e-9) ? (arcMm / radius) * 180.0 / M_PI : 0.0;
        constDeg = std::fmod(constDeg, 360.0);
        if (constDeg < 0.0) constDeg += 360.0;
    } else {
        constDeg = att.followerAngle;
        if (!att.followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att.followerAngleFormula, m_doc->parameters(), {});
            if (r.ok) constDeg = r.value;
        }
    }

    double absDeg = std::fmod(refWorldDeg + 180.0 - constDeg, 360.0);
    if (absDeg < 0.0) absDeg += 360.0;
    const QString text = QString::fromUtf8("= 绝对角度 %1°")
                             .arg(formatAngleDeg(absDeg));
    if (m_lblWorldAngle->text() != text)
        m_lblWorldAngle->setText(text);
    m_lblWorldAngle->setVisible(true);
}

void SegmentConnectionCard::onDocResolved()
{
    // Live path: only the geometry-dependent readouts, never the editor input
    // (populateAngleField would clobber in-progress typing).
    const cad::param::Attachment* att = findFollowerAttachment();
    if (att) {
        updateWorldAngleLabel(*att);
        return;
    }
    // 省道线 (用户拍板 2026-08): 每帧 resolved 也刷新反算角度, 让基准线段
    // 被拖动/旋转时读数实时跟. 只动标签文本 (同值短路).
    if (m_doc) {
        if (const auto* block = m_doc->findBlock(m_blockId);
            block && block->isDart()) {
            const QString text = dartFoldAngleText(*block);
            if (!text.isEmpty() && m_dartFoldLabel->text() != text)
                m_dartFoldLabel->setText(text);
        }
    }
}

// ── 省道线态 (用户拍板 2026-08) ────────────────────────────────────────────

QString SegmentConnectionCard::dartFoldAngleText(const cad::param::Block& block) const
{
    if (!m_doc || block.segments.empty()) return QString();
    const auto* aBlk = m_doc->findBlock(block.dartStartBlockId);
    const auto* bBlk = m_doc->findBlock(block.dartRefBlockId);
    if (!aBlk || !bBlk) return QString();
    const auto* aPt = aBlk->findPoint(block.dartStartPointId);
    const auto* bPt = bBlk->findPoint(block.dartRefPointId);
    if (!aPt || !bPt || !aPt->resolved || !bPt->resolved) return QString();

    // 线方向: 终点为 Polar(角度=0), 局部方向 = 0 → 线方向 = 块旋转.
    const double lineAngle = block.transform.rotation;
    // 角度基准 = B 所在线段出口方向 (永远只取偏移点的线段).
    const double thetaB = bBlk->transform.rotation
        + bBlk->exitDirectionAtPoint(block.dartRefPointId,
                                     block.dartRefSegmentId);
    double deg = (lineAngle - thetaB) * 180.0 / M_PI;
    deg = std::fmod(deg, 360.0);
    if (deg > 180.0) deg -= 360.0;
    if (deg < -180.0) deg += 360.0;
    return formatAngleDeg(deg) + QStringLiteral("\u00b0");
}

void SegmentConnectionCard::showDartState(const cad::param::Block& block,
                                          cad::param::Segment* seg)
{
    (void)seg;
    m_titleLabel->setText(QString::fromUtf8("\u7701\u9053\u7ebf \u00b7 \u504f\u79fb\u7ec8\u70b9"));  // 省道线 · 偏移终点
    m_connRow->setVisible(false);
    m_freeConnRow->setVisible(false);
    m_slideRow->setVisible(false);
    m_chkFollowHost->setVisible(false);
    m_chkLockConn->setVisible(false);
    m_btnAngleMode->setVisible(false);
    m_lblAngleCaption->setVisible(false);
    m_editAngle->setVisible(false);
    m_lblFollowValue->setVisible(false);
    m_lblWorldAngle->setVisible(false);
    m_lblFxAngle->setVisible(false);
    m_dartRow->setVisible(true);

    // 起点 A (挂靠点) — 只读.
    QString startText;
    if (const auto* aBlk = m_doc->findBlock(block.dartStartBlockId)) {
        if (const auto* aPt = aBlk->findPoint(block.dartStartPointId))
            startText = cad::param::Serial::tag(aPt->serial);
    }
    const QString startLabel =
        startText.isEmpty() ? QStringLiteral("?") : startText;
    if (m_dartStartRef->text() != startLabel)
        m_dartStartRef->setText(startLabel);

    // 偏移点 B + 所在线段 — 只读 (B 的线段 = 角度基准; 单向挂靠不修改).
    QString refText;
    if (const auto* bBlk = m_doc->findBlock(block.dartRefBlockId)) {
        if (const auto* bPt = bBlk->findPoint(block.dartRefPointId)) {
            refText = cad::param::Serial::tag(bPt->serial);
            if (const auto* bSeg = bBlk->findSegment(block.dartRefSegmentId)) {
                refText += QStringLiteral(" \u2190 ");  // ←
                refText += cad::param::Serial::tag(bSeg->serial);
                if (!bSeg->name.isEmpty())
                    refText += QStringLiteral("\u00b7") + bSeg->name;
            }
        }
    }
    const QString refLabel = refText.isEmpty() ? QStringLiteral("?") : refText;
    if (m_dartRefLabel->text() != refLabel)
        m_dartRefLabel->setText(refLabel);

    // 反算跟随角度 — 灰只读 (同值短路).
    const QString foldText = dartFoldAngleText(block);
    if (!foldText.isEmpty() && m_dartFoldLabel->text() != foldText)
        m_dartFoldLabel->setText(foldText);

    // 偏移 d / 角度 β — 可编辑 (数值或公式; 输入聚焦中不被刷新覆盖).
    const QString offText = block.dartOffsetFormula.isEmpty()
        ? QString::number(block.dartOffsetMm, 'f', 2)
        : block.dartOffsetFormula;
    if (!m_dartOffsetEdit->hasFocus()
        && m_dartOffsetEdit->text().trimmed() != offText)
        m_dartOffsetEdit->setText(offText);
    const QString angText = block.dartAngleFormula.isEmpty()
        ? formatAngleDeg(block.dartAngleDeg)
        : block.dartAngleFormula;
    if (!m_dartAngleEdit->hasFocus()
        && m_dartAngleEdit->text().trimmed() != angText)
        m_dartAngleEdit->setText(angText);
}

void SegmentConnectionCard::onDartOffsetEdited()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block || !block->isDart()) return;
    const QString text = m_dartOffsetEdit->text().trimmed();
    if (text.isEmpty()) return;
    bool isNum = false;
    const double dMm = text.toDouble(&isNum);
    if (isNum) {
        block->dartOffsetMm = dMm;
        block->dartOffsetFormula.clear();
    } else {
        block->dartOffsetFormula = text;
    }
    m_doc->resolveAll();
    emit changed(ChangeKind::AngleApplied);
}

void SegmentConnectionCard::onDartAngleEdited()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block || !block->isDart()) return;
    const QString text = m_dartAngleEdit->text().trimmed();
    if (text.isEmpty()) return;
    bool isNum = false;
    const double deg = text.toDouble(&isNum);
    if (isNum) {
        block->dartAngleDeg = deg;
        block->dartAngleFormula.clear();
    } else {
        block->dartAngleFormula = text;
    }
    m_doc->resolveAll();
    emit changed(ChangeKind::AngleApplied);
}

void SegmentConnectionCard::onTargetResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;

    // Locate the mutable follower attachment.
    cad::param::Attachment* att = nullptr;
    for (const auto& a : m_doc->attachments()) {
        if (!a.isPin && a.fromBlockId == m_blockId) { att = m_doc->findAttachment(a.id); break; }
    }
    if (!att) return;

    // Validate: would the re-targeted attachment create a cycle?
    cad::param::Attachment candidate = *att;
    candidate.toBlockId = blockId;
    candidate.toPointId = pointId;
    std::vector<cad::param::Attachment> others;
    for (const auto& a : m_doc->attachments())
        if (a.id != att->id) others.push_back(a);
    if (cad::param::checkAttachment(others, candidate)
            != cad::param::AttachmentIssue::Ok) {
        refreshCard();  // Revert the widget display.
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    auto* block = m_doc->findBlock(m_blockId);
    if (!leader || !block) { refreshCard(); return; }

    // Re-target.
    att->toBlockId = blockId;
    att->toPointId = pointId;
    att->toSegmentId = leader->exitSegmentAtPoint(pointId);

    // Back-solve the follower angle so the CURRENT world direction is
    // preserved (no visual jump on re-attach).
    if (auto* seg = block->findSegment(m_segmentId)) {
        const double refWorld = leader->transform.rotation
            + leader->exitDirectionAtPoint(pointId, att->toSegmentId);
        const double localDir = block->directionAtPoint(seg->startPointId);
        att->followerAngle = cad::param::backSolveFollowerAngle(
            block->transform.rotation, localDir, refWorld);
    }
    att->followerAngleFormula.clear();
    att->rotationMode = cad::param::RotationMode::Angle;
    att->arcLength = 0.0;
    att->arcLengthFormula.clear();

    // 滑轨模式重定向后: 锁轴坐标必须在**新**基准线局部系下重快照 (否则
    // 锁定的垂直/沿线偏移会沿用旧基准的坐标, 重定向瞬间跟随线会跳).
    if (att->slideMode != cad::param::SlideMode::None)
        m_doc->refreshSlideOffsets(att->id);

    refresh();
    emit changed(ChangeKind::Retargeted);
}

void SegmentConnectionCard::onClear()
{
    if (!m_doc) return;

    // Locate the mutable follower attachment (same search as onTargetResolved).
    const cad::param::Attachment* found = nullptr;
    for (const auto& a : m_doc->attachments()) {
        if (!a.isPin && a.fromBlockId == m_blockId) { found = &a; break; }
    }
    if (!found) { refreshCard(); return; }

    // Remove the attachment: the line becomes free (world angle preserved —
    // removeAttachment resolves, so the block simply stops being driven).
    m_doc->removeAttachment(found->id);

    refresh();
    emit changed(ChangeKind::Disconnected);
}

void SegmentConnectionCard::onConnectToResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const cad::param::Block* leader = m_doc->findBlock(blockId);
    if (!leader || !leader->findPoint(pointId)) { refreshCard(); return; }

    // Build the new attachment (same back-solve as onFollowHostToggled).
    cad::param::Attachment att;
    att.fromBlockId = m_blockId;
    att.fromPointId = seg->startPointId;
    att.toBlockId   = blockId;
    att.toPointId   = pointId;
    att.toSegmentId = leader->exitSegmentAtPoint(pointId);

    const double refWorld = leader->transform.rotation
        + leader->exitDirectionAtPoint(pointId, att.toSegmentId);
    const double localDir = block->directionAtPoint(seg->startPointId);
    att.followerAngle = cad::param::backSolveFollowerAngle(
        block->transform.rotation, localDir, refWorld);

    // May be rejected (cycle / conflicting follower).
    const bool added = m_doc->addAttachment(att);
    if (added && m_scene) {
        // Toast only for genuinely NEW cross-layer connections.
        if (const QString toast = crossLayerToast(m_doc, *block, *leader);
            !toast.isEmpty())
            m_scene->showToast(toast);
    }

    refresh();
    emit changed(ChangeKind::Connected);
}

void SegmentConnectionCard::onFollowHostToggled(bool on)
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) { refreshCard(); return; }

    const cad::param::Attachment* existing = findFollowerAttachment();
    const bool wasAngleOnly = existing && existing->angleOnly;
    if (on) {
        if (wasAngleOnly) {
            // 从「仅角度」恢复完整连接: 位置重新吸附回宿主点 (角度值不变).
            m_doc->setAttachmentAngleOnly(existing->id, false);
        } else {
            // Host = the current connection target, or the free-state 跟随宿主
            // input's resolved value (typed P number).
            const QUuid hostBlock = existing
                ? existing->toBlockId : m_refConnectTo->resolvedBlockId();
            const QUuid hostPoint = existing
                ? existing->toPointId : m_refConnectTo->resolvedPointId();
            const cad::param::Block* leader = m_doc->findBlock(hostBlock);
            if (!leader || !leader->findPoint(hostPoint)) {
                refreshCard();
                return;  // no host yet — the check rolls back
            }

            cad::param::Attachment att;
            att.fromBlockId = m_blockId;
            att.fromPointId = seg->startPointId;
            att.toBlockId   = hostBlock;
            att.toPointId   = hostPoint;
            att.toSegmentId = leader->exitSegmentAtPoint(hostPoint);
            // Back-solve the follower angle so the CURRENT world direction is
            // preserved (no jump on attach).
            const double refWorld = leader->transform.rotation
                + leader->exitDirectionAtPoint(hostPoint, att.toSegmentId);
            const double localDir = block->directionAtPoint(seg->startPointId);
            att.followerAngle = cad::param::backSolveFollowerAngle(
                block->transform.rotation, localDir, refWorld);

            const bool added = m_doc->addAttachment(att);
            if (added && m_scene && leader) {
                if (const QString toast = crossLayerToast(m_doc, *block, *leader);
                    !toast.isEmpty())
                    m_scene->showToast(toast);
            }
        }
    } else {
        if (existing) {
            // 拆开保留角度 (用户拍板 2026-08): 取消勾选 = 只解除位置吸附,
            // 角度跟随保留 (跟随线仍由基准线方向 + 跟随角驱动); 彻底断开
            // 走「清除」按钮.
            m_doc->setAttachmentAngleOnly(existing->id, true);
        }
    }

    refresh();
    if (!existing || (wasAngleOnly && on))
        emit changed(ChangeKind::Connected);          // free→connected / 仅角度→完整
    else
        emit changed(ChangeKind::AngleOnlyToggled);   // 完整连接→仅角度
}

} // namespace cad::tools
