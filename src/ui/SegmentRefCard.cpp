#include "ui/SegmentRefCard.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/Serial.h"
#include "canvas/CanvasScene.h"
#include "ui/PointRefEdit.h"
#include "document/commands/AttachmentCommands.h"

namespace cad::ui {

namespace {
constexpr int kLabelW = 64;   ///< 标签列统一 (2026-12 去卡框化: 短词列).
constexpr int kFieldH = 35;
constexpr int kBtnW = 58;     ///< 二字按钮统一宽.
} // namespace

SegmentRefCard::SegmentRefCard(cad::param::ParamDocument* doc,
                               CanvasScene* scene, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_scene(scene)
{
    // 纯行组 (2026-12 去卡框化): 无边框/无标题, 嵌入属性页「连接」分区。
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    // ── 引用行 (两行结构): 行1 引用线段; 行2 引用点+拆开/重连 ──
    auto* refRow = new QWidget(this);
    auto* refV = new QVBoxLayout(refRow);
    refV->setContentsMargins(0, 0, 0, 0);
    refV->setSpacing(6);
    auto* refL = new QHBoxLayout();
    refL->setSpacing(6);
    auto* lblRef = new ElaText(QString::fromUtf8("基准线"), 12, refRow);
    lblRef->setFixedWidth(kLabelW);
    refL->addWidget(lblRef);
    m_lblAngleRefSeg = new ElaLineEdit(refRow);
    m_lblAngleRefSeg->setFixedWidth(140);
    m_lblAngleRefSeg->setPlaceholderText(QString::fromUtf8("线段 L# / 点 P#"));
    m_lblAngleRefSeg->setToolTip(QString::fromUtf8("输入角度基准线段 ID/名称，或该线段上的点 ID"));
    m_lblAngleRefSeg->setStyleSheet("font-size:12px;");
    refL->addWidget(m_lblAngleRefSeg);
    refL->addStretch();
    refV->addLayout(refL);
    auto* refPtL = new QHBoxLayout();
    refPtL->setSpacing(6);
    auto* lblRefPt = new ElaText(QString::fromUtf8("基准点"), 12, refRow);
    lblRefPt->setFixedWidth(kLabelW);
    refPtL->addWidget(lblRefPt);
    m_angleRefPoint = new PointRefEdit(m_doc, refRow);
    m_angleRefPoint->setObjectName(QStringLiteral("angleRefPointEdit"));
    m_angleRefPoint->setFixedWidth(140);
    m_angleRefPoint->setFixedHeight(kFieldH);
    m_angleRefPoint->setToolTip(QString::fromUtf8(
        "角度引用点，必须属于上方选定的引用线段。"));
    refPtL->addWidget(m_angleRefPoint);
    // 角度维度 拆开/重连 双面按钮 (2026-xx 用户拍板): 拆开 = 角度不再跟随
    // 基准线 (有连接线·无基准线 = 独立角); 重连 = 恢复角度跟随 (原基准或
    // 位置宿主, 反算零跳变)。与连接点按钮 (位置维度) 独立。
    m_btnAngleBase = new ElaPushButton(QString::fromUtf8("拆开"), refRow);
    m_btnAngleBase->setObjectName(QStringLiteral("angleBaseToggleBtn"));
    m_btnAngleBase->setFixedSize(kBtnW, kFieldH);
    m_btnAngleBase->setToolTip(QString::fromUtf8(
        "拆开 = 角度不再跟随基准线（位置保持吸附）；重连 = 恢复角度跟随"));
    m_btnAngleBase->setCursor(Qt::PointingHandCursor);
    refPtL->addWidget(m_btnAngleBase);
    refPtL->addStretch();
    refV->addLayout(refPtL);
    lay->addWidget(refRow);

    // ── 指向行: [指向点:][P# 140][偏移(°):][70][清除 58] ──
    auto* aimRow = new QWidget(this);
    auto* aimL = new QHBoxLayout(aimRow);
    aimL->setContentsMargins(0, 0, 0, 0);
    aimL->setSpacing(6);
    auto* lblAim = new ElaText(QString::fromUtf8("指向点"), 12, aimRow);
    lblAim->setFixedWidth(kLabelW);
    aimL->addWidget(lblAim);
    m_refAimPoint = new PointRefEdit(m_doc, aimRow);
    m_refAimPoint->setFixedWidth(140);
    m_refAimPoint->setFixedHeight(kFieldH);
    m_refAimPoint->setToolTip(QString::fromUtf8(
        "终点方向指向该点；配合长度可让终点落在目标上。"));
    aimL->addWidget(m_refAimPoint);
    aimL->addWidget(new ElaText(QString::fromUtf8("偏移(°)"), 12, aimRow));
    m_editAimOffset = new ElaLineEdit(aimRow);
    m_editAimOffset->setFixedWidth(70);
    m_editAimOffset->setPlaceholderText(QString::fromUtf8("0"));
    m_editAimOffset->setToolTip(QString::fromUtf8(
        "相对精确指向方向的偏移角，0 = 精确指向目标点。"));
    aimL->addWidget(m_editAimOffset);
    m_btnClearAim = new ElaPushButton(QString::fromUtf8("清除"), aimRow);
    m_btnClearAim->setFixedSize(kBtnW, kFieldH);
    m_btnClearAim->setToolTip(QString::fromUtf8("解除终点指向约束"));
    m_btnClearAim->setCursor(Qt::PointingHandCursor);
    aimL->addWidget(m_btnClearAim);
    aimL->addStretch();
    lay->addWidget(aimRow);

    connect(m_angleRefPoint, &PointRefEdit::pointResolved,
            this, &SegmentRefCard::onAngleRefPointResolved);
    connect(m_btnAngleBase, &QPushButton::clicked,
            this, &SegmentRefCard::onAngleBaseToggled);
    connect(m_lblAngleRefSeg, &ElaLineEdit::editingFinished,
            this, &SegmentRefCard::onAngleRefSegEdited);
    connect(m_refAimPoint, &PointRefEdit::pointResolved,
            this, &SegmentRefCard::onAimTargetResolved);
    connect(m_editAimOffset, &ElaLineEdit::editingFinished,
            this, &SegmentRefCard::onAimOffsetApply);
    connect(m_btnClearAim, &QPushButton::clicked,
            this, &SegmentRefCard::onClearAim);
}

void SegmentRefCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    refresh();
}

const cad::param::Attachment* SegmentRefCard::findFollowerAttachment() const
{
    if (!m_doc) return nullptr;
    for (const auto& att : m_doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId == m_blockId)
            return &att;
    }
    return nullptr;
}

