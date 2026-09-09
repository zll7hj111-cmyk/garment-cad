#include "ui/LineGeometrySection.h"
#include "ui/LineOrthoOffsetCard.h"

#include <algorithm>
#include <cmath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QClipboard>
#include <QApplication>
#include <QTimer>
#include <QSignalBlocker>

#include "ElaText.h"
#include "ElaLineEdit.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/LinkedVariable.h"
#include "parametric/AttachmentGraph.h"
#include "document/commands/CurveCommands.h"
#include "canvas/CanvasScene.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"
#include "ui/Theme.h"
#include "ui/FormScaffold.h"
#include "ui/TooltipFormatter.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/BlockCommands.h"
#include "geometry/Angle.h"
#include "document/CommandTexts.h"
#include "ui/UiStrings.h"

namespace cad::ui {

namespace {

constexpr int kLabelW = 64;
constexpr int kFieldH = 30;


} // namespace

LineGeometrySection::LineGeometrySection(cad::param::ParamDocument* paramDoc,
                                         CanvasScene* scene,
                                         QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(paramDoc)
    , m_scene(scene)
{
    const QString chips = cad::ui::chipButtonStyle();
    const QString dimMono = cad::ui::Theme::dimValueStyle();

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);
    connect(m_debounce, &QTimer::timeout, this, &LineGeometrySection::onDebounceTimeout);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    // ── 长度行 ──
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblLen = new ElaText(QString::fromUtf8("长度"), 11, this);
        lblLen->setFixedWidth(kLabelW);
        row->addWidget(lblLen);

        m_lblFx = new ElaText(
            QStringLiteral("<i style='color:%1;'>fx</i>")
                .arg(cad::ui::Theme::tokens().text2.name()),
            11, this);
        m_lblFx->setVisible(false);
        m_lblFx->setFixedWidth(18);
        row->addWidget(m_lblFx);

        m_editLength = makeCompactEdit(this, 150);
        m_editLength->setObjectName(QStringLiteral("editLength"));
        m_editLength->setPlaceholderText(cad::ui::kPlaceholderCmOrFormula);
        connect(m_editLength, &QLineEdit::textChanged, this, &LineGeometrySection::onLengthDirty);
        connect(m_editLength, &QLineEdit::editingFinished, this, &LineGeometrySection::onLengthApply);
        row->addWidget(m_editLength);

        m_btnLenAuto = new QPushButton(QString::fromUtf8("自动"), this);
        m_btnLenAuto->setObjectName(QStringLiteral("lengthAutoChip"));
        m_btnLenAuto->setCheckable(true);
        m_btnLenAuto->setFixedSize(48, kFieldH);
        m_btnLenAuto->setStyleSheet(chips);
        m_btnLenAuto->setCursor(Qt::PointingHandCursor);
        m_btnLenAuto->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("自动长度"),
            QStringLiteral("两端都钉在宿主点上，长度由两点距离算出（桥接行为）")));

        m_btnLenSpec = new QPushButton(QString::fromUtf8("指定"), this);
        m_btnLenSpec->setObjectName(QStringLiteral("lengthSpecChip"));
        m_btnLenSpec->setCheckable(true);
        m_btnLenSpec->setFixedSize(48, kFieldH);
        m_btnLenSpec->setStyleSheet(chips);
        m_btnLenSpec->setCursor(Qt::PointingHandCursor);
        m_btnLenSpec->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("指定长度"),
            QStringLiteral("起点钉在宿主点上，按角度延伸，长度由本输入框指定")));

        m_lenGroup = new QButtonGroup(this);
        m_lenGroup->addButton(m_btnLenAuto);
        m_lenGroup->addButton(m_btnLenSpec);
        connect(m_btnLenAuto, &QPushButton::clicked, this, [this] { onLengthModeChanged(true); });
        connect(m_btnLenSpec, &QPushButton::clicked, this, [this] { onLengthModeChanged(false); });
        row->addWidget(m_btnLenAuto);
        row->addWidget(m_btnLenSpec);

        auto* btnPasteLen = new QPushButton(cad::ui::str::kFillIn, this);
        btnPasteLen->setFixedSize(48, kFieldH);
        btnPasteLen->setStyleSheet(chips);
        btnPasteLen->setToolTip(cad::ui::TooltipFormatter::action(
            cad::ui::str::kPasteClipboard,
            cad::ui::str::kPasteClearsInputTip));
        connect(btnPasteLen, &QPushButton::clicked, this, [this] {
            const QString clean = QString(QApplication::clipboard()->text())
                                      .remove(QLatin1Char('\r'))
                                      .remove(QLatin1Char('\n'))
                                      .trimmed();
            if (!clean.isEmpty())
                m_editLength->setText(clean);
        });
        row->addWidget(btnPasteLen);

        m_btnPublishLen = new QPushButton(QString::fromUtf8("发布参数"), this);
        m_btnPublishLen->setFixedHeight(kFieldH);
        m_btnPublishLen->setStyleSheet(chips);
        m_btnPublishLen->setToolTip(cad::ui::TooltipFormatter::action(
            cad::cmd::texts::kPublishLinkedVar,
            QStringLiteral("将本线段的长度发布为关联参数（只读），其他公式可直接引用其引用名（L+编号）")));
        m_btnPublishLen->setCursor(Qt::PointingHandCursor);
        connect(m_btnPublishLen, &QPushButton::clicked, this, &LineGeometrySection::onPublishLength);
        row->addWidget(m_btnPublishLen);

        m_lblActualLength = new ElaText(QString(), 11, this);
        m_lblActualLength->setStyleSheet(dimMono);
        m_lblActualLength->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("实际长度"),
            QStringLiteral("当前实际长度（只读）"),
            false));
        row->addWidget(m_lblActualLength);

        row->addStretch();
        col->addLayout(row);
    }

    m_orthoCard = new LineOrthoOffsetCard(this);
    col->addWidget(m_orthoCard);
    connect(m_orthoCard, &LineOrthoOffsetCard::orthoChanged, this, [this]() {
        onLengthDirty();
        onLengthApply();
        emit liveUpdated();
    });
    connect(m_orthoCard, &LineOrthoOffsetCard::liveUpdated, this, &LineGeometrySection::liveUpdated);
    connect(m_orthoCard, &LineOrthoOffsetCard::sceneRefreshRequested, this, &LineGeometrySection::sceneRefreshRequested);

    // ── 滑轨行 ──
    m_slideRow = new QWidget(this);
    {
        auto* slideLayout = new QHBoxLayout(m_slideRow);
        slideLayout->setContentsMargins(0, 0, 0, 0);
        slideLayout->setSpacing(6);
        auto* lblSlide = new ElaText(QString::fromUtf8("滑轨"), 11, m_slideRow);
        lblSlide->setFixedWidth(kLabelW);
        lblSlide->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("滑轨连接"),
            QStringLiteral("抽屉式单向滑动：连接姿态保持（角度始终随基准线），但位置只留一个自由度")));
        slideLayout->addWidget(lblSlide);

        auto* lblAlong = new ElaText(QString::fromUtf8("沿向"), 11, m_slideRow);
        lblAlong->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        slideLayout->addWidget(lblAlong);
        m_editSlideAlong = new ElaLineEdit(m_slideRow);
        m_editSlideAlong->setFixedWidth(150);
        m_editSlideAlong->setPlaceholderText(QString::fromUtf8("0"));
        m_editSlideAlong->setFixedHeight(kFieldH);
        m_editSlideAlong->setStyleSheet(QStringLiteral("font-size: 11px;"));
        slideLayout->addWidget(m_editSlideAlong);

        auto* lblPerpSp = new ElaText(QString(), 11, m_slideRow);
        lblPerpSp->setFixedWidth(18);
        slideLayout->addWidget(lblPerpSp);
        auto* lblPerp = new ElaText(QString::fromUtf8("垂直"), 11, m_slideRow);
        lblPerp->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        slideLayout->addWidget(lblPerp);
        m_editSlidePerp = new ElaLineEdit(m_slideRow);
        m_editSlidePerp->setFixedWidth(150);
        m_editSlidePerp->setPlaceholderText(QString::fromUtf8("0"));
        m_editSlidePerp->setFixedHeight(kFieldH);
        m_editSlidePerp->setStyleSheet(QStringLiteral("font-size: 11px;"));
        slideLayout->addWidget(m_editSlidePerp);

        m_cmbSlideMode = new QComboBox(m_slideRow);
        m_cmbSlideMode->setFixedHeight(kFieldH);
        m_cmbSlideMode->setStyleSheet(QStringLiteral("font-size: 11px;"));
        m_cmbSlideMode->addItem(QString::fromUtf8("全连接"));
        m_cmbSlideMode->addItem(QString::fromUtf8("沿线滑动"));
        m_cmbSlideMode->addItem(QString::fromUtf8("垂直拉出"));
        m_cmbSlideMode->setVisible(false);

        m_lblSlideBadge = new ElaText(QString(), 11, m_slideRow);
        m_lblSlideBadge->setStyleSheet(cad::ui::Theme::tealBadgeStyle());
        m_lblSlideBadge->setVisible(false);
        slideLayout->addWidget(m_lblSlideBadge);
        slideLayout->addStretch();

        connect(m_cmbSlideMode, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &LineGeometrySection::onSlideModeChanged);
        connect(m_editSlideAlong, &ElaLineEdit::editingFinished,
                this, &LineGeometrySection::onSlideOffsetEdited);
        connect(m_editSlidePerp, &ElaLineEdit::editingFinished,
                this, &LineGeometrySection::onSlideOffsetEdited);
    }
    col->addWidget(m_slideRow);

    // ── 曲线按需行 ──
    m_arcRow = new QWidget(this);
    {
        auto* arcLayout = new QHBoxLayout(m_arcRow);
        arcLayout->setContentsMargins(0, 0, 0, 0);
        arcLayout->setSpacing(6);
        auto* lblArc = new ElaText(QString::fromUtf8("弧长"), 11, m_arcRow);
        lblArc->setFixedWidth(kLabelW);
        arcLayout->addWidget(lblArc);
        m_lblArcLength = new ElaText(QStringLiteral("—"), 11, m_arcRow);
        m_lblArcLength->setStyleSheet(
            QStringLiteral("font-weight:600; background:transparent;")
            + QString::fromLatin1(cad::ui::ThemeTokens::kMonospaceFamily));
        arcLayout->addWidget(m_lblArcLength);
        arcLayout->addStretch();
    }
    m_arcRow->setVisible(false);
    col->addWidget(m_arcRow);

    m_tensionRow = new QWidget(this);
    {
        auto* tensionLayout = new QHBoxLayout(m_tensionRow);
        tensionLayout->setContentsMargins(0, 0, 0, 0);
        tensionLayout->setSpacing(6);
        auto* lblTension = new ElaText(QString::fromUtf8("张力"), 11, m_tensionRow);
        lblTension->setFixedWidth(kLabelW);
        tensionLayout->addWidget(lblTension);
        m_editTension = makeCompactEdit(m_tensionRow, 150);
        m_editTension->setPlaceholderText(QStringLiteral("0"));
        m_editTension->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("曲线张力"),
            QStringLiteral("0 = 平滑(Catmull-Rom)；>0 更紧绷；<0 更松弛")));
        connect(m_editTension, &QLineEdit::editingFinished, this, &LineGeometrySection::liveUpdated);
        tensionLayout->addWidget(m_editTension);

        m_btnConvert = new QPushButton(QString::fromUtf8("转为直线"), m_tensionRow);
        m_btnConvert->setFixedHeight(kFieldH);
        m_btnConvert->setStyleSheet(chips);
        m_btnConvert->setCursor(Qt::PointingHandCursor);
        m_btnConvert->setToolTip(cad::ui::TooltipFormatter::action(
            cad::cmd::texts::kToLine,
            QStringLiteral("移除本线段的所有曲线控制点，变为普通两点直线")));
        connect(m_btnConvert, &QPushButton::clicked, this, &LineGeometrySection::onConvertToLine);
        tensionLayout->addWidget(m_btnConvert);
        tensionLayout->addStretch();
    }
    m_tensionRow->setVisible(false);
    col->addWidget(m_tensionRow);
}

