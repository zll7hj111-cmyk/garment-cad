#pragma once

#include <QUuid>

#include "ElaScrollPageArea.h"

class ElaLineEdit;
class ElaCheckBox;
class ElaPushButton;

namespace cad::param {
class ParamDocument;
struct Attachment;
}

namespace cad::ui {

class PointRefEdit;

/// "终点指向" card of the line property dialog: edits the endpoint-aim
/// constraint (Block::endTarget*) of the target block. Changes apply LIVE
/// to the model (same semantics as the rest of the dialog); the owning
/// dialog re-syncs the scene and its sibling cards via changed().
class SegmentAimCard : public ElaScrollPageArea
{
    Q_OBJECT

public:
    /// What kind of aim mutation just happened (drives the dialog-side
    /// refresh granularity — mirror of the pre-extraction slot effects).
    enum class ChangeKind {
        TargetSet,    ///< Aim target point (re)assigned.
        OffsetApplied,///< Offset angle / formula applied.
        Cleared,      ///< Aim constraint removed.
        HostToggled,  ///< 终点指向宿主 checkbox flipped (may back-solve
                      ///< the follower angle — angle card must re-sync).
    };

    explicit SegmentAimCard(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    /// Switch the block being edited (dialog target change).
    void setTarget(const QUuid& blockId);
    /// Sync the widgets from the model (populate / post-mutation refresh).
    void refresh();

signals:
    void changed(ChangeKind kind);

private:
    void onTargetResolved(const QUuid& blockId, const QUuid& pointId);
    void onOffsetApply();
    void onClear();
    void onHostToggled(bool on);

    /// Non-pin follower attachment of the target block (nullptr when free).
    [[nodiscard]] const cad::param::Attachment* findFollowerAttachment() const;

    cad::param::ParamDocument* m_doc = nullptr;
    QUuid m_blockId;

    PointRefEdit* m_refTarget   = nullptr;   ///< Aim target point (editable).
    ElaLineEdit*    m_editOffset  = nullptr;   ///< Offset angle (°) from exact aim.
    ElaPushButton*  m_btnClear    = nullptr;   ///< Clear the aim constraint.
    ElaCheckBox*    m_chkHost     = nullptr;   ///< "终点指向宿主 P8" (all lines).
};

} // namespace cad::ui
