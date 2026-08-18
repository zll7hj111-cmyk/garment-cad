#pragma once

#include <QUuid>
#include <QHash>
#include <QSet>
#include <QString>
#include <QList>
#include <vector>
#include <utility>

#include "parametric/Condition.h"
#include "parametric/Group.h"

namespace cad::param {

class Block;
struct Attachment;
struct EvalContext;

/// One issue detected while resolving attachments. Produced only when the
/// caller passes a diagnostics vector to Resolver::resolveAll().
struct ResolveDiagnostic {
    enum class Kind {
        DanglingBlock,  ///< Attachment references a block that does not exist.
        DanglingPoint,  ///< Attachment references a point that is missing or unresolved.
        NotConverged,   ///< Iteration budget exhausted — conflicting or cyclic
                        ///< attachments prevent a stable placement.
    };
    Kind kind;
    QUuid attachmentId;  ///< Offending attachment (null for global issues).
};

/// Resolves the full dependency chain across all Blocks and Attachments.
/// Usage: call resolveAll() after any parameter change.
///
/// Layered resolution: the document is split into an auxiliary calculation
/// layer (a self-contained geometric calculator) and working layers. Value
/// dependencies are strictly one-directional (variables -> aux layer ->
/// published measurements -> working layers), so the two groups can be
/// resolved independently. The ONE geometric exception is the one-way
/// cross-layer attachment (aux follower → working leader): those followers
/// are settled by an extra AuxOnly pass AFTER the working layers
/// (ParamDocument Phase 3, 跨层沉降). ResolveScope narrows a pass to one
/// group; blocks outside the scope keep their current (cached) transforms
/// and serve as static references only.
class Resolver
{
public:
    /// Which layer group a resolve pass operates on.
    enum class Scope {
        All,         ///< Resolve every block (legacy / conservative path).
        AuxOnly,     ///< Only blocks on the auxiliary layer move.
        WorkingOnly, ///< Only blocks on working layers move; the aux layer is
                     ///< a frozen reference.
    };

    /// Resolve all blocks and propagate attachments.
    /// @param blocks      All blocks in the document.
    /// @param attachments All inter-block attachments.
    /// @param params      Variable name→value map (cm) for formula evaluation.
    /// @param conditioned formulaName→conditions for standalone-condition
    ///                    semantics (see ConditionEngine). May be empty.
    /// @param diagnostics Optional out-vector receiving issues found during
    ///                    resolution (cleared first; left untouched if null).
    /// @param scope       Layer group to resolve (see Scope).
    /// @param auxLayerId  Stable id (Layer::id) of the auxiliary calculation
    ///                    layer; only consulted when @p scope != All.
    /// @param affectedOnly Optional block-id subset. When non-null, ONLY blocks
    ///                    in the set may move; everything else is a static
    ///                    reference (same semantics as out-of-scope blocks).
    ///                    Null = full resolve (default). The set is treated as
    ///                    an intersection with the layer @p scope.
    /// @param components  Resolver-facing component snapshots (路线B). Active
    ///                    components (hasHinge) are rigid bodies driven by their
    ///                    single main hinge; ordinary attachments owned by their
    ///                    members are skipped so the component pose is the only
    ///                    authority for those blocks. Empty = legacy behaviour.
    static void resolveAll(std::vector<Block>& blocks,
                           const std::vector<Attachment>& attachments,
                           const QHash<QString, double>& params = {},
                           const QHash<QString, QList<Condition>>& conditioned = {},
                           std::vector<ResolveDiagnostic>* diagnostics = nullptr,
                           Scope scope = Scope::All,
                           const QUuid& auxLayerId = QUuid(),
                           const QSet<QUuid>* affectedOnly = nullptr,
                           const std::vector<Component>& components = {});

private:
    /// Process a single attachment: position and rotate the from-block
    /// so that its from-point coincides with the to-block's to-point.
    /// Returns true when the from-block's transform actually changed.
    /// Reports dangling points through @p diagnostics (deduplicated).
    /// @param preserveEndTargetRotation When true, a from-block that carries an
    ///        endpoint-aim constraint (endTarget) keeps its current rotation
    ///        (owned by the aim pass) and only has its position re-snapped.
    ///        Used by the post-aim re-settle so followers track an aimed leader
    ///        without fighting the aim-driven rotation.
    static bool applyAttachment(Block& from, const Attachment& att,
                                const Block& to,
                                const QHash<QString, double>& params,
                                const QHash<QString, QList<Condition>>& conditioned,
                                std::vector<ResolveDiagnostic>* diagnostics,
                                EvalContext* ctx,
                                bool preserveEndTargetRotation = false);
};

/// 滑轨投影快照 (用户拍板 2026-08): 把跟随线当前 from-point 的世界位置,
/// 投影到基准线在吸附点处的局部系 (x = 基准线延长方向, y = 垂直基准线),
/// 返回 {沿向偏移 slideAlongMm, 垂直偏移 slidePerpMm} (mm)。用于滑轨模式
/// (SlideMode) 激活 / 重定向时快照锁轴坐标。任一点缺失或未解析返回 {0,0}。
[[nodiscard]] std::pair<double, double> computeSlideOffsets(
    const Block& from, const Attachment& att, const Block& to);

} // namespace cad::param