void LineGeometrySection::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    if (m_orthoCard) {
        m_orthoCard->setContext(m_paramDoc, m_blockId, m_segmentId);
    }
}

void LineGeometrySection::populateFromModel(const cad::param::Block& block,
                                            const cad::param::Segment& seg)
{
    const QSignalBlocker bLen(m_editLength);
    const QSignalBlocker bTension(m_editTension);

    if (!seg.lengthFormula.isEmpty()) {
        m_editLength->setText(seg.lengthFormula);
        m_lblFx->setVisible(true);
    } else {
        m_lblFx->setVisible(false);
        const cad::param::ParamPoint* sp = block.findPoint(seg.startPointId);
        const cad::param::ParamPoint* ep = block.findPoint(seg.endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            // 正交拐角偏置：长度输入框对应中心主轴基准长（局部X），斜长由专属标签展示，绝不污染基准长
            double lenMm = (ep->constraint == cad::param::PointConstraint::OrthoOffset)
                ? ep->distance
                : sp->resolvedPos.distanceTo(ep->resolvedPos);
            m_editLength->setText(cad::geo::Units::formatCm(lenMm));
        }
    }

    refreshActualLengthLabel();

    // ── 拐角偏置回填 ──
    const bool isCurve = seg.isCurve();
    if (m_orthoCard) {
        m_orthoCard->populate(block, seg, isCurve);
    }

    m_arcRow->setVisible(isCurve);
    m_tensionRow->setVisible(isCurve);
    if (isCurve) {
        double arcMm = block.segmentBaseLength(seg.id);
        m_lblArcLength->setText(cad::geo::Units::formatLength(arcMm));
        m_editTension->setText(cad::geo::Units::formatNumberTrimmed(seg.tension));
    }

    const bool alreadyPublished = (m_paramDoc && m_paramDoc->findLinkedBySource(block.id, seg.id) != nullptr);
    if (m_btnPublishLen) {
        m_btnPublishLen->setEnabled(!alreadyPublished);
        m_btnPublishLen->setText(alreadyPublished ? QString::fromUtf8("已发布")
                                                  : QString::fromUtf8("发布参数"));
    }

    refreshLengthMode();
    refreshSlideRow();
    applyBridgeReadOnly();
}

