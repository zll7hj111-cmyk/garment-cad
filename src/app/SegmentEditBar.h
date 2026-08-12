#pragma once

#include <QWidget>
#include <QUuid>

#include "document/commands/BlockCommands.h"

class ElaText;
class ElaLineEdit;
class QTimer;
class QUndoStack;

namespace cad::param {
class ParamDocument;
struct Attachment;
}

namespace cad::app {

/// Status-bar edit strip for a freshly created segment (智能笔创建后内嵌编辑,
/// 替代旧的创建弹窗).
///
/// Two modes:
///  - Preview (创建中): read-only "长度 xx cm | 角度 xx°" following the mouse.
///  - Edit (创建后):   [L3] 名称[...] 长度[...] 角度[...] — live-apply on
///    editingFinished (200 ms debounce), matching LinePropertyDialog semantics.
///
/// Esc inside any edit field emits cancelRequested() — the host (MainWindow)
/// undoes the creation command (创建后 Esc = 删线) and hides this bar.
class SegmentEditBar : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentEditBar(cad::param::ParamDocument* paramDoc, QWidget* parent = nullptr);

    /// Switch to edit mode for the given segment: fill id/name/length/angle
    /// from the model, show the bar and focus the name field. Pass
    /// @p grabFocus = false to show without stealing keyboard focus (the
    /// selection tool's single-click pick must not fight the canvas).
    void showForLine(const QUuid& blockId, const QUuid& segmentId, bool grabFocus = true);
    /// Switch to read-only preview mode (creation in progress): shows the
    /// live length/angle readout. Call with both zero to hide the preview.
    void showPreview(double lenCm, double angleDeg);
    /// Hide the whole bar (host called after undo / tool switch).
    void hideBar();

    /// Inject the undo stack: edits commit through SegmentEditBarCommand so
    /// undo/redo AND the dirty flag stay consistent. Also rewinds the stack
    /// to the showForLine() position on cancelCreation().
    void setUndoStack(QUndoStack* stack);
    /// Esc semantics: rewind the undo stack to the creation point (撤销创建 +
    /// 本次编辑), then hide. No-op without an undo stack.
    void cancelCreation();

signals:
    /// Esc pressed inside an edit field: host undoes the creation (删线).
    void cancelRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyName();
    void applyLength();
    void applyAngle();
    /// Repopulate all fields from the model (after a live edit re-resolve).
    void refreshFields();
    /// Snapshot the model's current edit-strip state (baseline for the next
    /// commit — the command captures its own OLD state, this is the NEW one).
    cad::cmd::SegmentEditBarCommand::State snapshotState() const;
    /// First attachment owned by this block (follower-angle editing target;
    /// same matching rule as refreshFields and the property dialog).
    [[nodiscard]] const cad::param::Attachment* findEditAttachment() const;
    /// Push the state as an undoable command (falls back to apply+resolve
    /// when no undo stack is injected, e.g. headless tests).
    void commitState(cad::cmd::SegmentEditBarCommand::State st);

    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack* m_undoStack = nullptr;
    /// Stack index at showForLine() — cancelCreation() rewinds to it, undoing
    /// the creation command together with every strip edit pushed since.
    int m_editStartIndex = 0;
    QUuid m_blockId;
    QUuid m_segmentId;

    ElaText*     m_idLabel   = nullptr;  ///< "L3" (Serial::tag, no random prefix).
    ElaLineEdit* m_nameEdit  = nullptr;
    ElaLineEdit* m_lenEdit   = nullptr;
    ElaLineEdit* m_angleEdit = nullptr;
    QTimer*    m_debounce  = nullptr;  ///< 200 ms auto-apply for length/angle.
};

} // namespace cad::app