QString SegmentRefCard::leaderRefLabel(const cad::param::Attachment& att) const
{
    if (!m_doc) return QString();
    QString segPart;
    const auto* leader = m_doc->findBlock(att.toBlockId);
    if (leader) {
        const auto* lseg = leader->findSegment(att.toSegmentId);
        if (lseg) {
            segPart = cad::param::Serial::tag(lseg->serial);
            if (!lseg->name.isEmpty())
                segPart += QStringLiteral("·") + lseg->name;
        }
    }
    return segPart.isEmpty() ? QStringLiteral("?") : segPart;
}

void SegmentRefCard::refresh()
{
    if (!m_doc) return;
    const auto* block = m_doc->findBlock(m_blockId);
    const auto* att = findFollowerAttachment();

    // 预填自动落库 (用户 2026-12): 自由态先选好引用点, 连入后第一次 refresh
    // 自动写为角度基准 —— 一次生效 (落库后 angleRefBlockId 非空即停)。
    if (att && att->angleRefBlockId.isNull() && !att->angleIndependent) {
        const QUuid rb = m_angleRefPoint->resolvedBlockId();
        const QUuid rp = m_angleRefPoint->resolvedPointId();
        if (!rb.isNull() && !rp.isNull()
            && (rb != att->toBlockId || rp != att->toPointId)) {
            const auto* refBlk = m_doc->findBlock(rb);
            const QUuid rs = refBlk ? refBlk->exitSegmentAtPoint(rp) : QUuid();
            if (!rs.isNull()) {
                if (auto* stack = m_doc->undoStack())
                    stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                        m_doc, att->id, rb, rs, rp));
                else
                    m_doc->setAttachmentAngleRef(att->id, rb, rs, rp);
            }
        }
    }

    refreshAngleRefRow(att);
    refreshAimRow(block);
}

