#include "ui/SegmentConnectionCard.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QButtonGroup>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaComboBox.h"
#include <QPushButton>
#include <QSignalBlocker>
#include <QComboBox>

#include "ui/PointRefEdit.h"
#include "ui/FormScaffold.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "geometry/Units.h"
#include "ui/Theme.h"

namespace cad::ui {

namespace {

/// 行内控件统一高度 (2026-xx 紧凑化: 35→30, 与状态栏对齐)。
constexpr int kFieldH = 30;
/// 行首标签列固定宽度 (2026-12 去卡框化: 短词列, 各行对齐)。
constexpr int kLabelW = 64;
/// 次级标签列 (如「连接点」与主轴标签同列, 使输入框垂直成列)。
constexpr int kSubLabelW = 64;
/// 统一小按钮宽 (拆开/重连/归零, 二字按钮; 2026-xx 紧凑 58→48)。
constexpr int kBtnW = 48;

ElaText* makeRowLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new ElaText(text, 11, parent);
    lbl->setFixedWidth(kLabelW);
    return lbl;
}

/// 输入框紧凑化 (2026-xx 与状态栏对齐): 高 30 + 11px 紧凑字。
void tuneEdit(ElaLineEdit* edit)
{
    edit->setFixedHeight(kFieldH);
    edit->setStyleSheet(QStringLiteral("font-size: 11px;"));
}

void tuneButton(QPushButton* btn)
{
    btn->setFixedSize(kBtnW, kFieldH);
    btn->setStyleSheet(cad::ui::chipButtonStyle());
    btn->setCursor(Qt::PointingHandCursor);
}

} // namespace

// ── UI 构建 (SegmentConnectionCardBuild.cpp, 2026-12 去卡框化): ──
// 起点连接行两行结构 / 终点连接行组 (2026-xx 每端完整连接) + 信号接线。
// 规范: 标签列 64 统一 (11px 短词); 输入 140; 按钮 48×30; 无 stretch 自适应。

void SegmentConnectionCard::buildConnRow(QVBoxLayout* lay)
{
    m_connRow = new QWidget(this);
    auto* connV = new QVBoxLayout(m_connRow);
    connV->setContentsMargins(0, 0, 0, 0);
    connV->setSpacing(6);

    // 行组 caption「起点连接」(2026-xx 每端完整连接: 起点 = Attachment 位置+角度)。
    auto* startCap = new ElaText(QString::fromUtf8("起点连接"), 12, m_connRow);
    startCap->setStyleSheet(QStringLiteral("font-size:12px; font-weight:600;"));
    startCap->setToolTip(QString::fromUtf8(
        "起点连接 = 本线作为跟随线吸附到基准线段（位置 + 角度跟随）。"));
    connV->addWidget(startCap);

    // 行1: [连接线段][L#·名 140]
    auto* leaderRow = new QHBoxLayout();
    leaderRow->setSpacing(6);
    m_lblConnLabel = makeRowLabel(QString::fromUtf8("连接线段"), m_connRow);
    leaderRow->addWidget(m_lblConnLabel);
    // 2026-08 统一解析 (§6.6 收口扩展): 「连接线段」框也用 PointRefEdit ——
    // 与「连接点」框同一套同名弹窗消歧 / 完整串号 / 名称大小写规则,
    // 替代旧匿名 resolveSegmentInput (静默取首个命中, 无任何反馈)。
    m_refLeaderSeg = new PointRefEdit(m_doc, m_connRow);
    m_refLeaderSeg->setObjectName(QStringLiteral("connSegEdit"));
    m_refLeaderSeg->setFixedWidth(140);
    m_refLeaderSeg->setToolTip(QString::fromUtf8(
        "输入连接线段 ID/名称，或该线段上的点 ID；同名会弹窗选择"));
    leaderRow->addWidget(m_refLeaderSeg);
    leaderRow->addStretch();
    connV->addLayout(leaderRow);

    // 行2: [连接点][P# 140][拆开/重连]
    auto* pointRow = new QHBoxLayout();
    pointRow->setSpacing(6);
    m_lblConnSub = new ElaText(QString::fromUtf8("连接点"), 11, m_connRow);
    m_lblConnSub->setFixedWidth(kSubLabelW);
    pointRow->addWidget(m_lblConnSub);
    m_refConnPoint = new PointRefEdit(m_doc, m_connRow);
    m_refConnPoint->setObjectName(QStringLiteral("connPointEdit"));
    m_refConnPoint->setFixedWidth(140);
    m_refConnPoint->setFixedHeight(kFieldH);
    pointRow->addWidget(m_refConnPoint);
    m_btnDetach = new QPushButton(QString::fromUtf8("拆开"), m_connRow);
    m_btnDetach->setObjectName(QStringLiteral("connDetachBtn"));
    tuneButton(m_btnDetach);
    m_btnDetach->setToolTip(QString::fromUtf8(
        "拆开 = 解除位置吸附（角度仍跟随基准线）；重连 = 位置重新吸附回原宿主并重新焊接"));
    m_btnDetach->setVisible(false);
    pointRow->addWidget(m_btnDetach);
    pointRow->addStretch();
    connV->addLayout(pointRow);

    lay->addWidget(m_connRow);
}

