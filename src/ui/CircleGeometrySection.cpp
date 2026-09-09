#include "ui/CircleGeometrySection.h"

#include <algorithm>
#include <cmath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

#include "ElaText.h"
#include "ElaLineEdit.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/LinkedVariable.h"
#include "document/CommandTexts.h"
#include "document/commands/CircleCommands.h"
#include "document/commands/LinkedVariableCommands.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "ui/Theme.h"
#include "ui/FormScaffold.h"
#include "ui/TooltipFormatter.h"

namespace cad::ui {

namespace {

constexpr int kLabelW = 64;
constexpr int kFieldW = 150;
constexpr int kFieldH = 30;

ElaLineEdit* addFieldRow(QWidget* parent, QVBoxLayout* col,
                         const QString& labelText, const QString& placeholder,
                         const QString& unitText, const QString& tip,
                         ElaText** outFx)
{
    auto* row = new QHBoxLayout();
    row->setSpacing(6);

    auto* lbl = new ElaText(labelText, 11, parent);
    lbl->setFixedWidth(kLabelW);
    row->addWidget(lbl);

    auto* fx = new ElaText(
        QStringLiteral("<i style='color:%1;'>fx</i>")
            .arg(cad::ui::Theme::tokens().text2.name()),
        11, parent);
    fx->setVisible(false);
    fx->setFixedWidth(18);
    row->addWidget(fx);
    if (outFx) *outFx = fx;

    auto* edit = makeCompactEdit(parent, kFieldW);
    edit->setPlaceholderText(placeholder);
    if (!tip.isEmpty())
        edit->setToolTip(tip);
    row->addWidget(edit);

    if (!unitText.isEmpty()) {
        auto* unit = new ElaText(unitText, 11, parent);
        unit->setStyleSheet(cad::ui::Theme::dimValueStyle());
        row->addWidget(unit);
    }

    row->addStretch();
    col->addLayout(row);
    return edit;
}

} // namespace

CircleGeometrySection::CircleGeometrySection(cad::param::ParamDocument* paramDoc,
                                             CanvasScene* scene,
                                             QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(paramDoc)
    , m_scene(scene)
{
    buildRows();
}

void CircleGeometrySection::buildRows()
{
    const QString chips = cad::ui::chipButtonStyle();
    const QString dimMono = cad::ui::Theme::dimValueStyle();

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    m_editRadius = addFieldRow(
        this, col, QString::fromUtf8("半径"),
        cad::ui::kPlaceholderCmOrFormula, QStringLiteral("cm"),
        cad::ui::TooltipFormatter::action(
            QStringLiteral("半径"),
            QString::fromUtf8("圆心到圆周的距离。圆的唯一权威尺寸，直径/周长都换算到这里")),
        &m_lblFxR);
    connect(m_editRadius, &QLineEdit::editingFinished, this, [this] {
        emit liveUpdated();
    });

    m_editDiameter = addFieldRow(
        this, col, QString::fromUtf8("直径"),
        cad::ui::kPlaceholderCmOrFormula, QStringLiteral("cm"),
        cad::ui::TooltipFormatter::action(
            QStringLiteral("直径"),
            QString::fromUtf8("2 × 半径。输入直径会换算成半径（公式按原式除以 2 写入半径公式）")),
        &m_lblFxD);
    connect(m_editDiameter, &QLineEdit::editingFinished,
            this, &CircleGeometrySection::onDiameterEdited);

    m_editCircumference = addFieldRow(
        this, col, QString::fromUtf8("周长"),
        cad::ui::kPlaceholderCmOrFormula, QStringLiteral("cm"),
        cad::ui::TooltipFormatter::action(
            QStringLiteral("周长"),
            QString::fromUtf8("2π × 半径。输入周长会换算成半径（公式按原式除以 2π 写入半径公式）")),
        &m_lblFxC);
    connect(m_editCircumference, &QLineEdit::editingFinished,
            this, &CircleGeometrySection::onCircumferenceEdited);

    m_editBaseAngle = addFieldRow(
        this, col, QString::fromUtf8("基准角度"),
        cad::ui::kPlaceholderAngleOrFormula, QString::fromUtf8("°"),
        cad::ui::TooltipFormatter::action(
            QStringLiteral("基准角度"),
            QString::fromUtf8("起点所在的角度（从圆心出发）。改它 = 旋转接缝与全部象限锚，圆心不动")),
        &m_lblFxA0);
    connect(m_editBaseAngle, &QLineEdit::editingFinished, this, [this] {
        emit liveUpdated();
    });

    // ── 包角行: 输入框 + 90/180/270/360 预设 ──
    m_editSweep = addFieldRow(
        this, col, QString::fromUtf8("包角"),
        cad::ui::kPlaceholderAngleOrFormula, QString::fromUtf8("°"),
        cad::ui::TooltipFormatter::action(
            QStringLiteral("包角"),
            QString::fromUtf8("从起点扫到终点的角度。360 = 整圆，180 = 半圆。数值直接写终点角度，绝不归一化")),
        nullptr);
    connect(m_editSweep, &QLineEdit::editingFinished, this, [this] {
        emit liveUpdated();
    });
    // 预设按钮挂在包角行尾: 追加到该行 (addFieldRow 已把行加进布局, 这里找回它)。
    if (auto* row = qobject_cast<QHBoxLayout*>(col->itemAt(col->count() - 1)->layout())) {
        for (int deg : {90, 180, 270, 360}) {
            auto* btn = new QPushButton(QString::number(deg), this);
            btn->setFixedSize(40, kFieldH);
            btn->setStyleSheet(chips);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setToolTip(cad::ui::TooltipFormatter::action(
                QString::fromUtf8("包角 %1°").arg(deg),
                QString::fromUtf8("把包角直接设为 %1°").arg(deg)));
            connect(btn, &QPushButton::clicked, this, [this, deg] {
                m_editSweep->setText(QString::number(deg));
                emit liveUpdated();
            });
            row->insertWidget(row->count() - 1, btn);
        }
    }

    m_editRoundness = addFieldRow(
        this, col, QString::fromUtf8("圆度"),
        QStringLiteral("0"), QString(),
        cad::ui::TooltipFormatter::action(
            QStringLiteral("圆度"),
            QString::fromUtf8("0 = 正圆，−1 = 内接四边形（夹紧 −1 ~ +0.25）")),
        nullptr);
    connect(m_editRoundness, &QLineEdit::editingFinished, this, [this] {
        emit liveUpdated();
    });

    // ── 只读派生量: 弧长 / 弦长 ──
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblArc = new ElaText(QString::fromUtf8("弧长"), 11, this);
        lblArc->setFixedWidth(kLabelW);
        row->addWidget(lblArc);
        m_lblArcLength = new ElaText(QStringLiteral("—"), 11, this);
        m_lblArcLength->setStyleSheet(dimMono);
        row->addWidget(m_lblArcLength);

        auto* lblChord = new ElaText(QString::fromUtf8("弦长"), 11, this);
        lblChord->setFixedWidth(40);
        row->addWidget(lblChord);
        m_lblChord = new ElaText(QStringLiteral("—"), 11, this);
        m_lblChord->setStyleSheet(dimMono);
        row->addWidget(m_lblChord);
        row->addStretch();
        col->addLayout(row);
    }

    // ── D15: 弧长（= 周长）发布为关联参数（周长变量，cm 域）──
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        m_btnPublish = new QPushButton(cad::cmd::texts::kPublishLinkedVar, this);
        m_btnPublish->setFixedHeight(kFieldH);
        m_btnPublish->setStyleSheet(chips);
        m_btnPublish->setCursor(Qt::PointingHandCursor);
        m_btnPublish->setToolTip(cad::ui::TooltipFormatter::action(
            cad::cmd::texts::kPublishLinkedVar,
            QString::fromUtf8("把圆周长（= 弧长，整圆 = 2πR）发布为关联参数（只读），"
                              "其他公式可直接引用其引用名（L+编号）")));
        connect(m_btnPublish, &QPushButton::clicked,
                this, &CircleGeometrySection::onPublishLength);
        row->addWidget(m_btnPublish);
        row->addStretch();
        col->addLayout(row);
    }