void SegmentRefCard::refreshAngleRefRow(const cad::param::Attachment* att)
{
    m_angleRefPoint->setExcludeBlock(m_blockId);
    // 角度维度 拆开/重连 双面按钮 (2026-xx 用户拍板): 有连接即可用;
    // 拆开 = 有连接线·无基准线 (角度独立), 重连 = 恢复角度跟随。
    const bool hasAtt = att != nullptr;
    m_btnAngleBase->setEnabled(hasAtt);
    m_btnAngleBase->setText(hasAtt && att->angleIndependent
        ? QString::fromUtf8("重连") : QString::fromUtf8("拆开"));
    if (hasAtt && att->angleIndependent) {
        m_btnAngleBase->setToolTip(QString::fromUtf8(
            "重新连接角度基准：恢复角度跟随（原基准或位置宿主，反算零跳变）"));
    } else if (hasAtt && att->angleOnly) {
        m_btnAngleBase->setToolTip(QString::fromUtf8(
            "拆开角度基准：角度不再跟随基准线（位置维度已拆开 → 自由线）"));
    } else {
        m_btnAngleBase->setToolTip(QString::fromUtf8(
            "拆开角度基准：位置保持吸附，角度不再跟随基准线（有连接线、无基准线）"));
    }
    if (!att) return;   // 自由态: 保留用户已填的预填内容 (不 clear/不重写)。

    {
        const QSignalBlocker ar(m_angleRefPoint);
        const auto* refBlk = !att->angleRefBlockId.isNull()
            ? m_doc->findBlock(att->angleRefBlockId) : nullptr;
        if (refBlk && !att->angleRefSegmentId.isNull()) {
            const auto* refSeg = refBlk->findSegment(att->angleRefSegmentId);
            QUuid showPoint = att->angleRefPointId;
            if (showPoint.isNull() && refSeg)
                showPoint = refSeg->startPointId;
            m_angleRefPoint->setPoint(att->angleRefBlockId, showPoint);
        } else if (!att->angleIndependent) {
            // 未设置独立角度基准时，自动填入位置宿主作为角度引用。
            m_angleRefPoint->setPoint(att->toBlockId, att->toPointId);
        } else {
            m_angleRefPoint->clearPoint();
        }
        if (refBlk && !att->angleRefSegmentId.isNull()) {
            if (const auto* refSeg2 = refBlk->findSegment(att->angleRefSegmentId)) {
                QString t = cad::param::Serial::tag(refSeg2->serial);
                if (!refSeg2->name.isEmpty())
                    t += QStringLiteral("·") + refSeg2->name;
                m_lblAngleRefSeg->setText(t);
            } else {
                m_lblAngleRefSeg->setText(QString());
            }
        } else if (!att->angleIndependent) {
            m_lblAngleRefSeg->setText(leaderRefLabel(*att));
        } else {
            m_lblAngleRefSeg->setText(QString());
        }
    }
    m_lblAngleRefSeg->setEnabled(true);
}

void SegmentRefCard::refreshAimRow(const cad::param::Block* block)
{
    const bool hasAim = block && !block->endTargetPointId.isNull();
    if (hasAim) {
        m_refAimPoint->setExcludeBlock(m_blockId);
        m_refAimPoint->setPoint(block->endTargetBlockId, block->endTargetPointId);
        m_editAimOffset->setEnabled(true);
        m_btnClearAim->setEnabled(true);
        const QSignalBlocker ab(m_editAimOffset);
        if (!block->endTargetOffsetFormula.isEmpty())
            m_editAimOffset->setText(block->endTargetOffsetFormula);
        else
            m_editAimOffset->setText(QString::number(block->endTargetOffset, 'g', 6));
    } else {
        m_refAimPoint->setExcludeBlock(m_blockId);
        m_refAimPoint->clearPoint();
        m_editAimOffset->setEnabled(false);
        m_editAimOffset->clear();
        m_btnClearAim->setEnabled(false);
    }
}

void SegmentRefCard::onAngleRefPointResolved(const QUuid& blockId,
                                             const QUuid& pointId)
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) {
        // 自由态: 预填（连入后由 refresh 自动落库）。
        m_angleRefPoint->setPoint(blockId, pointId);
        refreshAngleRefRow(nullptr);
        return;
    }

    const auto* leader = m_doc->findBlock(blockId);
    if (!leader) { refresh(); return; }
    const QUuid segId = leader->exitSegmentAtPoint(pointId);
    if (segId.isNull()) { refresh(); return; }
    if (blockId == att->fromBlockId) { refresh(); return; }

    // 点必须属于上方选定的引用线段 (双基准一致性)。独立角态 (基准点「拆开」
    // 后) 旧基准已失效: 直接以新输入点建立新基准 (跳过 belongs 校验, 否则
    // 陈旧 angleRefSegmentId 会拦截恢复路径)。
    if (!att->angleIndependent && !att->angleRefSegmentId.isNull()) {
        if (const auto* refSeg = leader->findSegment(att->angleRefSegmentId)) {
            const bool belongs =
                pointId == refSeg->startPointId || pointId == refSeg->endPointId
                || std::find(refSeg->auxPointIds.begin(), refSeg->auxPointIds.end(),
                             pointId) != refSeg->auxPointIds.end();
            if (!belongs) { refresh(); return; }
        }
    }

    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
            m_doc, att->id, blockId, segId, pointId));
    else
        m_doc->setAttachmentAngleRef(att->id, blockId, segId, pointId);

    refresh();
    emit changed();
}

