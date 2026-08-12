#pragma once

#include <QUuid>
#include <QSet>
#include <QHash>
#include <QList>

#include <functional>

#include "geometry/Vec2.h"
#include "parametric/Duplicate.h"
#include "tools/SelectState.h"

class QUndoStack;
class CanvasScene;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// Ctrl+drag quick-copy gesture of the selection tool (快捷复制): clones the
/// CONFIRMED selection, drags the fresh copies live, and replays the whole
/// copy through the undo stack as ONE step on release (Esc aborts).
/// The owning tool dispatches mouse/key events here while active().
class CopyDragController
{
public:
    using Vec2 = cad::geo::Vec2;

    /// @p setState transitions the owning tool into CopyDragging.
    /// @p restoreAfterCancel returns to Confirmed/Selecting/Idle (a completed
    ///        copy exits via clearSelectionAndIdle instead).
    /// @p clearSelectionAndIdle drop the selection lock after a completed copy.
    CopyDragController(CanvasScene* scene, cad::param::ParamDocument* doc,
                       QUndoStack* undoStack,
                       const std::function<void(SelectState)>& setState,
                       const std::function<void()>& restoreAfterCancel,
                       const std::function<void()>& clearSelectionAndIdle);

    [[nodiscard]] bool active() const { return m_active; }

    /// Start the gesture over the given (already confirmed) selection.
    void begin(const QSet<QUuid>& selection, const QUuid& hitBlockId,
               const Vec2& pos);
    void move(const Vec2& pos);
    void release(const Vec2& pos);
    /// Esc: discard the preview clones.
    void cancel();

private:
    void removeCopyPreview();   ///< Remove live preview clones from doc.
    void finish();              ///< Reset copy state + restore SelectState.

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    std::function<void(SelectState)> m_setState;
    std::function<void()> m_restoreAfterCancel;
    std::function<void()> m_clearSelectionAndIdle;

    bool m_active = false;
    cad::param::DuplicateResult m_copyResult;  ///< Pristine clones for the command.
    Vec2 m_copyStartPos;                       ///< Press point (drag anchor).
    QHash<QUuid, Vec2> m_copyOrigins;          ///< Clone origins after initial add.
};

} // namespace cad::tools
