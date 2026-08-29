#pragma once

/// Narrow domain view over the ParamDocument attachment domain (B2,
/// 门面按域分组 — see BlockView.h for the pattern established in B1).
///
/// Contract (mirrors the facade — nothing new, nothing bypassed):
///   * READ-ONLY. Structural writes (addAttachment / removeAttachment /
///     setAttachment* / slide-mode updates) stay on the facade: they carry
///     the forest-invariant + cross-layer validation and emit signals.
///   * The MUTABLE findAttachment overload stays facade-only as well — it is
///     the single authorized in-place edit channel (e.g. welding a follower
///     angle during an aim-release back-solve) and must stay greppable as an
///     edit, not look like a read.
///   * Stateless — holds a `const ParamDocument*`, always reflects the live
///     document.
///
/// Usage:  for (const Attachment& a : doc.attachmentsView().all()) { ... }
///         if (const Attachment* a = doc.attachmentsView().byId(id)) { ... }
///         QSet<QUuid> welded = doc.attachmentsView().lockedClosure(seed);

#include <QSet>
#include <QUuid>
#include <vector>

#include "parametric/Attachment.h"
#include "parametric/ParamDocument.h"

namespace cad::param {

class AttachmentsView
{
public:
    explicit AttachmentsView(const ParamDocument& doc) noexcept
        : m_doc(&doc) {}

    /// All attachments in document order (read-only container view).
    [[nodiscard]] const std::vector<Attachment>& all() const { return m_doc->attachments(); }

    /// O(1)-ish lookup by id; nullptr when absent (read-only overload).
    [[nodiscard]] const Attachment* byId(const QUuid& id) const { return m_doc->findAttachment(id); }

    /// Expand a seed set to the welded closure (拖动保护递归焊接):
    /// dragging any member moves the whole closure.
    [[nodiscard]] QSet<QUuid> lockedClosure(const QSet<QUuid>& seed) const
    { return m_doc->lockedClosure(seed); }

    /// Bridge blocks pinned TO @p hostBlockId (deleting the host releases
    /// them — 删除影响报告的桥接分支).
    [[nodiscard]] std::vector<QUuid> bridgesPinnedTo(const QUuid& hostBlockId) const
    { return m_doc->bridgesPinnedTo(hostBlockId); }

private:
    const ParamDocument* m_doc;
};

/// Facade accessor — defined here so ParamDocument.h only carries the
/// forward declaration (keeps the facade header free of the view body).
inline AttachmentsView ParamDocument::attachmentsView() const noexcept
{
    return AttachmentsView(*this);
}

} // namespace cad::param