void LineGeometrySection::applyToModel(cad::param::Block* block,
                                       cad::param::Segment* seg)
{
    if (!block || !seg) return;

    // 圆拟合段 (FitKind::Circle): 半径/基准角/包角/圆度全部归 CircleGeometrySection,
    // 本区的长度框/张力框即使隐藏也还留着旧文本, 不早退会把它们写回模型
    // (长度框会写终点距离, 张力框会写圆度)。
    if (seg->fitKind == cad::param::FitKind::Circle) return;

    const auto parsedLen = cad::geo::parseNumberOrFormula(m_editLength->text());
    if (parsedLen.isNumber) {
        double numMm = cad::geo::Units::cmToMm(parsedLen.value);
        seg->lengthFormula.clear();
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula.clear();
            ep->distance = numMm;
        }
    } else if (!parsedLen.formula.isEmpty()) {
        seg->lengthFormula = parsedLen.formula;
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula = parsedLen.formula;
        }
    }

    if (m_orthoCard) {
        m_orthoCard->apply(block, seg);
    }

    if (m_editTension && m_tensionRow->isVisible()) {
        bool ok = false;
        double t = m_editTension->text().toDouble(&ok);
        if (ok) seg->tension = t;
    }
}

void LineGeometrySection::refreshActualLengthLabel()
{
    if (!m_lblActualLength || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;
    const double lenMm = seg->isCurve()
        ? block->segmentBaseLength(seg->id)
        : block->segmentEffectiveLength(seg->id);
    m_lblActualLength->setText(lenMm > 0.0
        ? cad::geo::Units::formatLength(lenMm)
        : QStringLiteral("—"));
}

void LineGeometrySection::refreshLengthMode()
{
    if (!m_btnLenAuto || !m_btnLenSpec || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const auto* att = m_paramDoc->findFollowerAttachmentOf(m_blockId);
    const bool hasAtt = att != nullptr;
    const bool hasEnd = !block->endTargetPointId.isNull();
    const bool bridge = hasAtt && hasEnd;
    const bool autoMode = bridge || block->lengthAuto;
    const QSignalBlocker b1(m_btnLenAuto);
    const QSignalBlocker b2(m_btnLenSpec);
    m_btnLenAuto->setChecked(autoMode);
    m_btnLenSpec->setChecked(!autoMode);
    m_btnLenAuto->setEnabled(!bridge);
    m_btnLenSpec->setEnabled(!bridge);
    if (m_editLength)
        m_editLength->setEnabled(!autoMode && !bridge);
}

void LineGeometrySection::applyBridgeReadOnly()
{
    if (!m_paramDoc) return;
    const cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block || !block->isBridge) return;
    const cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    if (m_editLength) {
        m_editLength->setEnabled(false);
        m_editLength->setToolTip(cad::ui::TooltipFormatter::plain(
            QString::fromUtf8("桥接线长度由两端吸附点位置决定，无法直接修改")));
        // 输入框一律无单位后缀 (审计 UI-P0-4): 桥接线分支此前写 formatLength
        // ("12.5 cm"), 与常规分支 ("12.5") 两种文本进同一解析路径。
        const double lenMm = block->segmentEffectiveLength(seg->id);
        m_editLength->setText(cad::geo::Units::formatCm(lenMm));
    }
    if (m_lblFx) m_lblFx->setVisible(false);
    if (m_btnPublishLen) m_btnPublishLen->setEnabled(false);
}

void LineGeometrySection::refreshSlideRow()
{
    if (!m_slideRow || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* att = m_paramDoc->findFollowerAttachmentOf(m_blockId);
    const bool hasAtt = att != nullptr;
    const bool hasEnd = block && !block->endTargetPointId.isNull();
    const bool bridge = hasAtt && hasEnd;

    m_slideRow->setVisible(true);

    const bool slideOk = hasAtt && !bridge && !att->angleOnly && !att->angleIndependent;
    m_editSlideAlong->setEnabled(slideOk);
    m_editSlidePerp->setEnabled(slideOk);
    m_cmbSlideMode->setEnabled(slideOk);

    {
        const QSignalBlocker b1(m_editSlideAlong);
        const QSignalBlocker b2(m_editSlidePerp);
        const QSignalBlocker b3(m_cmbSlideMode);

        const int modeIdx = (att && slideOk) ? static_cast<int>(att->slideMode) : 0;
        m_cmbSlideMode->setCurrentIndex(modeIdx);

        const bool hasSlide = att && slideOk;
        m_editSlideAlong->setText(hasSlide
            ? (!att->slideAlongFormula.isEmpty()
                   ? att->slideAlongFormula
                   : cad::geo::Units::formatCm(att->slideAlongMm))
            : QString());
        m_editSlidePerp->setText(hasSlide
            ? (!att->slidePerpFormula.isEmpty()
                   ? att->slidePerpFormula
                   : cad::geo::Units::formatCm(att->slidePerpMm))
            : QString());
    }
    m_lblSlideBadge->setVisible(false);
}

void LineGeometrySection::onLengthDirty()
{
    if (!m_editLength) return;
    const auto parsed = cad::geo::parseNumberOrFormula(m_editLength->text());
    if (m_lblFx) m_lblFx->setVisible(!parsed.formula.isEmpty());
    if (m_debounce) m_debounce->start();
}

void LineGeometrySection::onLengthApply()
{
    if (!m_editLength || !m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    applyToModel(block, seg);
    refreshActualLengthLabel();
    emit sceneRefreshRequested();
    emit lengthApplied();
}

void LineGeometrySection::onLengthModeChanged(bool autoMode)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const auto* att = m_paramDoc->findFollowerAttachmentOf(m_blockId);
    const bool bridge = att != nullptr && !block->endTargetPointId.isNull();
    if (bridge) {
        refreshLengthMode();
        return;
    }
    block->lengthAuto = autoMode;
    refreshLengthMode();
    emit sceneRefreshRequested();
}

void LineGeometrySection::onSlideModeChanged(int index)
{
    if (!m_paramDoc) return;
    const auto* att = m_paramDoc->findFollowerAttachmentOf(m_blockId);
    if (!att || att->angleOnly || att->angleIndependent) {
        refreshSlideRow();
        return;
    }
    const auto mode = static_cast<cad::param::SlideMode>(index);
    if (att->slideMode == mode) {
        refreshSlideRow();
        return;
    }
    // 滑轨模式切换入栈 (SetAttachmentSlideModeCommand: redo 走门面同语义,
    // undo 快照恢复 mode/锁轴偏移/焊接旗标) —— 此前直写模型不进历史。
    if (auto* stack = m_paramDoc->undoStack())
        stack->push(new cad::cmd::SetAttachmentSlideModeCommand(m_paramDoc, att->id, mode));
    else
        m_paramDoc->setAttachmentSlideMode(att->id, mode);
    refreshSlideRow();
    emit sceneRefreshRequested();
}

void LineGeometrySection::onSlideOffsetEdited()
{
    if (!m_paramDoc) return;
    const auto* att = m_paramDoc->findFollowerAttachmentOf(m_blockId);
    if (!att || att->angleOnly || att->angleIndependent) {
        refreshSlideRow();
        return;
    }

    // 数值/公式判读统一走 parseNumberOrFormula (UI-P1-9)。
    const auto alongParsed = cad::geo::parseNumberOrFormula(m_editSlideAlong->text());
    const auto perpParsed  = cad::geo::parseNumberOrFormula(m_editSlidePerp->text());
    const bool hasAlong = alongParsed.isNumber || !alongParsed.formula.isEmpty();
    const bool hasPerp  = perpParsed.isNumber  || !perpParsed.formula.isEmpty();
    const bool aFormula = !alongParsed.isNumber && !alongParsed.formula.isEmpty();
    const bool pFormula = !perpParsed.isNumber  && !perpParsed.formula.isEmpty();
    const double along = cad::geo::Units::cmToMm(alongParsed.value);
    const double perp  = cad::geo::Units::cmToMm(perpParsed.value);

    cad::param::SlideMode mode = cad::param::SlideMode::None;
    if (hasAlong || hasPerp) {
        if (hasAlong && !hasPerp)
            mode = cad::param::SlideMode::AlongLeader;
        else if (!hasAlong && hasPerp)
            mode = cad::param::SlideMode::PerpLeader;
        else
            mode = att->slideMode != cad::param::SlideMode::None
                ? att->slideMode : cad::param::SlideMode::AlongLeader;
    }

    const QString alongFormula = aFormula ? alongParsed.formula : QString();
    const QString perpFormula  = pFormula ? perpParsed.formula : QString();
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::SetAttachmentSlideOffsetsCommand(
            m_paramDoc, att->id, mode, along, alongFormula, perp, perpFormula));
    } else {
        auto* mut = m_paramDoc->findAttachment(att->id);
        if (mut) {
            mut->slideMode = mode;
            mut->slideAlongMm = along;
            mut->slideAlongFormula = alongFormula;
            mut->slidePerpMm = perp;
            mut->slidePerpFormula = perpFormula;
            m_paramDoc->resolveAll();
        }
    }
    refreshSlideRow();
}

