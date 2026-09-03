#include "CopyDragController.h"

#include <QUndoStack>

#include "parametric/ParamDocument.h"
#include "canvas/CanvasScene.h"
#include "document/commands/BlockCommands.h"
#include "parametric/ParamDocumentRaw.h"

namespace cad::tools {

CopyDragController::CopyDragController(CanvasScene* scene,
                                       cad::param::ParamDocument* doc,
                                       QUndoStack* undoStack,
                                       const std::function<void(SelectState)>& setState,
                                       const std::function<void()>& restoreAfterCancel,
                                       const std::function<void()>& clearSelectionAndIdle)
    : m_scene(scene)
    , m_paramDoc(doc)
    , m_undoStack(undoStack)
    , m_setState(setState)
    , m_restoreAfterCancel(restoreAfterCancel)
    , m_clearSelectionAndIdle(clearSelectionAndIdle)
{
}

void CopyDragController::begin(const QSet<QUuid>& selection,
                               const QUuid& hitBlockId, const Vec2& pos)
{
    // Copy set: the CONFIRMED selection — the gesture is only dispatched when
    // the hit belongs to it (复制仅对已确认选择集生效, 见 mousePress).
    QList<QUuid> ids = selection.values();
    if (!ids.contains(hitBlockId))
        ids = {hitBlockId};   // safety net — not reachable via normal dispatch

    m_copyResult = cad::param::duplicateBlocks(*m_paramDoc, ids);
    if (m_copyResult.isEmpty()) return;

    // Live preview: add the clones directly (no undo). The whole gesture is
    // replayed through the undo stack at release (restore-then-replay, same
    // pattern as finalizeConnection).
    for (const auto& lv : m_copyResult.newLinked)
        m_paramDoc->addLinked(lv);
    for (const auto& b : m_copyResult.blocks)
        m_paramDoc->addBlock(b);
    // Verbatim: cloned connections keep the ORIGINAL's isLocked (复制语义).
    cad::param::RawModelAccess::addAttachmentsRaw(*m_paramDoc, m_copyResult.attachments);
    for (const auto& c : m_copyResult.components)
        m_paramDoc->addComponent(c);

    // Anchor the drag on the LIVE origins (followers were just resolver-placed).
    m_copyStartPos = pos;
    m_copyOrigins.clear();
    for (const auto& b : m_copyResult.blocks) {
        if (const auto* live = m_paramDoc->findBlock(b.id))
            m_copyOrigins.insert(b.id, live->transform.origin);
    }
    m_scene->refreshAllBlockItems();
    m_active = true;
    m_setState(SelectState::CopyDragging);
}

void CopyDragController::move(const Vec2& pos)
{
    const Vec2 delta = pos - m_copyStartPos;
    for (auto it = m_copyOrigins.cbegin(); it != m_copyOrigins.cend(); ++it) {
        if (auto* b = m_paramDoc->findBlock(it.key()))
            b->transform.origin = it.value() + delta;
    }
    // Live resolve: copied followers cascade every frame (see updateDrag).
    // Seeds = the cloned set (their attachments live entirely inside it).
    for (auto it = m_copyOrigins.cbegin(); it != m_copyOrigins.cend(); ++it) {
        if (auto* b = m_paramDoc->findBlock(it.key()))
            m_paramDoc->invalidateLayer(b->layer);
    }
    m_paramDoc->resolveForDrag(m_copyOrigins.keys());
}

void CopyDragController::release(const Vec2& pos)
{
    const Vec2 delta = pos - m_copyStartPos;

    // Remove the preview clones, then replay the copy as ONE undo step at
    // the drop position. A jitter/zero-delta release (accidental Ctrl+click,
    // < 5 screen px) drops the copy — an exact overlap of the original would
    // be invisible or create ghost duplicate geometry.
    removeCopyPreview();
    const double zoom = m_scene ? m_scene->currentZoom() : 1.0;
    const double thresh = 5.0 / (zoom > 1e-9 ? zoom : 1.0);
    if (delta.length() > thresh && m_undoStack) {
        for (auto& b : m_copyResult.blocks)
            b.transform.origin = b.transform.origin + delta;
        m_undoStack->push(new cad::cmd::DuplicateBlocksCommand(
            m_paramDoc, std::move(m_copyResult)));
        // Same as a completed move: drop the selection lock (用户拍板：
        // 复制完成后退出选中锁定状态).
        m_copyResult = {};
        m_copyOrigins.clear();
        if (m_scene) m_scene->refreshAllBlockItems();
        m_active = false;
        m_clearSelectionAndIdle();
        return;
    }
    finish();
}

void CopyDragController::cancel()
{
    removeCopyPreview();
    finish();
}

void CopyDragController::removeCopyPreview()
{
    if (!m_paramDoc) return;
    for (const auto& c : m_copyResult.components)
        m_paramDoc->removeComponentRecord(c.id);
    // Attachments first (removeBlock would drop them anyway); new linked
    // variables last — they reference ORIGINAL blocks and survive removeBlock.
    for (const auto& att : m_copyResult.attachments)
        m_paramDoc->removeAttachment(att.id);
    for (const auto& b : m_copyResult.blocks)
        m_paramDoc->removeBlock(b.id);
    for (const auto& lv : m_copyResult.newLinked)
        m_paramDoc->removeLinked(lv.id);
}

void CopyDragController::finish()
{
    m_copyResult = {};
    m_copyOrigins.clear();
    if (m_scene) m_scene->refreshAllBlockItems();
    m_active = false;
    // Cancelled / zero-delta copy: keep the confirmed selection so the user
    // can retry the gesture (a COMPLETED copy exits via clearSelectionAndIdle).
    m_restoreAfterCancel();
}

} // namespace cad::tools
