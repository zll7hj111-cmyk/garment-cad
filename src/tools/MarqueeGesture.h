#pragma once

#include <QRectF>
#include <QSet>
#include <QUuid>

#include "geometry/Vec2.h"

class QGraphicsRectItem;

class CanvasScene;

namespace cad::param { class ParamDocument; }

namespace cad::tools {

/// Rubber-band marquee gesture of the selection tool (empty-space drag).
/// Owns the dashed rect item and the block hit test; the owning tool applies
/// the returned toggle result to its selection. Follows the ConnectGesture
/// extraction pattern: the gesture computes, the tool applies.
class MarqueeGesture
{
public:
    MarqueeGesture(CanvasScene* scene, cad::param::ParamDocument* doc);
    ~MarqueeGesture();

    /// Start the marquee at @p pos; @p baseSelection is the pre-gesture
    /// selection snapshot (toggle semantics: intersecting blocks flip
    /// membership relative to the base).
    void begin(const cad::geo::Vec2& pos, const QSet<QUuid>& baseSelection);
    /// Update the rect + live preview of the prospective toggle result
    /// (BlockItem selected/locked visuals). No-op while inactive.
    void update(const cad::geo::Vec2& pos);
    /// Finish: apply base XOR hits (group-expanded), remove the rect and
    /// return the resulting selection.
    QSet<QUuid> end(const cad::geo::Vec2& pos);
    /// Remove the rect without touching the selection (idempotent).
    void cancel();

    [[nodiscard]] bool active() const { return m_item != nullptr; }

    /// Group expansion (group = minimal selection unit) — shared by the
    /// selection tool (selectBlocksExternally).
    static QSet<QUuid> expandWithGroups(cad::param::ParamDocument* doc,
                                        const QSet<QUuid>& ids);

private:
    /// Blocks whose segments intersect @p rectUser (active layer only).
    [[nodiscard]] QSet<QUuid> hitsIn(const QRectF& rectUser) const;
    /// Base XOR intersecting blocks (toggle semantics, NOT group-expanded).
    [[nodiscard]] QSet<QUuid> toggleOf(const QRectF& rectUser) const;
    /// Sync the marquee's live preview visuals for @p prospective.
    void syncPreview(const QSet<QUuid>& prospective);

    CanvasScene* m_scene = nullptr;
    cad::param::ParamDocument* m_doc = nullptr;
    QGraphicsRectItem* m_item = nullptr;  ///< Dashed rect (created on begin).
    cad::geo::Vec2 m_start;               ///< Drag start (user coords).
    QSet<QUuid> m_base;                   ///< Selection snapshot at begin (toggle).
};

} // namespace cad::tools
