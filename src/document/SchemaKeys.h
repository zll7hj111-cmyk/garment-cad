#pragma once

#include <QString>

/// JSON schema key names shared by the document serializer, the file wrapper
/// and the version migrators (2026-12 审计 P1-7).
///
/// Why a header: FormatMigration writes the v0/v1 JSON shapes that
/// DocumentSerializer::fromJson reads. A key that drifts between the two is a
/// SILENT failure — the file still parses, the field is simply ignored (and a
/// migration that emits unreadable JSON produces a doc with no layers/blocks).
/// The keys below are exactly the migration-critical surface: the document
/// envelope, the layer records and the block→layer reference.
///
/// Per-entity record keys that are written AND read inside one translation
/// unit (point/segment/variable/component records) intentionally stay literal:
/// they cannot drift across the migration boundary, and replacing them would
/// only add noise.
// Namespace cad::schema (not cad::doc::schema): DocumentSerializer lives in
// cad::param, FormatMigration/DocumentFile in cad::doc. Both reach it as
// `schema::kX` through enclosing-namespace lookup.
namespace cad::schema {

// ── Document envelope ──
inline const QString kDocument    = QStringLiteral("document");
inline const QString kVariables   = QStringLiteral("variables");
inline const QString kBlocks      = QStringLiteral("blocks");
inline const QString kLayers      = QStringLiteral("layers");
inline const QString kActiveLayer = QStringLiteral("activeLayer");
inline const QString kAttachments = QStringLiteral("attachments");

// ── Layer records + the block→layer reference (rewritten by migrateV0ToV1) ──
inline const QString kId      = QStringLiteral("id");
inline const QString kName    = QStringLiteral("name");
inline const QString kVisible = QStringLiteral("visible");
inline const QString kType    = QStringLiteral("type");
inline const QString kLayer   = QStringLiteral("layer");

// ── Layer type VALUES (part of the schema too: "working"/"auxiliary") ──
inline const QString kTypeWorking   = QStringLiteral("working");
inline const QString kTypeAuxiliary = QStringLiteral("auxiliary");

} // namespace cad::schema
