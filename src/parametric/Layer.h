#pragma once

#include <QString>
#include <QUuid>

namespace cad::param {

/// Discriminates the two layer kinds.
///
/// Auxiliary (辅助计算层): a geometric "scratch calculator" — the user draws
/// construction geometry (lines, intersections, attachments) and publishes
/// measured distances as parameters. Exactly ONE auxiliary layer exists per
/// document (index 0, created automatically, cannot be removed). When it is
/// not the active layer it renders GRAYED like any non-active layer (its
/// draft stays visible as a reference) but is NOT snappable — working layers
/// can never reference its points or attach to its blocks; its only export
/// is the scalar measurement values published from it.
///
/// Working: normal pattern-making layers (existing behaviour: non-active
/// working layers render grayed and stay snappable as a reference).
enum class LayerType : quint8 {
    Working,
    Auxiliary,
};

/// A canvas layer — a selection/visibility filter with a type distinction.
///
/// Layers do NOT affect the solver's correctness: every block is still
/// resolved (though the dirty-marking pipeline may skip layers whose inputs
/// have not changed). A layer controls (a) whether its blocks are
/// selectable/hoverable (only the active layer is) and (b) whether they are
/// painted. Non-active visible layers are rendered grayed as a reference;
/// the non-active AUXILIARY layer is grayed too, but never a snap target
/// (see ParamDocument::layerSnappable).
/// Blocks reference their layer by the STABLE Layer::id (never by display
/// row) — removal/reordering of other layers never invalidates the reference.
struct Layer {
    QUuid     id = QUuid::createUuid();  ///< Stable identity (Block::layer ref).
    QString   name;        ///< User-editable display name.
    bool      visible = true;  ///< False = hidden (not painted, not snappable).
    LayerType type = LayerType::Working;  ///< Layer kind (single Auxiliary per doc).
};

} // namespace cad::param