void LineGeometrySection::onPublishLength()
{
    if (!m_paramDoc) return;
    if (m_paramDoc->findLinkedBySource(m_blockId, m_segmentId)) return;

    onLengthApply();
    m_paramDoc->resolveAll();

    const auto* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk) return;
    const auto* seg = blk->findSegment(m_segmentId);
    if (!seg) return;

    cad::param::LinkedVariable lv = cad::param::LinkedVariable::fromSegment(*blk, *seg);
    auto* stack = m_paramDoc->undoStack();
    if (stack)
        stack->push(new cad::cmd::AddLinkedCommand(m_paramDoc, lv));
    else
        m_paramDoc->addLinked(lv);

    m_paramDoc->resolveAll();

    m_btnPublishLen->setEnabled(false);
    m_btnPublishLen->setText(QString::fromUtf8("已发布"));
}

void LineGeometrySection::onConvertToLine()
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg || !seg->isCurve()) return;

    // Push a dedicated command: curve-to-line is a structural topology change
    // (pass-points are removed) that the session snapshot / SetLineProperties
    // commit never captures. Pushing it here makes it a single undo step.
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::ConvertCurveToLineCommand(
            m_paramDoc, m_blockId, m_segmentId));
    } else {
        auto& pts = block->points;
        for (const auto& ppId : seg->passPointIds) {
            pts.erase(std::remove_if(pts.begin(), pts.end(),
                [&ppId](const cad::param::ParamPoint& p) { return p.id == ppId; }),
                pts.end());
        }
        block->rebuildPointIndex();
        seg->passPointIds.clear();
        seg->type = cad::param::SegmentType::Line;
        block->touchGeometry();
        m_paramDoc->resolveAll();
    }

    emit sceneRefreshRequested();
    if (auto* b = m_paramDoc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId))
            populateFromModel(*b, *s);
    }
}

