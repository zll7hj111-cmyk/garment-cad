#include "SegmentAimCard.h"

#include "ElaCheckBox.h"
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "parametric/FollowerAngle.h"
#include "PointRefEdit.h"

namespace cad::tools {

SegmentAimCard::SegmentAimCard(cad::param::ParamDocument* doc, QWidget* parent)
    : ElaScrollPageArea(parent)
    , m_doc(doc)
{
    // ElaScrollPageArea's constructor hard-codes setFixedHeight(75); lift it
    // so the card sizes itself from its content layout.
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    auto* aimLayout = new QVBoxLayout(this);
    aimLayout->setContentsMargins(10, 8, 10, 10);
    aimLayout->setSpacing(6);

    auto* aimTitle = new ElaText(QString::fromUtf8("终点指向"), 13, this);  // 终点指向
    aimTitle->setStyleSheet("font-weight:600;");
    aimLayout->addWidget(aimTitle);

    auto* aimRow = new QHBoxLayout();
    aimRow->setSpacing(6);
    auto* lblAimCaption = new ElaText(QString::fromUtf8("\u6307\u5411\u70b9:"), 13, this);  // 指向点:
    lblAimCaption->setStyleSheet(QString());
    aimRow->addWidget(lblAimCaption);
    m_refTarget = new PointRefEdit(m_doc, this);
    m_refTarget->setMaximumWidth(150);
    m_refTarget->setToolTip(QString::fromUtf8(
        "\u7ec8\u70b9\u65b9\u5411\u59cb\u7ec8\u6307\u5411\u8be5\u70b9\uff08\u914d\u5408\u957f\u5ea6\u53ef\u8ba9\u7ec8\u70b9\u843d\u5728\u76ee\u6807\u4e0a\uff09\u3002\u8f93\u5165 P \u7f16\u53f7\u56de\u8f66\u8bbe\u7f6e"));
    // 终点方向始终指向该点（配合长度可让终点落在目标上）。输入 P 编号回车设置
    aimRow->addWidget(m_refTarget);
    aimRow->addWidget(new ElaText(QString::fromUtf8("\u504f\u79fb(\u00b0):"), 13, this));  // 偏移(°):
    m_editOffset = new ElaLineEdit(this);
    m_editOffset->setMaximumWidth(70);
    m_editOffset->setToolTip(QString::fromUtf8(
        "\u76f8\u5bf9\u7cbe\u786e\u6307\u5411\u65b9\u5411\u7684\u504f\u79fb\u89d2\uff0c0=\u7cbe\u786e\u6307\u5411\u76ee\u6807\u70b9"));
    aimRow->addWidget(m_editOffset);
    m_btnClear = new ElaPushButton(QString::fromUtf8("\u6e05\u9664"), this);  // 清除
    m_btnClear->setToolTip(QString::fromUtf8("\u89e3\u9664\u7ec8\u70b9\u6307\u5411\u7ea6\u675f\uff0c\u89d2\u5ea6\u6062\u590d\u4e3a\u81ea\u7531\u4e16\u754c\u89d2"));
    m_btnClear->setCursor(Qt::PointingHandCursor);
    aimRow->addWidget(m_btnClear);
    aimRow->addStretch();
    aimLayout->addLayout(aimRow);

    // Aim-host checkbox (ALL lines): checked = the end point always aims at
    // the target; unchecking releases the aim (visible via refresh()).
    m_chkHost = new ElaCheckBox(this);
    
    aimLayout->addWidget(m_chkHost);

    connect(m_refTarget, &PointRefEdit::pointResolved,
            this, &SegmentAimCard::onTargetResolved);
    connect(m_editOffset, &QLineEdit::editingFinished,
            this, &SegmentAimCard::onOffsetApply);
    connect(m_btnClear, &QPushButton::clicked,
            this, &SegmentAimCard::onClear);
    connect(m_chkHost, &QCheckBox::toggled,
            this, &SegmentAimCard::onHostToggled);
}

void SegmentAimCard::setTarget(const QUuid& blockId)
{
    m_blockId = blockId;
    refresh();
}

void SegmentAimCard::refresh()
{
    if (!m_refTarget || !m_doc) return;
    const auto* block = m_doc->findBlock(m_blockId);

    if (!block || block->endTargetPointId.isNull()) {
        // No aim constraint: empty ref (placeholder), offset disabled.
        m_refTarget->setExcludeBlock(m_blockId);
        m_refTarget->clearPoint();
        m_editOffset->setEnabled(false);
        m_editOffset->clear();
        m_btnClear->setEnabled(false);
    } else {
        m_refTarget->setExcludeBlock(m_blockId);
        m_refTarget->setPoint(block->endTargetBlockId, block->endTargetPointId);
        m_editOffset->setEnabled(true);
        m_btnClear->setEnabled(true);

        const QSignalBlocker b(m_editOffset);
        if (!block->endTargetOffsetFormula.isEmpty())
            m_editOffset->setText(block->endTargetOffsetFormula);
        else
            m_editOffset->setText(QString::number(block->endTargetOffset, 'g', 6));
    }

    // ── Aim-host checkbox (all lines): host = the aim target, or the aim
    // input's resolved value when no aim constraint exists yet.
    if (block) {
        const QUuid hostBlock = !block->endTargetBlockId.isNull()
            ? block->endTargetBlockId : m_refTarget->resolvedBlockId();
        const QUuid hostPoint = !block->endTargetPointId.isNull()
            ? block->endTargetPointId : m_refTarget->resolvedPointId();
        const auto* hostBlk = !hostBlock.isNull() ? m_doc->findBlock(hostBlock) : nullptr;
        const auto* hostB = hostBlk ? hostBlk->findPoint(hostPoint) : nullptr;

        const bool endFollowing = !block->endTargetPointId.isNull();

        const QSignalBlocker b(m_chkHost);
        m_chkHost->setText(hostB
            ? QString::fromUtf8("\u7ec8\u70b9\u6307\u5411\u5bbf\u4e3b %1")  // 终点指向宿主 %1
                .arg(cad::param::Serial::tag(hostB->serial))
            : QString::fromUtf8("\u7ec8\u70b9\u6307\u5411\u5bbf\u4e3b"));  // 终点指向宿主
        m_chkHost->setChecked(endFollowing);
        m_chkHost->setEnabled(hostB != nullptr);
        m_chkHost->setToolTip(hostB
            ? QString::fromUtf8(
                  "\u52fe\u9009\u540e\u7ec8\u70b9\u59cb\u7ec8\u6307\u5411\u5bbf\u4e3b\u70b9 %1\uff1b"
                  "\u914d\u5408\u6d4b\u91cf\u957f\u5ea6\u4e0e\u8d77\u70b9\u8ddf\u968f\uff0c\u7ec8\u70b9\u59cb\u7ec8\u843d\u5728\u5bbf\u4e3b\u70b9\u4e0a")
                  .arg(cad::param::Serial::tag(hostB->serial))
            : QString::fromUtf8(
                  "\u52fe\u9009\u540e\u7ec8\u70b9\u6307\u5411\u4e0a\u65b9\u8f93\u5165\u7684\u76ee\u6807\u70b9\uff1b\u53d6\u6d88\u52fe\u9009\u91ca\u653e\u6307\u5411"));
        // 勾选后终点始终指向宿主点；配合测量长度与起点跟随，终点始终落在宿主点上
        m_chkHost->setVisible(true);
    }

    // Group protection: endpoint-aim drives the block's ROTATION — it would
    // break group rigidity, so aim editing is read-only on grouped members
    // (组成员不可编辑终点指向, 请先解散组).
    if (block && !m_doc->groupOfBlock(m_blockId).isNull()) {
        const QString tip = QString::fromUtf8(
            "\xe7\xbb\x84\xe6\x88\x90\xe5\x91\x98\xe4\xb8\x8d\xe5\x8f\xaf"
            "\xe7\xbc\x96\xe8\xbe\x91\xe7\xbb\x88\xe7\x82\xb9\xe6\x8c\x87"
            "\xe5\x90\x91\xef\xbc\x8c\xe8\xaf\xb7\xe5\x85\x88\xe8\xa7\xa3"
            "\xe6\x95\xa3\xe7\xbb\x84");
        m_refTarget->setEnabled(false);
        m_editOffset->setEnabled(false);
        m_btnClear->setEnabled(false);
        m_chkHost->setEnabled(false);
        m_refTarget->setToolTip(tip);
        m_editOffset->setToolTip(tip);
        m_btnClear->setToolTip(tip);
        m_chkHost->setToolTip(tip);
    }
}

const cad::param::Attachment* SegmentAimCard::findFollowerAttachment() const
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

void SegmentAimCard::onTargetResolved(const QUuid& blockId, const QUuid& pointId)
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    // The aux layer is sealed: a working-layer block cannot aim at an
    // aux-layer point (and vice versa).
    if (const auto* target = m_doc->findBlock(blockId)) {
        if (m_doc->isAuxBlock(*target) != m_doc->isAuxBlock(*block))
            return;
    }

