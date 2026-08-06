#pragma once

#include <QUndoCommand>
#include <QUuid>
#include <vector>

#include "parametric/Attachment.h"
#include "parametric/Block.h"

namespace cad::param { class ParamDocument; }

namespace cad::cmd {

/// Add an attachment between two blocks.
class AddAttachmentCommand : public QUndoCommand
{
public:
    AddAttachmentCommand(cad::param::ParamDocument* doc,
                         cad::param::Attachment att,
                         QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Attachment m_att;
};

/// Remove an attachment by ID.
class RemoveAttachmentCommand : public QUndoCommand
{
public:
    RemoveAttachmentCommand(cad::param::ParamDocument* doc,
                            const QUuid& attId,
                            QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;

private:
    cad::param::ParamDocument* m_doc;
    cad::param::Attachment m_att;  ///< Saved for undo.
    /// Removing a bridge pin releases the bridge (it becomes an independent
    /// segment) — snapshot its pre-removal state + attachments for undo.
    cad::param::Block m_bridge;
    std::vector<cad::param::Attachment> m_bridgeAtts;
    bool m_hasBridge = false;
};

/// Set the follower angle (followerAngle) and/or arc-length rotation state
/// of an attachment. Supports both angle and arc-length modes.
class SetFollowerAngleCommand : public QUndoCommand
{
public:
    SetFollowerAngleCommand(cad::param::ParamDocument* doc,
                          const QUuid& attId, double newAngle,
                          const QString& newFormula = QString(),
                          cad::param::RotationMode newMode = cad::param::RotationMode::Angle,
                          double newArcLength = 0.0,
                          const QString& newArcFormula = QString(),
                          QUndoCommand* parent = nullptr);
    void redo() override;
    void undo() override;
    int id() const override { return 1002; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    cad::param::ParamDocument* m_doc;
    QUuid m_attId;
    double m_oldAngle;
    double m_newAngle;
    QString m_oldFormula;
    QString m_newFormula;
    cad::param::RotationMode m_oldMode;
    cad::param::RotationMode m_newMode;
    double m_oldArcLength;
    double m_newArcLength;
    QString m_oldArcFormula;
    QString m_newArcFormula;
};

} // namespace cad::cmd
