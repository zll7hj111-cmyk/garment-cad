#include "SegmentConnectionCard.h"

#include <algorithm>
#include <cmath>

#include "ElaCheckBox.h"
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include "ElaComboBox.h"
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/ConditionEngine.h"
#include "parametric/FollowerAngle.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "canvas/CanvasScene.h"
#include "PointRefEdit.h"
#include "tools/LayerFeedback.h"
#include "document/commands/AttachmentCommands.h"
#include "ui/Theme.h"

namespace cad::tools {

namespace {

/// 统一行内控件高度。ElaLineEdit/ElaComboBox 原生固定 35px, 而
/// ElaPushButton 默认 38、PointRefEdit 未设 (~24px) —— 混排即“输入框
/// 大小不一”。本卡片所有行内控件统一 35。
constexpr int kFieldH = 35;

/// 左侧标签列固定宽度: 各行首标签左对齐 (视觉成表)。最宽标签
/// “跟随角度(°):” ≈ 72px, 取 92 留余量 (13px 字号)。
constexpr int kLabelW = 92;

ElaText* makeRowLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new ElaText(text, 13, parent);
    lbl->setFixedWidth(kLabelW);
    return lbl;
}

/// 固定宽度 150 的参考输入 (线段编号/点编号): 各行同宽, 子标签列对齐。
ElaLineEdit* makeRefEdit(QWidget* parent)
{
    auto* edit = new ElaLineEdit(parent);
    edit->setFixedWidth(150);
    return edit;
}

} // namespace

// ── UI 构建：构造函数拆出的逐行 builders + 信号接线 (2026-08 拆分) ──
// 纯剪切自原 391 行构造函数；布局装配顺序见 SegmentConnectionCard.cpp 构造函数。
// 2026-12 用户要求“成熟 UI 布局”: 行的形式统一 = [固定宽标签][输入][子标签]
// [输入][按钮], 行内控件同高 (kFieldH), 标签列固定宽 (kLabelW) 全行对齐。

