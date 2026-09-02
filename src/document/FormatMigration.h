#pragma once

// ---------------------------------------------------------------------------
// On-disk format versioning (ARCHITECTURE_REVIEW P2-1).
//
// Before this module existed the .gcad loader had exactly two states: "the
// version number is bigger than what we know" (hard reject) and "everything
// else" (load anyway). Every historical shape was handled by field sniffing
// scattered through DocumentSerializer::deserialize — the reader had to know
// that "a missing layers array means the file predates the auxiliary layer"
// and that "an integer block layer means indices shift by one". That knowledge
// was invisible in the version number and untestable in isolation.
//
// This module makes history explicit: a file declares version N, and N is
// walked forward to kFormatVersion by a chain of registered single-step
// migrations. Each step is a pure QJsonObject -> QJsonObject function, so it
// can be unit-tested without a document, a canvas, or a .gcad archive.
//
// Adding a v2 format
// ------------------
//   1. bump kFormatVersion below;
//   2. write `migrateV1ToV2(QJsonObject, QStringList*)` in FormatMigration.cpp;
//   3. append `{1, "your-step-name", &migrateV1ToV2}` to registry().
// Nothing else changes — apply() walks the chain and refuses to load if a
// link is missing (a gap means the reader cannot be trusted to interpret the
// bytes, which is exactly when silent corruption used to happen).
// ---------------------------------------------------------------------------

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace cad::doc {

/// The version DocumentFile writes, and the newest one it can read.
constexpr int kFormatVersion = 2;

/// One link in the chain: rewrites a vN document in place into a vN+1 one.
/// @p warnings collects human-readable degradation notes (same contract as
/// DocumentSerializer::deserialize).
using MigrationFn = QJsonObject (*)(QJsonObject root, QStringList* warnings);

struct MigrationStep {
    int         from = 0;         ///< Upgrades FROM this version (to from+1).
    const char* name = nullptr;   ///< Stable label — appears in diagnostics.
    MigrationFn fn   = nullptr;
};

class FormatMigration
{
public:
    /// Append a step to the chain. Steps may be registered in any order;
    /// apply() looks them up by `from`.
    static void registerStep(const MigrationStep& step);

    /// The registered chain (ordered by `from`). Exposed for diagnostics.
    static std::vector<MigrationStep> steps();

    /// Walk @p root from version @p fromVersion up to kFormatVersion.
    ///
    /// Returns false and fills @p error when the chain has a gap or
    /// @p fromVersion is newer than kFormatVersion — in both cases the bytes
    /// cannot be interpreted safely, so the caller must refuse to load.
    /// A version equal to kFormatVersion is returned untouched (no copies).
    static bool migrate(int fromVersion, QJsonObject& root,
                        QStringList* warnings, QString* error);

    // ── Individual steps (public so tests can exercise one link alone) ─────

    /// v0 → v1, "layer-refs": v0 predates the auxiliary calculation layer and
    /// stable layer ids. Blocks referenced their layer by INTEGER INDEX into a
    /// working-layer-only array. v1 inserts the auxiliary layer at index 0 and
    /// gives every layer a stable QUuid, so every integer reference has to be
    /// resolved to an id string once — here, at load time, instead of being
    /// guessed by the serializer on every read.
    static QJsonObject migrateV0ToV1(QJsonObject root, QStringList* warnings);

    /// v1 → v2, "shadow-anchor": v1 stored the shadow as a cumulative offset
    /// (baselineOffsetDeg, degrees). The shadow-deflection feature and its
    /// anchor rewrite (shadowAnchorRotDeg) were removed since v2, so this step
    /// now only drops the dead legacy key from each attachment. The step name
    /// is kept stable — it is a registered link in the migration chain.
    static QJsonObject migrateV1ToV2(QJsonObject root, QStringList* warnings);
};

} // namespace cad::doc