    // ── D14: 解除圆约束 (唯一的「变自由曲线」入口) ──
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        m_btnDetach = new QPushButton(cad::cmd::texts::kDetachCircle, this);
        m_btnDetach->setFixedHeight(kFieldH);
        m_btnDetach->setStyleSheet(chips);
        m_btnDetach->setCursor(Qt::PointingHandCursor);
        m_btnDetach->setToolTip(cad::ui::TooltipFormatter::action(
            cad::cmd::texts::kDetachCircle,
            QString::fromUtf8("把圆变成普通曲线：形状原地冻结（不跳变），"
                              "之后可用曲线编辑自由拖控制点。可撤销")));
        connect(m_btnDetach, &QPushButton::clicked, this, &CircleGeometrySection::onDetach);
        row->addWidget(m_btnDetach);
        row->addStretch();
        col->addLayout(row);
    }
}

void CircleGeometrySection::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
}

void CircleGeometrySection::populateFromModel(const cad::param::Block& block,
                                              const cad::param::Segment& seg)
{
    m_blockId = block.id;
    m_segmentId = seg.id;
    fill(true);
}

void CircleGeometrySection::refreshDerived()
{
    fill(false);
}

void CircleGeometrySection::fill(bool force)
{
    if (!m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const auto* seg = block->findSegment(m_segmentId);
    if (!seg || seg->fitKind != cad::param::FitKind::Circle) return;
    const auto* sp = block->findPoint(seg->startPointId);
    if (!sp) return;

    auto setText = [force](ElaLineEdit* edit, const QString& text) {
        if (!edit) return;
        if (!force && edit->hasFocus()) return;   // 不覆盖正在输入的内容
        if (edit->text() == text) return;
        const QSignalBlocker blk(edit);
        edit->setText(text);
    };

    const double rMm   = block->circleRadiusMm(*seg);
    const double a0Deg = block->circleStartAngleDeg(*seg);
    const double sweep = block->circleSweepDeg(*seg);

    if (!sp->distanceFormula.isEmpty()) {
        setText(m_editRadius, sp->distanceFormula);
        if (m_lblFxR) m_lblFxR->setVisible(true);
    } else {
        setText(m_editRadius, cad::geo::Units::formatCm(sp->distance));
        if (m_lblFxR) m_lblFxR->setVisible(false);
    }
    setText(m_editDiameter, cad::geo::Units::formatCm(2.0 * rMm));
    setText(m_editCircumference, cad::geo::Units::formatCm(2.0 * cad::geo::kPi * rMm));

    if (!sp->angleFormula.isEmpty()) {
        setText(m_editBaseAngle, sp->angleFormula);
        if (m_lblFxA0) m_lblFxA0->setVisible(true);
    } else {
        setText(m_editBaseAngle, cad::geo::Units::formatDegValue(a0Deg));
        if (m_lblFxA0) m_lblFxA0->setVisible(false);
    }
    // 包角始终按几何值回读 (终点角度公式只是「基准角 + 包角」的耦合实现)。
    setText(m_editSweep, cad::geo::Units::formatDegValue(sweep));
    setText(m_editRoundness, cad::geo::Units::formatNumberTrimmed(seg->tension));

    if (m_lblArcLength)
        m_lblArcLength->setText(cad::geo::Units::formatLength(block->segmentBaseLength(seg->id)));
    if (m_lblChord)
        m_lblChord->setText(cad::geo::Units::formatLength(cad::geo::degToChordMm(sweep, rMm)));

    const bool alreadyPublished =
        (m_paramDoc->findLinkedBySource(block->id, seg->id) != nullptr);
    if (m_btnPublish) {
        m_btnPublish->setEnabled(!alreadyPublished);
        m_btnPublish->setText(alreadyPublished ? QString::fromUtf8("已发布")
                                               : cad::cmd::texts::kPublishLinkedVar);
    }
}

void CircleGeometrySection::onDiameterEdited()
{
    const auto d = cad::geo::parseNumberOrFormula(m_editDiameter->text());
    QString rText;
    if (d.isNumber) {
        rText = cad::geo::Units::formatCm(cad::geo::Units::cmToMm(d.value) * 0.5);
    } else if (!d.formula.isEmpty()) {
        rText = QStringLiteral("(%1)/2").arg(d.formula);
    } else {
        return;
    }
    {
        const QSignalBlocker blk(m_editRadius);
        m_editRadius->setText(rText);
    }
    emit liveUpdated();
}

void CircleGeometrySection::onCircumferenceEdited()
{
    const auto c = cad::geo::parseNumberOrFormula(m_editCircumference->text());
    QString rText;
    if (c.isNumber) {
        rText = cad::geo::Units::formatCm(
            cad::geo::Units::cmToMm(c.value) / (2.0 * cad::geo::kPi));
    } else if (!c.formula.isEmpty()) {
        rText = QStringLiteral("(%1)/(%2)")
                    .arg(c.formula, QString::number(2.0 * cad::geo::kPi, 'g', 17));
    } else {
        return;
    }
    {
        const QSignalBlocker blk(m_editRadius);
        m_editRadius->setText(rText);
    }
    emit liveUpdated();
}

void CircleGeometrySection::onPublishLength()
{
    if (!m_paramDoc) return;
    if (m_paramDoc->findLinkedBySource(m_blockId, m_segmentId)) return;

    m_paramDoc->resolveAll();

    const auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const auto* seg = block->findSegment(m_segmentId);
    if (!seg || seg->fitKind != cad::param::FitKind::Circle) return;

    // 零新机制 (D15): 弧长（圆度 0 时 = 精确周长 2πR，见 Block::segmentBaseLength）
    // 走既有 LinkedVariable 通路，进参数表 cm 域，任何 lengthFormula 都能引用。
    cad::param::LinkedVariable lv =
        cad::param::LinkedVariable::fromSegment(*block, *seg);
    if (auto* stack = m_paramDoc->undoStack())
        stack->push(new cad::cmd::AddLinkedCommand(m_paramDoc, lv));
    else
        m_paramDoc->addLinked(lv);

    m_paramDoc->resolveAll();

    if (m_btnPublish) {
        m_btnPublish->setEnabled(false);
        m_btnPublish->setText(QString::fromUtf8("已发布"));
    }
}

void CircleGeometrySection::onDetach()
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg || seg->fitKind != cad::param::FitKind::Circle) return;

    // 一条命令 = 一个撤销步；没有撤销栈时走同一套冻结逻辑（不建命令）。
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::DetachCircleCommand(m_paramDoc, m_blockId, m_segmentId));
    } else {
        cad::cmd::detachCircleInPlace(m_paramDoc, m_blockId, m_segmentId);
    }

    emit sceneRefreshRequested();
    emit detachRequested();   // 对话框重填 → 圆区隐藏、线段区接管
}

