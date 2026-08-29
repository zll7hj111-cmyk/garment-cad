#pragma once

#include <QList>
#include <QUuid>

class QWidget;

namespace cad::param { class ParamDocument; }

namespace cad::doc {

/// Ask the user to confirm deleting @p blockIds, showing the aggregated
/// delete-impact report (连接 / 桥接线 / 交点 / 变量 / 公式 consequences)
/// when the deletion has any. Returns true when the deletion may proceed
/// (no impact at all, or the user confirmed).
///
/// Batch deletions are reported per block with INDEPENDENT reports — the
/// real cascade may merge (e.g. two deleted hosts pinning one bridge), so
/// the counts are an upper bound, never a lower one.
bool confirmDeleteImpact(QWidget* parent, const cad::param::ParamDocument* doc,
                         const QList<QUuid>& blockIds);

} // namespace cad::doc
