#include "ui/SegmentConnectionCard.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include "ElaComboBox.h"
#include <QSignalBlocker>
#include <QComboBox>

#include "ui/PointRefEdit.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "geometry/Units.h"
#include "ui/Theme.h"

namespace cad::ui {

namespace {

/// 行内控件统一高度 (全页规范: 35px)。
constexpr int kFieldH = 35;
/// 行首标签列固定宽度 (2026-12 去卡框化: 短词列, 各行对齐)。
constexpr int kLabelW = 64;
/// 次级标签列 (如「连接点」与主轴标签同列, 使输入框垂直成列)。
constexpr int kSubLabelW = 64;
/// 统一小按钮宽 (清除/拆开, 二字按钮)。
constexpr int kBtnW = 58;

ElaText* makeRowLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new ElaText(text, 12, parent);
    lbl->setFixedWidth(kLabelW);
    return lbl;
}

void tuneButton(ElaPushButton* btn)
{
    btn->setFixedSize(kBtnW, kFieldH);
    btn->setCursor(Qt::PointingHandCursor);
}

} // namespace

// ── UI 构建 (SegmentConnectionCardBuild.cpp, 2026-12 去卡框化): ──
// 连接行两行结构 / 滑轨行 / 省道行 + 信号接线。
// 规范: 标签列 64 统一 (12px 短词); 输入 140; 按钮 58×35; 无 stretch 自适应。

void SegmentConnectionCard::buildConnRow(QVBoxLayout* lay)
{
    m_connRow = new QWidget(this);
    auto* connV = new QVBoxLayout(m_connRow);
    connV->setContentsMargins(0, 0, 0, 0);
    connV->setSpacing(6);

    // 行1: [连接线段][L#·名 140]
    auto* leaderRow = new QHBoxLayout();
    leaderRow->setSpacing(6);
    m_lblConnLabel = makeRowLabel(QString::fromUtf8("连接线段"), m_connRow);
    leaderRow->addWidget(m_lblConnLabel);
    m_lblLeaderRef = new ElaLineEdit(m_connRow);
    m_lblLeaderRef->setFixedWidth(140);
    m_lblLeaderRef->setPlaceholderText(QString::fromUtf8("线段 L# / 点 P#"));
    m_lblLeaderRef->setToolTip(QString::fromUtf8(
        "输入连接线段 ID/名称，或该线段上的点 ID"));
    m_lblLeaderRef->setStyleSheet("font-size:12px;");
    leaderRow->addWidget(m_lblLeaderRef);
    leaderRow->addStretch();
    connV->addLayout(leaderRow);

    // 行2: [连接点][P# 140][拆开/重连]
    auto* pointRow = new QHBoxLayout();
    pointRow->setSpacing(6);
    m_lblConnSub = new ElaText(QString::fromUtf8("连接点"), 12, m_connRow);
    m_lblConnSub->setFixedWidth(kSubLabelW);
    pointRow->addWidget(m_lblConnSub);
    m_refConnPoint = new PointRefEdit(m_doc, m_connRow);
    m_refConnPoint->setFixedWidth(140);
    m_refConnPoint->setFixedHeight(kFieldH);
    pointRow->addWidget(m_refConnPoint);
    m_btnDetach = new ElaPushButton(QString::fromUtf8("拆开"), m_connRow);
    tuneButton(m_btnDetach);
    m_btnDetach->setToolTip(QString::fromUtf8(
        "拆开 = 解除位置吸附（角度仍跟随基准线）；重连 = 位置重新吸附回原宿主并重新焊接"));
    m_btnDetach->setVisible(false);
    pointRow->addWidget(m_btnDetach);
    pointRow->addStretch();
    connV->addLayout(pointRow);

    m_lblLayerBadge = new ElaText(QString(), 13, m_connRow);
    m_lblLayerBadge->setStyleSheet(cad::ui::Theme::purpleBadgeStyle());
    m_lblLayerBadge->setToolTip(QString::fromUtf8(
        "跨层连接：基准线位于另一图层"));
    m_lblLayerBadge->setVisible(false);
    m_lblAngleOnlyBadge = new ElaText(QString(), 13, m_connRow);
    m_lblAngleOnlyBadge->setStyleSheet(cad::ui::Theme::tealBadgeStyle());
    m_lblAngleOnlyBadge->setVisible(false);

    lay->addWidget(m_connRow);
}

