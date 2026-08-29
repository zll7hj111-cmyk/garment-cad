#include "ui/SegmentExtendCard.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>

#include "ElaText.h"
#include "ElaLineEdit.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "canvas/CanvasScene.h"
#include "document/commands/BlockCommands.h"
#include "ui/Theme.h"

namespace cad::ui {

SegmentExtendCard::SegmentExtendCard(cad::param::ParamDocument* doc,
                                     CanvasScene* scene, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_scene(scene)
{
    // 纯行组 (2026-12 去卡框化): 无边框/无底色/无标题, 由分区提供标题。
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    m_lblHint = new ElaText(QString(), 12, this);
    m_lblHint->setStyleSheet(cad::ui::Theme::dimValueStyle());
    m_lblHint->setVisible(false);
    lay->addWidget(m_lblHint);

    // ── 起端延长 row: [起 P12][输入110][=值] ──
    m_startRow = new QWidget(this);
    auto* startLay = new QHBoxLayout(m_startRow);
    startLay->setContentsMargins(0, 0, 0, 0);
    startLay->setSpacing(6);
    m_lblStart = new ElaText(QString::fromUtf8("起点延长"), 12, m_startRow);
    m_lblStart->setFixedWidth(96);
    startLay->addWidget(m_lblStart);
    m_startEdit = new ElaLineEdit(m_startRow);
    m_startEdit->setFixedWidth(110);
    m_startEdit->setPlaceholderText(QString::fromUtf8("数值或公式 cm"));
    m_startEdit->setToolTip(QString::fromUtf8(
        "起端沿「终点→起点」方向往外延长的距离；数值与公式均为 cm 域；"
        "只允许 >= 0（仅往外延长，不改变原参数化）"));
    startLay->addWidget(m_startEdit);
    m_lblStartValue = new ElaText(QString(), 12, m_startRow);
    m_lblStartValue->setStyleSheet(cad::ui::Theme::dimValueStyle());
    startLay->addWidget(m_lblStartValue);
    startLay->addStretch();
    lay->addWidget(m_startRow);

    // ── 终端延长 row ──
    m_endRow = new QWidget(this);
    auto* endLay = new QHBoxLayout(m_endRow);
    endLay->setContentsMargins(0, 0, 0, 0);
    endLay->setSpacing(6);
    m_lblEnd = new ElaText(QString::fromUtf8("终点延长"), 12, m_endRow);
    m_lblEnd->setFixedWidth(96);
    endLay->addWidget(m_lblEnd);
    m_endEdit = new ElaLineEdit(m_endRow);
    m_endEdit->setFixedWidth(110);
    m_endEdit->setPlaceholderText(QString::fromUtf8("数值或公式 cm"));
    m_endEdit->setToolTip(QString::fromUtf8(
        "终端沿「起点→终点」方向往外延长的距离；数值与公式均为 cm 域；"
        "只允许 >= 0（仅往外延长，不改变原参数化）"));
    endLay->addWidget(m_endEdit);
    m_lblEndValue = new ElaText(QString(), 12, m_endRow);
    m_lblEndValue->setStyleSheet(cad::ui::Theme::dimValueStyle());
    endLay->addWidget(m_lblEndValue);
    endLay->addStretch();
    lay->addWidget(m_endRow);

    // ── 原长 / 延长量 / 实际长 readout ──
    m_lblReadout = new ElaText(QString(), 12, this);
    m_lblReadout->setStyleSheet(cad::ui::Theme::dimValueStyle());
    lay->addWidget(m_lblReadout);

    connect(m_startEdit, &ElaLineEdit::editingFinished,
            this, &SegmentExtendCard::onStartEdited);
    connect(m_endEdit, &ElaLineEdit::editingFinished,
            this, &SegmentExtendCard::onEndEdited);
    // 起终点延长量随模型变更刷新 (线段换向会把延长量随端点角色互换,
    // 滞留旧值会让用户拿错基线编辑)。正在输入的框不覆盖。
    if (m_doc) {
        connect(m_doc, &cad::param::ParamDocument::resolved,
                this, [this]() {
                    if (m_startEdit->hasFocus() || m_endEdit->hasFocus()) return;
                    refresh();
                });
    }
}

void SegmentExtendCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    refresh();
}

QString SegmentExtendCard::endDisableReason(const cad::param::Block& block,
                                            const cad::param::Segment& seg,
                                            bool forStart) const
{
    const QUuid pointId = forStart ? seg.startPointId : seg.endPointId;

    // 粘死端 (EXTEND_LINE_DESIGN.md D5): 本线自身作为跟随线的吸附点不允许
    // 延长 —— 否则与"两点重合"的位置约束冲突。
    for (const auto& att : m_doc->attachments()) {
        if (att.fromBlockId == block.id && !att.isPin
            && att.fromPointId == pointId) {
            return QString::fromUtf8("该端已粘在基准线上（跟随连接端），不允许延长");
        }
    }

    // 同块角点 (D4b): 该端同时是同一 Block 其他线段的端点。
    int incidence = 0;
    for (const auto& s : block.segments)
        if (s.startPointId == pointId || s.endPointId == pointId)
            ++incidence;
    if (incidence > 1)
        return QString::fromUtf8("该端为折线/闭合轮廓的共用角点，暂不支持延长");

    // 组件暴露端点 (D9): 组件级连接的借用点, 延长会牵动组件对接语义。
    const auto* comp = m_doc->componentsView().ofBlock(block.id);
    if (comp && comp->exposedPointId == pointId)
        return QString::fromUtf8("该端为组件的暴露端点，暂不支持延长");

    return QString();
}

void SegmentExtendCard::refresh()
{
    const auto* block = m_doc ? m_doc->findBlock(m_blockId) : nullptr;
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    // 整卡置灰判定 (D3 / D5 推论)。
    QString cardReason;
    if (seg->isCurve())
        cardReason = QString::fromUtf8("曲线暂不支持延长");
    else if (block->isBridge)
        cardReason = QString::fromUtf8("桥接线两端均已钉住，不支持延长");
    else if (block->isDart())
        cardReason = QString::fromUtf8("省道线为计算线，不支持延长");

    const bool wholeGray = !cardReason.isEmpty();
    const QString startReason = wholeGray ? cardReason
                                          : endDisableReason(*block, *seg, true);
    const QString endReason = wholeGray ? cardReason
                                        : endDisableReason(*block, *seg, false);

    m_lblHint->setVisible(wholeGray);
    m_lblHint->setText(cardReason);

    // 行标签 = 起点延长/终点延长 + 点 ID (用户 2026-12: 补全"延长"文字,
    // 去掉简写"起/终").
    const auto* spTag = block->findPoint(seg->startPointId);
    const auto* epTag = block->findPoint(seg->endPointId);
    const QString sLab = QString::fromUtf8("起点延长(%1)")
        .arg(spTag ? cad::param::Serial::tag(spTag->serial) : QStringLiteral("?"));
    const QString eLab = QString::fromUtf8("终点延长(%1)")
        .arg(epTag ? cad::param::Serial::tag(epTag->serial) : QStringLiteral("?"));
    if (m_lblStart->text() != sLab) m_lblStart->setText(sLab);
    if (m_lblEnd->text() != eLab) m_lblEnd->setText(eLab);

    {
        const QSignalBlocker b1(m_startEdit);
        const QSignalBlocker b2(m_endEdit);
        const QString startText = !seg->extendStartFormula.isEmpty()
            ? seg->extendStartFormula
            : (seg->extendStartMm > 0.0
                   ? cad::geo::Units::formatCmTrimmed(seg->extendStartMm)
                   : QString());
        const QString endText = !seg->extendEndFormula.isEmpty()
            ? seg->extendEndFormula
            : (seg->extendEndMm > 0.0
                   ? cad::geo::Units::formatCmTrimmed(seg->extendEndMm)
                   : QString());
        m_startEdit->setText(startText);
        m_endEdit->setText(endText);
    }
    m_startEdit->setEnabled(startReason.isEmpty());
    m_endEdit->setEnabled(endReason.isEmpty());
    m_startEdit->setToolTip(startReason.isEmpty()
        ? QString::fromUtf8("数值或公式 cm（0 = 不延长）") : startReason);
    m_endEdit->setToolTip(endReason.isEmpty()
        ? QString::fromUtf8("数值或公式 cm（0 = 不延长）") : endReason);
    m_startRow->setEnabled(startReason.isEmpty());
    m_endRow->setEnabled(endReason.isEmpty());

    updateReadout(*block, *seg);
}

void SegmentExtendCard::updateReadout(const cad::param::Block& block,
                                      const cad::param::Segment& seg)
{
    const double base = block.segmentBaseLength(seg.id);
    const double extS = block.segmentExtendStart(seg.id);
    const double extE = block.segmentExtendEnd(seg.id);
    const double eff = block.segmentEffectiveLength(seg.id);
    const QString sVal = cad::geo::Units::formatCmTrimmed(extS);
    const QString eVal = cad::geo::Units::formatCmTrimmed(extE);
    m_lblStartValue->setText(QString::fromUtf8("= %1 cm").arg(sVal));
    m_lblEndValue->setText(QString::fromUtf8("= %1 cm").arg(eVal));
    // 读数 = 等式: 原长 + 延长起 + 延长终 = 实际 (用户 2026-12: 补充 +/= 文字).
    m_lblReadout->setText(QString::fromUtf8("原长 %1 + 延长 %2 + %3 = 实际 %4")
        .arg(cad::geo::Units::formatCmTrimmed(base))
        .arg(sVal)
        .arg(eVal)
        .arg(cad::geo::Units::formatCmTrimmed(eff)));
}

void SegmentExtendCard::applyEdited(ElaLineEdit* edit, bool isStart)
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const auto parsed = cad::geo::parseNumberOrFormula(edit->text());
    const bool isNumber = parsed.isNumber;
    const double num = parsed.value;
    if (isNumber && num < 0.0) {
        // 只允许往外延长 (D2)。
        if (m_scene)
            m_scene->showToast(QString::fromUtf8(
                "延长量不能为负数（只允许往外延长）"));
        // 还原为当前模型值（cm 显示）。
        const QString restore = isStart
            ? (!seg->extendStartFormula.isEmpty()
                   ? seg->extendStartFormula
                   : (seg->extendStartMm > 0.0
                          ? cad::geo::Units::formatCmTrimmed(seg->extendStartMm)
                          : QString()))
            : (!seg->extendEndFormula.isEmpty()
                   ? seg->extendEndFormula
                   : (seg->extendEndMm > 0.0
                          ? cad::geo::Units::formatCmTrimmed(seg->extendEndMm)
                          : QString()));
        const QSignalBlocker b(edit);
        edit->setText(restore);
        return;
    }