void CircleGeometrySection::applyToModel(cad::param::Block* block,
                                         cad::param::Segment* seg)
{
    if (!block || !seg || seg->fitKind != cad::param::FitKind::Circle) return;
    auto* sp = block->findPoint(seg->startPointId);
    auto* ep = block->findPoint(seg->endPointId);
    if (!sp || !ep) return;

    // ── 半径 (唯一权威): 直径/周长编辑已把换算结果写进半径输入框 ──
    const auto r = cad::geo::parseNumberOrFormula(m_editRadius->text());
    bool radiusNumeric = false;
    if (r.isNumber) {
        sp->distanceFormula.clear();
        sp->distance = std::max(cad::geo::Units::cmToMm(r.value), 0.0);
        radiusNumeric = true;
    } else if (!r.formula.isEmpty()) {
        sp->distanceFormula = r.formula;
    }
    if (radiusNumeric) {
        const QSignalBlocker b1(m_editDiameter);
        const QSignalBlocker b2(m_editCircumference);
        m_editDiameter->setText(cad::geo::Units::formatCm(2.0 * sp->distance));
        m_editCircumference->setText(
            cad::geo::Units::formatCm(2.0 * cad::geo::kPi * sp->distance));
    }

    // ── 基准角度 a0 (D18) 与包角 (D19) 必须**联合**应用 ──
    // 终点角度 = a0 + 包角, 两行互相耦合。若先写 a0 再写包角, 后一步会用
    // **解算前**的旧几何 a0 (circleStartAngleDeg 读的是 resolvedPos, 此刻
    // 还没重新解算) 覆盖掉刚写好的终点角度 —— 整圆会从 450° 掉回 360°。
    const double sweepOld = block->circleSweepDeg(*seg);
    const double a0Old = block->circleStartAngleDeg(*seg);
    const auto a0 = cad::geo::parseAngleText(m_editBaseAngle->text());
    const auto sw = cad::geo::parseAngleText(m_editSweep->text());

    if (a0.isNumber) {
        sp->angleFormula.clear();
        sp->angle = a0.value;
    } else if (!a0.formula.isEmpty()) {
        sp->angleFormula = a0.formula;
    }

    double sweepNum = sweepOld;
    bool sweepIsNumber = false;
    if (sw.isNumber) {
        sweepNum = sw.value;
        if (sweepNum <= cad::geo::kGeomEps) sweepNum = 360.0;   // 0/负值 = 整圆
        if (sweepNum > 360.0) sweepNum = 360.0;
        sweepIsNumber = true;
    }

    // **禁归一化**: 整圆存成 a0+360; 把 360° 折成 0° 会让整圆退化成零包角
    // (CIRCLE_TOOL_DESIGN.md §4.1.1 / §11 用例 6i)。
    const double a0Num = a0.isNumber ? a0.value : a0Old;
    if (sweepIsNumber && sp->angleFormula.isEmpty()) {
        ep->angleFormula.clear();
        ep->angle = a0Num + sweepNum;
    } else {
        const QString a0Expr = sp->angleFormula.isEmpty()
            ? cad::geo::Units::formatNumberTrimmed(a0Num)
            : sp->angleFormula;
        const QString swExpr = sweepIsNumber
            ? cad::geo::Units::formatNumberTrimmed(sweepNum)
            : sw.formula;
        if (!swExpr.isEmpty())
            ep->angleFormula = sweepIsNumber
                ? QStringLiteral("(%1) + %2").arg(a0Expr, swExpr)
                : QStringLiteral("(%1) + (%2)").arg(a0Expr, swExpr);
    }

    // ── 圆度 = Segment::tension (夹紧 [−1, +0.25]) ──
    const auto t = cad::geo::parseNumberOrFormula(m_editRoundness->text());
    if (t.isNumber)
        seg->tension = std::clamp(t.value, -1.0, 0.25);
}

} // namespace cad::ui