void SegmentConnectionCard::buildDartRow()
{
    // 省道线 rows (2026-08) —— 只有 block->isDart() 时可见.
    m_dartRow = new QWidget(this);
    auto* dartLayout = new QVBoxLayout(m_dartRow);
    dartLayout->setContentsMargins(0, 0, 0, 0);
    dartLayout->setSpacing(4);
    auto* dartTop = new QHBoxLayout();
    dartTop->setSpacing(6);
    dartTop->addWidget(makeRowLabel(QString::fromUtf8("起点 A"), m_dartRow));
    m_dartStartRef = new ElaText(QString(), 12, m_dartRow);
    m_dartStartRef->setStyleSheet("font-size:12px;");
    dartTop->addWidget(m_dartStartRef, 1);
    dartTop->addWidget(new ElaText(QString::fromUtf8("偏移点 B"), 12, m_dartRow));
    m_dartRefLabel = new ElaText(QString(), 12, m_dartRow);
    m_dartRefLabel->setStyleSheet("font-size:12px;");
    m_dartRefLabel->setToolTip(QString::fromUtf8(
        "偏移点 B 所在线段即角度基准：线段旋转时本线跟着转。"));
    dartTop->addWidget(m_dartRefLabel, 1);
    dartLayout->addLayout(dartTop);
    auto* dartMid = new QHBoxLayout();
    dartMid->setSpacing(6);
    auto* lblFold = new ElaText(
        QString::fromUtf8("反算角"), 12, m_dartRow);
    lblFold->setFixedWidth(kLabelW);
    lblFold->setToolTip(QString::fromUtf8(
        "跟随角度（反算）：线方向相对 B 所在线段方向的角度"
        "（由 A/B/d/β 自动反算，只读不可编辑）。"));
    dartMid->addWidget(lblFold);
    m_dartFoldLabel = new ElaText(QString(), 12, m_dartRow);
    m_dartFoldLabel->setStyleSheet(
        QStringLiteral("color:%1; font-size:12px; text-decoration:line-through;")
            .arg(cad::ui::Theme::tokens().text3.name()));
    m_dartFoldLabel->setToolTip(QString::fromUtf8(
        "反算角度（最终行为）：不可直接编辑——改→“角度 β”才能改变它。"));
    dartMid->addWidget(m_dartFoldLabel, 1);
    dartLayout->addLayout(dartMid);
    auto* dartParams = new QHBoxLayout();
    dartParams->setSpacing(6);
    dartParams->addWidget(makeRowLabel(QString::fromUtf8("偏移 d"), m_dartRow));
    m_dartOffsetEdit = new ElaLineEdit(m_dartRow);
    m_dartOffsetEdit->setFixedWidth(90);
    m_dartOffsetEdit->setPlaceholderText(QString::fromUtf8("mm / 公式(cm)"));
    m_dartOffsetEdit->setToolTip(QString::fromUtf8(
        "终点 E 距偏移点 B 的距离 d（沿 B 所在线段方向转 β 角），正负天然决定方向。"));
    dartParams->addWidget(m_dartOffsetEdit);
    dartParams->addWidget(new ElaText(QString::fromUtf8("角度 β"), 12, m_dartRow));
    m_dartAngleEdit = new ElaLineEdit(m_dartRow);
    m_dartAngleEdit->setFixedWidth(90);
    m_dartAngleEdit->setPlaceholderText(QString::fromUtf8("相对线段(°)"));
    m_dartAngleEdit->setToolTip(QString::fromUtf8(
        "终点相对偏移点所在线段方向的转角（默认 90°）。"));
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
    // 滑轨行: [滑轨][水平][90][垂直][90] (无连接/拆开禁用仍显示; cm 域).
    m_slideRow = new QWidget(this);
    auto* slideLayout = new QHBoxLayout(m_slideRow);
    slideLayout->setContentsMargins(0, 0, 0, 0);
    slideLayout->setSpacing(6);
    auto* lblSlide = makeRowLabel(QString::fromUtf8("滑轨"), m_slideRow);
    lblSlide->setToolTip(QString::fromUtf8(
        "抽屉式单向滑动：连接姿态保持（角度始终随基准线），但位置只留一个自由度。"));
    slideLayout->addWidget(lblSlide);
    slideLayout->addWidget(new ElaText(QString::fromUtf8("水平"), 12, m_slideRow));
    m_editSlideAlong = new ElaLineEdit(m_slideRow);
    m_editSlideAlong->setFixedWidth(90);
    m_editSlideAlong->setPlaceholderText(QString::fromUtf8("0"));
    m_editSlideAlong->setToolTip(QString::fromUtf8(
        "沿基准线方向偏移（cm）。数值或公式（如 肩宽/2）；留空/0 表示不偏移。"));
    slideLayout->addWidget(m_editSlideAlong);
    slideLayout->addWidget(new ElaText(QString::fromUtf8("垂直"), 12, m_slideRow));
    m_editSlidePerp = new ElaLineEdit(m_slideRow);
    m_editSlidePerp->setFixedWidth(90);
    m_editSlidePerp->setPlaceholderText(QString::fromUtf8("0"));
    m_editSlidePerp->setToolTip(QString::fromUtf8(
        "垂直基准线方向偏移（cm）。数值或公式；留空/0 表示不偏移。"));
    slideLayout->addWidget(m_editSlidePerp);
    m_cmbSlideMode = new ElaComboBox(m_slideRow);
    m_cmbSlideMode->addItem(QString::fromUtf8("全连接"));
    m_cmbSlideMode->addItem(QString::fromUtf8("沿线滑动"));
    m_cmbSlideMode->addItem(QString::fromUtf8("垂直拉出"));
    m_cmbSlideMode->setVisible(false);
    m_lblSlideBadge = new ElaText(QString(), 13, m_slideRow);
    m_lblSlideBadge->setStyleSheet(cad::ui::Theme::tealBadgeStyle());
    m_lblSlideBadge->setToolTip(QString::fromUtf8(
        "滑轨状态：角度跟随保持，但位置只留一个自由度。"));
    slideLayout->addWidget(m_lblSlideBadge);
    slideLayout->addStretch();
}