    // 空 = 0; 纯数字 = cm（转 mm 存储）; 其他 = 公式 (cm 域)。
    cad::cmd::SetSegmentExtendCommand::Values v;
    v.startMm = seg->extendStartMm;
    v.startFormula = seg->extendStartFormula;
    v.endMm = seg->extendEndMm;
    v.endFormula = seg->extendEndFormula;
    if (isStart) {
        if (parsed.formula.isEmpty()) { v.startMm = 0.0; v.startFormula.clear(); }
        else if (isNumber) { v.startMm = cad::geo::Units::cmToMm(num); v.startFormula.clear(); }
        else v.startFormula = parsed.formula;   // 数值保留, 公式优先 (模型语义)
    } else {
        if (parsed.formula.isEmpty()) { v.endMm = 0.0; v.endFormula.clear(); }
        else if (isNumber) { v.endMm = cad::geo::Units::cmToMm(num); v.endFormula.clear(); }
        else v.endFormula = parsed.formula;
    }

    // P0-3: 文档栈由 ParamDocument 构造时创建, 恒非空 —— 统一走命令,
    // 删除旧的 "undoStack null 时静默降级直改" else 双写路径。
    m_doc->undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
        m_doc, m_blockId, m_segmentId, v));

    // 提交后刷新本卡 (命令内部已 resolveAll)。
    refresh();
    emit changed();
}

void SegmentExtendCard::onStartEdited()
{
    applyEdited(m_startEdit, true);
}

void SegmentExtendCard::onEndEdited()
{
    applyEdited(m_endEdit, false);
}

} // namespace cad::ui