void SegmentConnectionCard::buildModeRow(QVBoxLayout* angleLayout)
{
    // ── 五态连接模式下拉 ──
    m_modeRow = new QWidget(this);
    auto* modeLayout = new QHBoxLayout(m_modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(6);
    modeLayout->addWidget(makeRowLabel(QString::fromUtf8("模式:"), m_modeRow));
    m_cmbMode = new ElaComboBox(m_modeRow);
    m_cmbMode->addItem(QString::fromUtf8("跟随"));
    m_cmbMode->addItem(QString::fromUtf8("独立线段"));
    m_cmbMode->setMinimumWidth(140);
    modeLayout->addWidget(m_cmbMode);
    m_btnAngleOnly = new ElaPushButton(QString::fromUtf8("拆开"), m_modeRow);
    m_btnAngleOnly->setFixedHeight(kFieldH);
    m_btnAngleOnly->setToolTip(QString::fromUtf8(
        "快速拆开：保持角度跟随，解除位置吸附（快拆）"));
    m_btnAngleOnly->setCursor(Qt::PointingHandCursor);
    modeLayout->addWidget(m_btnAngleOnly);
    modeLayout->addStretch();
    angleLayout->addWidget(m_modeRow);
}

void SegmentConnectionCard::buildConnRow(QVBoxLayout* angleLayout)
{
    // 连接行 (跟随 mode): 连接/未连接同构 —— 连接时 [L1][P2][清除],
    // 未连接时两栏为空 (占位符提示), 在「连接点」输入 P 编号即建立连接。
    m_connRow = new QWidget(this);
    auto* connLayout = new QHBoxLayout(m_connRow);
    connLayout->setContentsMargins(0, 0, 0, 0);
    connLayout->setSpacing(6);
    m_lblConnLabel = makeRowLabel(QString::fromUtf8("连接线段:"), m_connRow);
    connLayout->addWidget(m_lblConnLabel);
    m_lblLeaderRef = makeRefEdit(m_connRow);
    m_lblLeaderRef->setPlaceholderText(QString::fromUtf8("线段 L# / 点 P#"));
    m_lblLeaderRef->setToolTip(QString::fromUtf8(
        "输入连接线段 ID/名称，或该线段上的点 ID"));
    m_lblLeaderRef->setStyleSheet("font-size:12px;");
    connLayout->addWidget(m_lblLeaderRef);
    m_lblLayerBadge = new ElaText(QString(), 13, m_connRow);
    // Stylesheet set ONCE at construction (每帧 setStyleSheet 会致卡顿).
    m_lblLayerBadge->setStyleSheet(cad::ui::Theme::purpleBadgeStyle());
    m_lblLayerBadge->setToolTip(QString::fromUtf8(
        "\u8de8\u5c42\u8fde\u63a5\uff1a\u57fa\u51c6\u7ebf\u4f4d\u4e8e\u53e6\u4e00\u56fe\u5c42"));  // 跨层连接：基准线位于另一图层
    m_lblLayerBadge->setVisible(false);
    connLayout->addWidget(m_lblLayerBadge);
    // 拆开保留角度 badge ("仅角度 · 位置自由"): shown while the connection is
    // angle-only (position constraint released, angle following kept).
    m_lblAngleOnlyBadge = new ElaText(QString(), 13, m_connRow);
    m_lblAngleOnlyBadge->setStyleSheet(cad::ui::Theme::tealBadgeStyle());
    m_lblAngleOnlyBadge->setToolTip(QString::fromUtf8(
        "\u62c6\u5f00\u4fdd\u7559\u89d2\u5ea6\uff1a\u4f4d\u7f6e\u5438\u9644\u5df2\u89e3\u9664"
        "\uff08\u7ebf\u6bb5\u53ef\u81ea\u7531\u79fb\u52a8\uff09\uff0c\u4f46\u89d2\u5ea6\u4ecd"
        "\u8ddf\u968f\u57fa\u51c6\u7ebf\uff1b\u57fa\u51c6\u7ebf\u65cb\u8f6c\u65f6\u672c\u7ebf"
        "\u8ddf\u7740\u8f6c\u3002\u518d\u6b21\u52fe\u9009\u201c\u4f4d\u7f6e\u5438\u9644\u201d"
        "\u91cd\u65b0\u5438\u9644\u56de\u5bbf\u4e3b\u70b9\u3002"));  // 拆开保留角度：位置吸附已解除（线段可自由移动），但角度仍跟随基准线；基准线旋转时本线跟着转。再次勾选“位置吸附”重新吸附回宿主点。
    m_lblAngleOnlyBadge->setVisible(false);
    connLayout->addWidget(m_lblAngleOnlyBadge);
    m_lblConnSub = new ElaText(QString::fromUtf8("连接点:"), 13, m_connRow);  // 连接点:
    connLayout->addWidget(m_lblConnSub);
    m_refConnPoint = new PointRefEdit(m_doc, m_connRow);
    m_refConnPoint->setFixedWidth(150);
    connLayout->addWidget(m_refConnPoint);
    m_btnClearConn = new ElaPushButton(QString::fromUtf8("\u6e05\u9664"), m_connRow);  // 清除
    m_btnClearConn->setFixedHeight(kFieldH);
    m_btnClearConn->setToolTip(QString::fromUtf8(
        "\u62c6\u9664\u8fde\u63a5\uff08\u5220\u9664\u9644\u7740\uff09\uff0c\u7ebf\u6bb5\u6062\u590d\u4e3a\u81ea\u7531\u72b6\u6001"));  // 拆除连接（删除附着），线段恢复为自由状态
    m_btnClearConn->setCursor(Qt::PointingHandCursor);
    m_btnClearConn->setVisible(false);
    connLayout->addWidget(m_btnClearConn);
    connLayout->addStretch();
    angleLayout->addWidget(m_connRow);
}

QHBoxLayout* SegmentConnectionCard::buildAngleRow()
{
    // Angle value row: [caption] [fx] [input] [readouts] [mode toggle]
    auto* angleRow = new QHBoxLayout();
    angleRow->setSpacing(6);
    m_lblAngleCaption = new ElaText(QString::fromUtf8("\u89d2\u5ea6(\u00b0):"), 13, this);  // 角度(°):
    m_lblAngleCaption->setFixedWidth(kLabelW);
    angleRow->addWidget(m_lblAngleCaption);
    m_lblFxAngle = new ElaText(
        QStringLiteral("<i style='color:%1;'>fx</i>")
            .arg(cad::ui::Theme::tokens().text2.name()),
        13, this);
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
    // 角度/弧长切换按钮移到行尾 (成熟表单: 主值居中, 切换器贴右)。
    m_btnAngleMode = new ElaPushButton(this);
    m_btnAngleMode->setFixedSize(30, kFieldH);
    m_btnAngleMode->setCursor(Qt::PointingHandCursor);
    m_btnAngleMode->setToolTip(QString::fromUtf8("\u5207\u6362\u89d2\u5ea6/\u5f27\u957f\u6a21\u5f0f"));  // 切换角度/弧长模式
    m_btnAngleMode->setVisible(false);
    angleRow->addWidget(m_btnAngleMode);

    return angleRow;
}

void SegmentConnectionCard::buildDartRow()
{
    // 省道线 row (用户拍板 2026-08): 起点 A + 偏移点 B（只读）+ 反算跟随
    // 角度（灰只读）+ 偏移 d / 角度 β 编辑。只有 block->isDart() 时可见。
    m_dartRow = new QWidget(this);
    auto* dartLayout = new QVBoxLayout(m_dartRow);
    dartLayout->setContentsMargins(0, 0, 0, 0);
    dartLayout->setSpacing(4);
    auto* dartTop = new QHBoxLayout();
    dartTop->setSpacing(6);
    dartTop->addWidget(makeRowLabel(QString::fromUtf8("\u8d77\u70b9 A:"), m_dartRow));  // 起点 A:
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
    dartTop->addWidget(m_dartRefLabel, 1);
    dartLayout->addLayout(dartTop);
    auto* dartMid = new QHBoxLayout();
    dartMid->setSpacing(6);
    auto* lblFold = new ElaText(
        QString::fromUtf8("\u8ddf\u968f\u89d2\u5ea6\uff08\u53cd\u7b97\uff09:"), 13, m_dartRow);  // 跟随角度（反算）:
    lblFold->setFixedWidth(kLabelW);
    lblFold->setToolTip(QString::fromUtf8(
        "\u7ebf\u65b9\u5411\u76f8\u5bf9 B \u6240\u5728\u7ebf\u6bb5\u65b9\u5411\u7684\u89d2\u5ea6\uff08"
        "\u7531 A/B/d/\u03b2 \u81ea\u52a8\u53cd\u7b97\uff0c\u53ea\u8bfb\u4e0d\u53ef\u7f16\u8f91\uff09\u3002"));
    dartMid->addWidget(lblFold);
    m_dartFoldLabel = new ElaText(QString(), 13, m_dartRow);
    m_dartFoldLabel->setStyleSheet(
        QStringLiteral("color:%1; font-size:12px; text-decoration:line-through;")
            .arg(cad::ui::Theme::tokens().text3.name()));
    m_dartFoldLabel->setToolTip(QString::fromUtf8(
        "\u53cd\u7b97\u89d2\u5ea6\uff08\u6700\u7ec8\u884c\u4e3a\uff09\uff1a"
        "\u7ebf\u65b9\u5411\u76f8\u5bf9\u504f\u79fb\u70b9\u7ebf\u6bb5\u65b9\u5411\u7684\u5e26\u7b26\u53f7\u6298\u89d2\uff0c"
        "\u4e0d\u53ef\u76f4\u63a5\u7f16\u8f91\u2014\u2014\u6539\u2192\u201c\u89d2\u5ea6 \u03b2\u201d\u624d\u80fd\u6539\u53d8\u5b83\u3002"));
    dartMid->addWidget(m_dartFoldLabel, 1);
    dartLayout->addLayout(dartMid);
    auto* dartParams = new QHBoxLayout();
    dartParams->setSpacing(6);
    dartParams->addWidget(makeRowLabel(QString::fromUtf8("\u504f\u79fb d:"), m_dartRow));  // 偏移 d:
    m_dartOffsetEdit = new ElaLineEdit(m_dartRow);
    m_dartOffsetEdit->setFixedWidth(90);
    m_dartOffsetEdit->setPlaceholderText(QString::fromUtf8("mm / \u516c\u5f0f(cm)"));
    m_dartOffsetEdit->setToolTip(QString::fromUtf8(
        "\u7ec8\u70b9 E \u8ddd\u504f\u79fb\u70b9 B \u7684\u8ddd\u79bb d\uff08\u6cbf B \u6240\u5728\u7ebf\u6bb5\u65b9\u5411\u8f6c \u03b2 \u89d2\uff09\uff1b"
        "\u6b63\u8d1f\u5929\u7136\u51b3\u5b9a\u65b9\u5411\u3002\u7eaf\u6570\u503c = mm\uff1b\u516c\u5f0f = cm \u57df\u3002"));
    dartParams->addWidget(m_dartOffsetEdit);
    dartParams->addWidget(new ElaText(QString::fromUtf8("\u89d2\u5ea6 \u03b2:"), 13, m_dartRow));  // 角度 β:
    m_dartAngleEdit = new ElaLineEdit(m_dartRow);
    m_dartAngleEdit->setFixedWidth(90);
    m_dartAngleEdit->setPlaceholderText(QString::fromUtf8("\u76f8\u5bf9\u7ebf\u6bb5(\u00b0)"));
    m_dartAngleEdit->setToolTip(QString::fromUtf8(
        "\u7ec8\u70b9\u76f8\u5bf9\u504f\u79fb\u70b9\u6240\u5728\u7ebf\u6bb5\u65b9\u5411\u7684\u8f6c\u89d2"
        "\uff08\u9ed8\u8ba4 90\u00b0\uff09\uff1b\u7ebf\u6bb5\u65cb\u8f6c\u65f6\u7ec8\u70b9\u8ddf\u7740\u8f6c\u3002"));
    dartParams->addWidget(m_dartAngleEdit);
    dartParams->addStretch();
    dartLayout->addLayout(dartParams);
    connect(m_dartOffsetEdit, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onDartOffsetEdited);
    connect(m_dartAngleEdit, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onDartAngleEdited);
}

void SegmentConnectionCard::buildSlideRow()
{
    // 滑轨模式 row (抽屉式滑动, 用户拍板 2026-08): 全连接 / 沿线滑动 /
    // 垂直拉出. 进入滑轨后拖动跟随线只沿对应方向动, 角度跟随始终保留.
    // 只在连接态显示 (free 态与 m_connRow 一起隐藏).
    m_slideRow = new QWidget(this);
    auto* slideLayout = new QHBoxLayout(m_slideRow);
    slideLayout->setContentsMargins(0, 0, 0, 0);
    slideLayout->setSpacing(6);
    auto* lblSlide = makeRowLabel(QString::fromUtf8("\u6ed1\u8f68:"), m_slideRow);  // 滑轨:
    lblSlide->setToolTip(QString::fromUtf8(
        "\u62bd\u5c49\u5f0f\u5355\u5411\u6ed1\u52a8\uff08\u7528\u6237\u62cd\u677f 2026-08\uff09\uff1a"
        "\u8fde\u63a5\u59ff\u6001\u4fdd\u6301\uff08\u89d2\u5ea6\u59cb\u7ec8\u968f\u57fa\u51c6\u7ebf\uff09\uff0c"
        "\u4f46\u4f4d\u7f6e\u53ea\u7559\u4e00\u4e2a\u81ea\u7531\u5ea6\u3002\u300c\u6cbf\u7ebf\u6ed1\u52a8\u300d="
        "\u8fde\u63a5\u70b9\u6cbf\u57fa\u51c6\u7ebf\u65b9\u5411\u6ed1\uff08\u5782\u76f4\u504f\u79fb\u9501\u5b9a\uff09\uff1b"
        "\u300c\u5782\u76f4\u62c9\u51fa\u300d=\u8fde\u63a5\u70b9\u5782\u76f4\u57fa\u51c6\u7ebf\u62c9\u52a8"
        "\uff08\u6cbf\u7ebf\u4f4d\u7f6e\u9501\u5b9a\uff09\uff1b\u57fa\u51c6\u7ebf\u65cb\u8f6c\u65f6\u6ed1\u8f68\u8ddf\u7740\u8f6c\u3002"
        "\u5207\u56de\u300c\u5168\u8fde\u63a5\u300d\u4f4d\u7f6e\u91cd\u65b0\u5438\u9644\u56de\u5bbf\u4e3b\u70b9\u3002"));
    slideLayout->addWidget(lblSlide);
    slideLayout->addWidget(new ElaText(QString::fromUtf8("水平(cm):"), 13, m_slideRow));
    m_editSlideAlong = new ElaLineEdit(m_slideRow);
    m_editSlideAlong->setFixedWidth(90);
    m_editSlideAlong->setPlaceholderText(QString::fromUtf8("0"));
    m_editSlideAlong->setToolTip(QString::fromUtf8(
        "沿基准线方向偏移（cm）。留空/0 表示不偏移。"));
    slideLayout->addWidget(m_editSlideAlong);
    slideLayout->addWidget(new ElaText(QString::fromUtf8("垂直(cm):"), 13, m_slideRow));
    m_editSlidePerp = new ElaLineEdit(m_slideRow);
    m_editSlidePerp->setFixedWidth(90);
    m_editSlidePerp->setPlaceholderText(QString::fromUtf8("0"));
    m_editSlidePerp->setToolTip(QString::fromUtf8(
        "垂直基准线方向偏移（cm）。留空/0 表示不偏移。"));
    slideLayout->addWidget(m_editSlidePerp);
    m_cmbSlideMode = new ElaComboBox(m_slideRow);
    m_cmbSlideMode->addItem(QString::fromUtf8("\u5168\u8fde\u63a5"));
    m_cmbSlideMode->addItem(QString::fromUtf8("\u6cbf\u7ebf\u6ed1\u52a8"));
    m_cmbSlideMode->addItem(QString::fromUtf8("\u5782\u76f4\u62c9\u51fa"));
    m_cmbSlideMode->setVisible(false);
    m_lblSlideBadge = new ElaText(QString(), 13, m_slideRow);
    m_lblSlideBadge->setStyleSheet(cad::ui::Theme::tealBadgeStyle());
    m_lblSlideBadge->setToolTip(QString::fromUtf8(
        "\u6ed1\u8f68\u72b6\u6001\uff1a\u89d2\u5ea6\u8ddf\u968f\u4fdd\u6301\uff0c"
        "\u4f46\u4f4d\u7f6e\u53ea\u7559\u4e00\u4e2a\u81ea\u7531\u5ea6\uff08\u62bd\u5c49\u5f0f\u5355\u5411\u6ed1\u52a8\uff09\u3002"
        "\u514d\u91cd\u65b0\u8d34\u5408\u57fa\u51c6\u7ebf\u540e\uff0c\u952e\u76d8\u62d6\u52a8\u8ddf\u968f\u7ebf\u4ec5\u6cbf\u6b64\u65b9\u5411\u79fb\u52a8\u3002"));
    slideLayout->addWidget(m_lblSlideBadge);
    slideLayout->addStretch();
}

QHBoxLayout* SegmentConnectionCard::buildConnControls()
{
    // Follow-host checkbox (ALL lines): checked = start point follows the
    // connection target; unchecking detaches (visible via refreshCard()).
    m_chkFollowHost = new ElaCheckBox(this);

    // 拖动保护 checkbox (ALL lines, no hidden items): checked = 焊接
    // (isLocked — 拖任一端整个对一起移动, 不拆)。**新建连接默认勾选**
    // (用户拍板 2026-08 复旧): 整对移动是默认语义; 取消勾选 = 解焊仍完整
    // 连接 (拖跟随线可拆散); 拆散入口 = D 键快拆 / 面板取消「拖动保护」;
    // 多线整体移动仍可交给组件 (componentClosure)。
    m_chkLockConn = new ElaCheckBox(QString::fromUtf8("连接保护"), this);  // 连接保护（拖动不拆）

    m_chkLockConn->setToolTip(QString::fromUtf8(
        "\u65b0\u5efa\u8fde\u63a5\u9ed8\u8ba4\u52fe\u9009\uff1a\u62d6\u52a8\u4efb\u4e00\u7aef\u65f6\u6574\u4e2a\u5bf9\u4e00\u8d77\u79fb\u52a8\uff0c\u4e0d\u4f1a\u88ab\u62d6\u62c6\uff1b"
        "\u53d6\u6d88\u52fe\u9009 = \u89e3\u9664\u710a\u63a5\uff08\u8fde\u63a5\u4fdd\u6301\uff0c\u62d6\u8ddf\u968f\u7ebf\u5373\u53ef\u62c6\u6563\uff0c\u62c6\u6563\u8d70 D \u952e\u5feb\u62c6\uff09\u3002"));

    // 角度独立 (用户新需求 2026): 位置仍吸附, 但本线角度不随基准线, 由自己控制。
    m_chkAngleIndependent = new ElaCheckBox(QString::fromUtf8("独立角度"), this);
    m_chkAngleIndependent->setToolTip(QString::fromUtf8(
        "勾选后：位置仍吸附在基准点，但本线角度不再跟随基准线，"
        "可用旋转/角度公式自由控制。"
        "取消勾选恢复角度跟随（自动反算当前角度，无跳变）。"));

    auto* connCtrls = new QHBoxLayout();
    connCtrls->setSpacing(12);
    connCtrls->addWidget(m_chkFollowHost);
    connCtrls->addWidget(m_chkLockConn);
    connCtrls->addWidget(m_chkAngleIndependent);
    connCtrls->addStretch();

    return connCtrls;
}

void SegmentConnectionCard::buildAngleRefRow()
{
    // 角度基准分离 (用户需求 2026): 位置锚点不变，角度可由另一条线段约束。
    // 这里通过选择一个“角度基准点”来代表那条线段（取该点所在线段的方向）。
    m_angleRefRow = new QWidget(this);
    auto* angleRefLayout = new QHBoxLayout(m_angleRefRow);
    angleRefLayout->setContentsMargins(0, 0, 0, 0);
    angleRefLayout->setSpacing(6);
    auto* lblAngleRef = makeRowLabel(QString::fromUtf8("引用线段:"), m_angleRefRow);
    lblAngleRef->setToolTip(QString::fromUtf8(
        "角度引用线段。可输入线段 L#/名称，或该线段上的点 P#。"));
    angleRefLayout->addWidget(lblAngleRef);
    m_lblAngleRefSeg = makeRefEdit(m_angleRefRow);
    m_lblAngleRefSeg->setPlaceholderText(QString::fromUtf8("线段 L# / 点 P#"));
    m_lblAngleRefSeg->setToolTip(QString::fromUtf8("输入角度基准线段 ID/名称，或该线段上的点 ID"));
    m_lblAngleRefSeg->setStyleSheet("font-size:12px;");
    angleRefLayout->addWidget(m_lblAngleRefSeg);
    angleRefLayout->addWidget(new ElaText(QString::fromUtf8("引用点:"), 13, m_angleRefRow));
    m_angleRefPoint = new PointRefEdit(m_doc, m_angleRefRow);
    m_angleRefPoint->setFixedWidth(150);
    m_angleRefPoint->setToolTip(QString::fromUtf8(
        "角度引用点，必须属于上方选定的引用线段。"));
    angleRefLayout->addWidget(m_angleRefPoint);

    m_btnClearAngleRef = new ElaPushButton(QString::fromUtf8("清除"), m_angleRefRow);
    m_btnClearAngleRef->setFixedHeight(kFieldH);
    m_btnClearAngleRef->setToolTip(QString::fromUtf8("恢复默认：角度跟随位置宿主线段"));
    m_btnClearAngleRef->setCursor(Qt::PointingHandCursor);
    angleRefLayout->addWidget(m_btnClearAngleRef);
    angleRefLayout->addStretch();
}

void SegmentConnectionCard::buildAimRow()
{
    // ── 指向（终点指向）并入连接卡片 ──
    m_aimRow = new QWidget(this);
    auto* aimLayout = new QHBoxLayout(m_aimRow);
    aimLayout->setContentsMargins(0, 0, 0, 0);
    aimLayout->setSpacing(6);
    aimLayout->addWidget(makeRowLabel(QString::fromUtf8("指向点:"), m_aimRow));
    m_refAimPoint = new PointRefEdit(m_doc, m_aimRow);
    m_refAimPoint->setFixedWidth(150);
    m_refAimPoint->setToolTip(QString::fromUtf8(
        "终点方向指向该点；配合长度可让终点落在目标上。"));
    aimLayout->addWidget(m_refAimPoint);
    aimLayout->addWidget(new ElaText(QString::fromUtf8("偏移(°):"), 13, m_aimRow));
    m_editAimOffset = new ElaLineEdit(m_aimRow);
    m_editAimOffset->setFixedWidth(70);
    m_editAimOffset->setPlaceholderText(QString::fromUtf8("0"));
    m_editAimOffset->setToolTip(QString::fromUtf8(
        "相对精确指向方向的偏移角，0 = 精确指向目标点。"));
    aimLayout->addWidget(m_editAimOffset);
    m_btnClearAim = new ElaPushButton(QString::fromUtf8("清除"), m_aimRow);
    m_btnClearAim->setFixedHeight(kFieldH);
    m_btnClearAim->setToolTip(QString::fromUtf8("解除终点指向约束"));
    m_btnClearAim->setCursor(Qt::PointingHandCursor);
    aimLayout->addWidget(m_btnClearAim);
    aimLayout->addStretch();
}

void SegmentConnectionCard::connectSignals()
{
    // Angle mode toggle (angle ↔ arc length) for follower lines.
    connect(m_btnAngleMode, &QPushButton::clicked,
            this, &SegmentConnectionCard::onModeToggle);
    connect(m_refConnPoint, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onConnPointResolved);
    connect(m_btnClearConn, &QPushButton::clicked,
            this, &SegmentConnectionCard::onClear);
    connect(m_chkFollowHost, &QCheckBox::toggled,
            this, &SegmentConnectionCard::onFollowHostToggled);
    connect(m_chkLockConn, &QCheckBox::toggled,
            this, &SegmentConnectionCard::onLockToggled);
    connect(m_chkAngleIndependent, &QCheckBox::toggled,
            this, &SegmentConnectionCard::onAngleIndependentToggled);
    connect(m_angleRefPoint, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onAngleRefPointResolved);
    connect(m_btnClearAngleRef, &QPushButton::clicked,
            this, &SegmentConnectionCard::onClearAngleRef);
    connect(m_cmbSlideMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SegmentConnectionCard::onSlideModeChanged);
    connect(m_cmbMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SegmentConnectionCard::onModeChanged);
    connect(m_editSlideAlong, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onSlideOffsetEdited);
    connect(m_editSlidePerp, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onSlideOffsetEdited);
    connect(m_refAimPoint, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onAimTargetResolved);
    connect(m_editAimOffset, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onAimOffsetApply);
    connect(m_btnClearAim, &QPushButton::clicked,
            this, &SegmentConnectionCard::onClearAim);
    connect(m_btnAngleOnly, &QPushButton::clicked,
            this, &SegmentConnectionCard::onAngleOnlyClicked);
    connect(m_lblLeaderRef, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onLeaderSegEdited);
    connect(m_lblAngleRefSeg, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onAngleRefSegEdited);
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

} // namespace cad::tools
