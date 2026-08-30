#include "ui/SegmentConnectionCard.h"

#include <cmath>

#include "ElaText.h"
#include "ElaLineEdit.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"

namespace cad::ui {

// ── 省道线态：反算跟随角度 + 偏移 d / 角度 β 编辑 (2026-08 拆分) ──
// 2026-12-15 面板重设计后本卡只保留省道态 (角度/引用/指向已抽出)。

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
    deg = cad::geo::normalizeDeg180(deg);
    return cad::geo::Units::formatDegTrimmed(deg);
}

void SegmentConnectionCard::showDartState(const cad::param::Block& block,
                                          cad::param::Segment* seg)
{
    (void)seg;
    // 2026-12 去卡框化: 卡内标题已移除, 「省道线」状态由属性页「连接」
    // 分区标题右侧 hint 显示 (LinePropertyDialog::populateFromModel)。
    // 2026-xx: 复选框已删, 连接行/滑轨行随 m_connRow/m_slideRow 隐藏。
    m_connRow->setVisible(false);
    m_slideRow->setVisible(false);
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
                refText += QStringLiteral(" ← ");
                refText += cad::param::Serial::tag(bSeg->serial);
                if (!bSeg->name.isEmpty())
                    refText += QStringLiteral("·") + bSeg->name;
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
        ? cad::geo::Units::formatCmTrimmed(block.dartOffsetMm)
        : block.dartOffsetFormula;
    if (!m_dartOffsetEdit->hasFocus()
        && m_dartOffsetEdit->text().trimmed() != offText)
        m_dartOffsetEdit->setText(offText);
    const QString angText = block.dartAngleFormula.isEmpty()
        ? cad::geo::Units::formatDegValue(block.dartAngleDeg)
        : block.dartAngleFormula;
    if (!m_dartAngleEdit->hasFocus()
        && m_dartAngleEdit->text().trimmed() != angText)
        m_dartAngleEdit->setText(angText);
}

void SegmentConnectionCard::onDocResolved()
{
    // Live 路径 (省道反算角): 基准线段被拖动/旋转时读数实时跟, 只动标签.
    if (m_doc) {
        if (const auto* block = m_doc->findBlock(m_blockId);
            block && block->isDart()) {
            const QString text = dartFoldAngleText(*block);
            if (!text.isEmpty() && m_dartFoldLabel->text() != text)
                m_dartFoldLabel->setText(text);
        }
    }
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

} // namespace cad::ui
