#include "ui/SegmentShadowBasisCard.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "geometry/Angle.h"
#include "geometry/Units.h"
#include "parametric/FollowerAngle.h"
#include "ui/FormScaffold.h"
#include "ui/TooltipFormatter.h"
#include "ui/Theme.h"
#include "document/commands/AttachmentCommands.h"
#include "document/commands/BlockCommands.h"
#include "ui/UiStrings.h"

namespace cad::ui {

namespace {
constexpr int kFieldH = 30;
constexpr int kRefEditW = 88;

} // namespace

SegmentShadowBasisCard::SegmentShadowBasisCard(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    m_lblBasis = new ElaText(QString::fromUtf8("基准："), 11, this);
    m_lblBasis->setFixedWidth(34);
    row->addWidget(m_lblBasis);

    m_shadowAngleEdit = new ElaLineEdit(this);
    m_shadowAngleEdit->setObjectName(QStringLiteral("shadowAngleEdit"));
    m_shadowAngleEdit->setFixedWidth(kRefEditW);
    m_shadowAngleEdit->setPlaceholderText(QStringLiteral("±180°"));
    row->addWidget(m_shadowAngleEdit);

    m_btnClearShadow = new QPushButton(QString::fromUtf8("清除基准"), this);
    m_btnClearShadow->setObjectName(QStringLiteral("clearShadowBtn"));
    m_btnClearShadow->setFixedHeight(kFieldH);
    m_btnClearShadow->setStyleSheet(cad::ui::chipButtonStyle());
    m_btnClearShadow->setCursor(Qt::PointingHandCursor);
    row->addWidget(m_btnClearShadow);

    connect(m_shadowAngleEdit, &QLineEdit::returnPressed,
            this, &SegmentShadowBasisCard::onShadowAngleEdited);
    connect(m_btnClearShadow, &QPushButton::clicked,
            this, &SegmentShadowBasisCard::onClearShadowClicked);
}

void SegmentShadowBasisCard::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    refresh();
}

void SegmentShadowBasisCard::refresh()
{
    if (!m_doc) return;
    const auto* att = m_doc->findFollowerAttachmentOf(m_blockId);
    const auto* toBlkChk = att ? m_doc->findBlock(att->toBlockId) : nullptr;
    if (!toBlkChk || !toBlkChk->isShadow) return;

    double shadowAngle = 0.0;
    bool shadowMounted = false;
    const cad::param::Attachment* att1 = nullptr;
    for (const auto& a : m_doc->attachments()) {
        if (!a.isPin && a.fromBlockId == toBlkChk->id) { att1 = &a; break; }
    }
    if (att1) {
        shadowMounted = true;
        shadowAngle = cad::geo::normalizeDeg180(att1->followerAngle);
    } else {
        if (const auto* sh = m_doc->findBlock(toBlkChk->id)) {
            const double world = sh->transform.rotation
                + sh->exitDirectionAtPoint(att->toPointId, att->toSegmentId);
            shadowAngle = cad::geo::normalizeDeg180(cad::geo::radToDeg(world));
        }
    }
    {
        const QSignalBlocker se(m_shadowAngleEdit);
        m_shadowAngleEdit->setText(
            cad::geo::Units::formatDegValue(shadowAngle));
        m_shadowAngleEdit->setToolTip(shadowMounted
            ? cad::ui::TooltipFormatter::action(
                QStringLiteral("基准角度 (挂载态)"),
                QStringLiteral("相对宿主线的基准折角。回车提交，本线方向随之变化；offset 公式不受影响。"))
            : cad::ui::TooltipFormatter::action(
                QStringLiteral("基准角度 (独立态)"),
                QStringLiteral("独立基准线（拆开瞬间冻结）的方向角。回车提交，本线绕对齐点原地转；offset 公式不受影响。")));
    }
    if (m_btnClearShadow) {
        m_btnClearShadow->setToolTip(shadowMounted
            ? cad::ui::TooltipFormatter::action(
                cad::ui::str::kClearBasis,
                QStringLiteral("清除基准线与其挂载连接，本线变为纯自由线。"))
            : cad::ui::TooltipFormatter::action(
                cad::ui::str::kClearBasis,
                QStringLiteral("清除基准线，本线变纯自由线 (角度/位置全自由)。")));
    }
}

void SegmentShadowBasisCard::onShadowAngleEdited()
{
    if (!m_doc) return;
    const auto* att = m_doc->findFollowerAttachmentOf(m_blockId);
    const auto* toBlk = att ? m_doc->findBlock(att->toBlockId) : nullptr;
    if (!att || !toBlk || !toBlk->isShadow) { refresh(); return; }

    // 解析统一走 parseAngleText (审计 UI-P0-7): 允许 "°" 后缀。
    const auto parsed = cad::geo::parseAngleText(m_shadowAngleEdit->text());
    if (!parsed.isNumber) { refresh(); return; }
    const double inputDeg = parsed.value;

    const cad::param::Attachment* att1 = nullptr;
    for (const auto& a : m_doc->attachments()) {
        if (!a.isPin && a.fromBlockId == toBlk->id) { att1 = &a; break; }
    }

    auto* mutBlk = m_doc->findBlock(att->fromBlockId);
    if (!mutBlk) { refresh(); return; }

    if (att1) {
        if (auto* stack = m_doc->undoStack())
            stack->push(new cad::cmd::SetFollowerAngleCommand(
                m_doc, att1->id, cad::param::followerAngleToStorage(inputDeg)));
        else if (auto* mut = m_doc->findAttachment(att1->id)) {
            mut->followerAngle = cad::param::followerAngleToStorage(inputDeg);
            m_doc->resolveAll();
        }
    } else {
        auto* sh = m_doc->findBlock(toBlk->id);
        if (!sh) { refresh(); return; }
        const auto* anchor = mutBlk->findPoint(att->fromPointId);
        if (!anchor || !anchor->resolved) { refresh(); return; }
        const double curWorld = sh->transform.rotation
            + sh->exitDirectionAtPoint(att->toPointId, att->toSegmentId);
        const double deltaRad = cad::geo::degToRad(
            cad::geo::normalizeDeg180(inputDeg - cad::geo::radToDeg(curWorld)));
        const cad::param::Transform2D shOld = sh->transform;
        cad::param::Transform2D shNew = shOld;
        shNew.rotation += deltaRad;
        const cad::param::Transform2D flOld = mutBlk->transform;
        cad::param::Transform2D flNew = flOld;
        flNew.rotation += deltaRad;
        const geo::Vec2 p3World = mutBlk->worldPos(att->fromPointId);
        flNew.origin = p3World - anchor->resolvedPos.rotated(flNew.rotation);
        if (auto* stack = m_doc->undoStack()) {
            stack->push(new cad::cmd::ShadowRotateCommand(
                m_doc, toBlk->id, shOld, shNew,
                mutBlk->id, flOld, flNew));
        } else {
            sh->transform = shNew;
            mutBlk->transform = flNew;
            m_doc->resolveAll();
        }
    }
    refresh();
    emit changed();
}

void SegmentShadowBasisCard::onClearShadowClicked()
{
    if (!m_doc) return;
    const auto* att = m_doc->findFollowerAttachmentOf(m_blockId);
    const auto* toBlk = att ? m_doc->findBlock(att->toBlockId) : nullptr;
    if (!att || !toBlk || !toBlk->isShadow) { refresh(); return; }
    if (auto* stack = m_doc->undoStack())
        stack->push(new cad::cmd::RemoveShadowCommand(m_doc, toBlk->id));
    else
        m_doc->removeShadow(toBlk->id);
    refresh();
    emit changed();
}

} // namespace cad::ui
