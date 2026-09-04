#pragma once

#include <QUuid>

namespace cad::tools {

/// Candidate leader segment for overlapping-target disambiguation
/// (ConfirmTarget state): a segment whose endpoint lies on the connection
/// spot. Clicking it confirms the leader point + reference segment.
/// Shared by ConnectGesture (state machine) and ConnectOverlapResolver
/// (candidate collection / highlight) — 避免头文件互相 include (阶段 3 拆分)。
struct ConfirmCandidate {
    QUuid blockId;
    QUuid segId;
    QUuid pointId;   ///< The endpoint ON the connection position.
};

} // namespace cad::tools