void SegmentRefCard::onAngleBaseToggled()
{
    if (!m_doc) return;
    const auto* att = findFollowerAttachment();
    if (!att) { refresh(); return; }
    // 角度维度 拆开/重连 (2026-xx 用户拍板): 拆开 = 有连接线·无基准线
    // (角度独立: 位置保持吸附、角度不再跟随); 重连 = 恢复角度跟随
    // (SetAttachmentAngleIndependentCommand(false) 反算回原基准/位置宿主)。
    // 不碰位置维度 (angleOnly) —— 两维独立。
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleIndependentCommand(
            m_doc, att->id, /*angleIndependent=*/!att->angleIndependent));
    else
        m_doc->setAttachmentAngleIndependent(att->id, !att->angleIndependent);
    refresh();
    emit changed();
}

void SegmentRefCard::onAngleRefSegEdited()
{
    if (!m_doc) return;
    const QString text = m_lblAngleRefSeg->text().trimmed();
    if (text.isEmpty()) return;

    QUuid blkId, segId, ptId;
    bool found = false;
    for (const auto& b : m_doc->blocks()) {
        if (b.id == m_blockId) continue;
        for (const auto& s : b.segments) {
            const QString label = cad::param::Serial::tag(s.serial);
            if (label.compare(text, Qt::CaseInsensitive) == 0
                || (!s.name.isEmpty() && s.name == text)) {
                blkId = b.id; segId = s.id;
                ptId = s.startPointId;
                found = true;
                break;
            }
        }
        if (found) break;
    }
    if (!found) {
        for (const auto& b : m_doc->blocks()) {
            if (b.id == m_blockId) continue;
            for (const auto& p : b.points) {
                const QString label = cad::param::Serial::tag(p.serial);
                if (label.compare(text, Qt::CaseInsensitive) == 0
                    || (!p.name.isEmpty() && p.name == text)) {
                    const QUuid s = b.exitSegmentAtPoint(p.id);
                    if (!s.isNull()) {
                        blkId = b.id; segId = s; ptId = p.id;
                        found = true;
                    }
                    break;
                }
            }
            if (found) break;
        }
    }
    if (!found) { refresh(); return; }

    if (const auto* att = findFollowerAttachment()) {
        if (auto* stack = m_doc->undoStack())
            stack->push(new cad::cmd::SetAttachmentAngleRefCommand(
                m_doc, att->id, blkId, segId, ptId));
        else
            m_doc->setAttachmentAngleRef(att->id, blkId, segId, ptId);
    } else {
        m_angleRefPoint->setPoint(blkId, ptId);
    }
    refresh();
    emit changed();
}

void SegmentRefCard::onAimTargetResolved(const QUuid& blockId,
                                         const QUuid& pointId)
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    if (const auto* target = m_doc->findBlock(blockId)) {
        if (m_doc->isAuxBlock(*target) != m_doc->isAuxBlock(*block))
            return;
    }
    block->endTargetBlockId = blockId;
    block->endTargetPointId = pointId;
    refresh();
    emit changed();
}

void SegmentRefCard::onAimOffsetApply()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block || block->endTargetPointId.isNull()) return;
    const QString text = m_editAimOffset->text().trimmed();
    bool isNum = false;
    const double val = text.toDouble(&isNum);
    if (isNum) {
        block->endTargetOffset = val;
        block->endTargetOffsetFormula.clear();
    } else if (!text.isEmpty()) {
        block->endTargetOffsetFormula = text;
    }
    refresh();
    emit changed();
}

void SegmentRefCard::onClearAim()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;
    block->endTargetBlockId = QUuid();
    block->endTargetPointId = QUuid();
    block->endTargetOffset = 0.0;
    block->endTargetOffsetFormula.clear();
    refresh();
    emit changed();
}

} // namespace cad::ui