    block->endTargetBlockId = blockId;
    block->endTargetPointId = pointId;
    // Keep the existing offset unchanged (user may have a deliberate offset).

    emit changed(ChangeKind::TargetSet);
    refresh();
}

void SegmentAimCard::onOffsetApply()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block || block->endTargetPointId.isNull()) return;

    const QString text = m_editOffset->text().trimmed();
    bool isNum = false;
    const double val = text.toDouble(&isNum);
    if (isNum) {
        block->endTargetOffset = val;
        block->endTargetOffsetFormula.clear();
    } else if (!text.isEmpty()) {
        block->endTargetOffsetFormula = text;
    }

    emit changed(ChangeKind::OffsetApplied);
    refresh();
}

void SegmentAimCard::onClear()
{
    if (!m_doc) return;
    auto* block = m_doc->findBlock(m_blockId);
    if (!block) return;

    block->endTargetBlockId = QUuid();
    block->endTargetPointId = QUuid();
    block->endTargetOffset = 0.0;
    block->endTargetOffsetFormula.clear();

    emit changed(ChangeKind::Cleared);
    refresh();
}

void SegmentAimCard::onHostToggled(bool on)
{
    if (!m_doc) return;
    cad::param::Block* block = m_doc->findBlock(m_blockId);
    if (!block) { refresh(); return; }

    if (on) {
        // Host = the current aim target, or the aim input's resolved value.
        const QUuid hostBlock = !block->endTargetBlockId.isNull()
            ? block->endTargetBlockId : m_refTarget->resolvedBlockId();
        const QUuid hostPoint = !block->endTargetPointId.isNull()
            ? block->endTargetPointId : m_refTarget->resolvedPointId();
        const cad::param::Block* host = m_doc->findBlock(hostBlock);
        if (!host || !host->findPoint(hostPoint)) { refresh(); return; }
        // Aux-layer seal: never aim a working-layer block at an aux point.
        if (m_doc->isAuxBlock(*host) != m_doc->isAuxBlock(*block)) {
            refresh(); return;
        }
        block->endTargetBlockId = hostBlock;
        block->endTargetPointId = hostPoint;
        block->endTargetOffset = 0.0;
        block->endTargetOffsetFormula.clear();
    } else {
        // Freeze the aimed direction before releasing: when a follower
        // attachment drives the rotation, back-solve its follower angle
        // from the CURRENT (aimed) rotation so the line doesn't jump.
        if (const auto* fatt = findFollowerAttachment()) {
            if (const auto* leader = m_doc->findBlock(fatt->toBlockId)) {
                if (auto* a = m_doc->findAttachment(fatt->id)) {
                    const double refWorld = leader->transform.rotation
                        + leader->exitDirectionAtPoint(a->toPointId, a->toSegmentId);
                    const double localDir = block->directionAtPoint(a->fromPointId);
                    a->followerAngle = cad::param::backSolveFollowerAngle(
                        block->transform.rotation, localDir, refWorld);
                    a->followerAngleFormula.clear();
                    a->rotationMode = cad::param::RotationMode::Angle;
                    a->arcLength = 0.0;
                    a->arcLengthFormula.clear();
                }
            }
        }
        block->endTargetBlockId = QUuid();
        block->endTargetPointId = QUuid();
        block->endTargetOffset = 0.0;
        block->endTargetOffsetFormula.clear();
    }

    emit changed(ChangeKind::HostToggled);
    refresh();
}

} // namespace cad::tools
