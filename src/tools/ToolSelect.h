#pragma once

#include <QUuid>
#include <QList>
#include <QHash>

#include "Tool.h"
#include "geometry/Vec2.h"

namespace cad::tools {

/// Selection tool: click to select entities, drag to move the whole attachment
/// group as a rigid body, double-click a segment to edit its properties,
/// Del to delete selection.
class ToolSelect : public Tool
{
public:
    void activate(CanvasScene& scene, cad::param::ParamDocument* paramDoc) override;
    void deactivate() override;

    void mousePress(QGraphicsSceneMouseEvent* event) override;
    void mouseMove(QGraphicsSceneMouseEvent* event) override;
    void mouseRelease(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClick(QGraphicsSceneMouseEvent* event) override;

    void keyPress(QKeyEvent* event) override;

    [[nodiscard]] const char* name() const override { return "\xe9\x80\x89\xe6\x8b\xa9"; }

private:
    void deleteSelectedBlocks();

    /// Collect the attachment group (connected component of the attachment
    /// graph, traversing both directions) containing the given block.
    [[nodiscard]] QList<QUuid> collectGroup(const QUuid& seedBlockId) const;

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_paramDoc = nullptr;

    // Group-drag state
    cad::geo::Vec2 m_dragStartPos;                 ///< Cursor position at drag start (user coords).
    QList<QUuid>   m_dragGroup;                    ///< Block IDs in the dragged group.
    QHash<QUuid, cad::geo::Vec2> m_dragOrigins;    ///< Original origin of each group block.
};

} // namespace cad::tools