void SegmentConnectionCard::buildShadowRow()
{
    // 影子偏转读数行 (§2.6, 2026-08-27): 只读「随组偏转 N°」+ 归零。
    // offset==0 时整行隐藏 (默认零干扰); 值由选集旋转写入。
    m_shadowRow = new QWidget(this);
    auto* shadowLayout = new QHBoxLayout(m_shadowRow);
    shadowLayout->setContentsMargins(0, 0, 0, 0);
    shadowLayout->setSpacing(8);
    auto* lblShadowTitle = new ElaText(QString::fromUtf8("基准偏转"), 12, m_shadowRow);
    lblShadowTitle->setFixedHeight(kFieldH);
    lblShadowTitle->setMinimumHeight(kFieldH);
    shadowLayout->addWidget(lblShadowTitle);
    m_lblShadowValue = new ElaText(QString(), 12, m_shadowRow);
    m_lblShadowValue->setFixedHeight(kFieldH);   // v3 行内统一高度
    m_lblShadowValue->setMinimumHeight(kFieldH);
    m_lblShadowValue->setToolTip(QString::fromUtf8(
        "影子基准相对基准线累计偏转的度数（选集/整组刚体旋转时产生）："
        "本线的角度跟随仍归基准线，但整体叠加此偏转——像给基准配了一根随组转动的隐形影子。"
        "归零 = 本线转回与基准当前方向对齐。"));
    shadowLayout->addWidget(m_lblShadowValue);
    m_btnShadowReset = new ElaPushButton(QString::fromUtf8("归零"), m_shadowRow);
    tuneButton(m_btnShadowReset);
    connect(m_btnShadowReset, &QPushButton::clicked,
            this, &SegmentConnectionCard::onShadowResetClicked);
    shadowLayout->addWidget(m_btnShadowReset);
    shadowLayout->addStretch();
    m_shadowRow->setVisible(false);
}

void SegmentConnectionCard::connectSignals()
{
    connect(m_refConnPoint, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onConnPointResolved);
    connect(m_cmbSlideMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SegmentConnectionCard::onSlideModeChanged);
    connect(m_editSlideAlong, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onSlideOffsetEdited);
    connect(m_editSlidePerp, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onSlideOffsetEdited);
    connect(m_btnDetach, &QPushButton::clicked,
            this, &SegmentConnectionCard::onDetachClicked);
    connect(m_lblLeaderRef, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onLeaderSegEdited);
    connect(m_doc, &cad::param::ParamDocument::resolved,
            this, &SegmentConnectionCard::onDocResolved);
}

} // namespace cad::ui