void LineGeometrySection::onDebounceTimeout()
{
    onLengthApply();
}

const cad::param::MeasureVariable* LineGeometrySection::findBridgeMeasure() const
{
    if (!m_paramDoc) return nullptr;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!seg) return nullptr;

    const QString formula = seg->lengthFormula.trimmed();
    if (formula.isEmpty()) return nullptr;
    for (const auto& mv : m_paramDoc->measureVars()) {
        if (mv.refName.compare(formula, Qt::CaseInsensitive) == 0) return &mv;
    }
    return nullptr;
}

void LineGeometrySection::refreshOrthoHypotLabel()
{
    if (!m_paramDoc || !m_orthoCard) return;
    if (const auto* b = m_paramDoc->findBlock(m_blockId)) {
        if (const auto* s = b->findSegment(m_segmentId)) {
            m_orthoCard->refreshHypotLabel(*b, *s);
        }
    }
}

void LineGeometrySection::onOrthoDistEdited()
{
    if (m_orthoCard) m_orthoCard->onOrthoDistEdited();
}

void LineGeometrySection::onOrthoDirChanged(int id)
{
    if (m_orthoCard) m_orthoCard->onOrthoDirChanged(id);
}

void LineGeometrySection::onToggleCenterAxis()
{
    if (m_orthoCard) m_orthoCard->onToggleCenterAxis();
}

} // namespace cad::ui