void SegmentConnectionCard::buildEndRow(QVBoxLayout* lay)
{
    // 终点连接行组 (2026-xx 每端完整连接): 引擎载体 = Block::endTarget 终点
    // 指向 (Resolver Step 7, 旋转指向目标点)。行1 连接线段; 行2 连接点 +
    // 偏移(°) + 拆开/重连。双端都连上 = 桥接线 (基准线行隐藏、角度/滑轨/影子
    // 禁用 —— 互斥, 见 SegmentRefCard/refreshUnifiedState)。
    m_endRow = new QWidget(this);
    auto* endV = new QVBoxLayout(m_endRow);
    endV->setContentsMargins(0, 0, 0, 0);
    endV->setSpacing(6);

    auto* endCap = new ElaText(QString::fromUtf8("终点连接"), 12, m_endRow);
    endCap->setStyleSheet(QStringLiteral("font-size:12px; font-weight:600;"));
    endCap->setToolTip(QString::fromUtf8(
        "终点连接 = 终点指向目标线段上的目标点（旋转指向）；"
        "「自动」长度模式会发布测量驱动长度，终点精确落在目标点上。"
        "两端都连上 = 桥接线。"));
    endV->addWidget(endCap);

    // 行1: [连接线段][L#·名 140]
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(6);
    m_lblEndConnLabel = makeRowLabel(QString::fromUtf8("连接线段"), m_endRow);
    row1->addWidget(m_lblEndConnLabel);
    // 与起点行同规: PointRefEdit 统一解析 (2026-08)。
    m_refEndLeaderSeg = new PointRefEdit(m_doc, m_endRow);
    m_refEndLeaderSeg->setObjectName(QStringLiteral("endConnSegEdit"));
    m_refEndLeaderSeg->setFixedWidth(140);
    m_refEndLeaderSeg->setToolTip(QString::fromUtf8(
        "输入终点连接线段 ID/名称，或该线段上的点 ID；同名会弹窗选择"));
    row1->addWidget(m_refEndLeaderSeg);
    row1->addStretch();
    endV->addLayout(row1);

    // 行2: [连接点][P# 140][偏移(°)][70][拆开/重连]
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(6);
    m_lblEndConnSub = new ElaText(QString::fromUtf8("连接点"), 11, m_endRow);
    m_lblEndConnSub->setFixedWidth(kSubLabelW);
    row2->addWidget(m_lblEndConnSub);
    m_refEndPoint = new PointRefEdit(m_doc, m_endRow);
    m_refEndPoint->setObjectName(QStringLiteral("endConnPointEdit"));
    m_refEndPoint->setFixedWidth(140);
    m_refEndPoint->setFixedHeight(kFieldH);
    m_refEndPoint->setToolTip(QString::fromUtf8(
        "输入目标点 P 编号回车建立终点连接（终点指向该点）；已有指向则重定向。"));
    row2->addWidget(m_refEndPoint);
    auto* lblOff = new ElaText(QString::fromUtf8("偏移(°)"), 11, m_endRow);
    row2->addWidget(lblOff);
    m_editEndOffset = new ElaLineEdit(m_endRow);
    m_editEndOffset->setFixedWidth(70);
    m_editEndOffset->setPlaceholderText(QString::fromUtf8("0"));
    m_editEndOffset->setToolTip(QString::fromUtf8(
        "相对精确指向方向的偏移角，0 = 精确指向目标点。"));
    tuneEdit(m_editEndOffset);
    row2->addWidget(m_editEndOffset);
    m_btnEndDetach = new QPushButton(QString::fromUtf8("拆开"), m_endRow);
    m_btnEndDetach->setObjectName(QStringLiteral("endConnDetachBtn"));
    tuneButton(m_btnEndDetach);
    m_btnEndDetach->setToolTip(QString::fromUtf8(
        "拆开 = 清除终点连接（终点恢复自由，已发布的长度测量保留）；"
        "重连 = 恢复到最近一次的目标点（宿主已删除则需重新输入）。"));
    row2->addWidget(m_btnEndDetach);
    row2->addStretch();
    endV->addLayout(row2);
}

void SegmentConnectionCard::connectSignals()
{
    connect(m_refConnPoint, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onConnPointResolved);
    connect(m_btnDetach, &QPushButton::clicked,
            this, &SegmentConnectionCard::onDetachClicked);
    // 「连接线段」框: PointRefEdit 解析成功 (L#/名称→线段起点 或 P#→该点)
    // 才发信号; 失败自带红闪回显, 无需 refreshCard 回滚。
    connect(m_refLeaderSeg, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onLeaderSegResolved);
    // ── 终点连接行 (2026-xx 每端完整连接) ──
    connect(m_refEndPoint, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onEndConnPointResolved);
    connect(m_refEndLeaderSeg, &PointRefEdit::pointResolved,
            this, &SegmentConnectionCard::onEndLeaderSegResolved);
    connect(m_editEndOffset, &ElaLineEdit::editingFinished,
            this, &SegmentConnectionCard::onEndOffsetEdited);
    connect(m_btnEndDetach, &QPushButton::clicked,
            this, &SegmentConnectionCard::onEndDetachClicked);
}

} // namespace cad::ui
