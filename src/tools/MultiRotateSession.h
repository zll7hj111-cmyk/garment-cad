#pragma once

#include <QUuid>
#include <QSet>
#include <QHash>
#include <vector>

#include "geometry/Vec2.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"

class CanvasScene;
class QUndoStack;

namespace cad::param {
class ParamDocument;
}

namespace cad::tools {

/// Multi-block rotation session managing multiple selected blocks,
/// their rigid-body transform snapshots, and released attachment restoration.
class MultiRotateSession
{
public:
    struct MultiBlockBase {
        cad::param::Transform2D tf;
        QUuid endTargetBlock;
        QUuid endTargetPoint;
    };

    MultiRotateSession() = default;

    void adoptSelection(cad::param::ParamDocument* doc, const QSet<QUuid>& blockIds);
    void clear();

    void captureBase(cad::param::ParamDocument* doc);
    void restoreBase(cad::param::ParamDocument* doc, CanvasScene* scene);
    bool commit(cad::param::ParamDocument* doc, QUndoStack* undoStack);
    void applyModeValue(cad::param::ParamDocument* doc,
                        CanvasScene* scene,
                        const cad::geo::Vec2& pivot,
                        double value);

    [[nodiscard]] const QSet<QUuid>& selection() const { return m_selection; }
    [[nodiscard]] QSet<QUuid>& selection() { return m_selection; }
    [[nodiscard]] bool isMultiSelect() const { return m_selection.size() > 1; }

    [[nodiscard]] bool isMarqueeSelected() const { return m_isMarqueeSelected; }
    void setMarqueeSelected(bool val) { m_isMarqueeSelected = val; }

    [[nodiscard]] double accumulatedAngleDeg() const { return m_accumulatedAngleDeg; }
    void setAccumulatedAngleDeg(double val) { m_accumulatedAngleDeg = val; }

    [[nodiscard]] const QHash<QUuid, MultiBlockBase>& multiBaseTf() const { return m_multiBaseTf; }
    [[nodiscard]] const std::vector<cad::param::Attachment>& multiReleasedAtts() const { return m_multiReleasedAtts; }

private:
    QSet<QUuid> m_selection;
    bool m_isMarqueeSelected = false;
    double m_accumulatedAngleDeg = 0.0;
    QHash<QUuid, MultiBlockBase> m_multiBaseTf;
    std::vector<cad::param::Attachment> m_multiReleasedAtts;
};

} // namespace cad::tools
