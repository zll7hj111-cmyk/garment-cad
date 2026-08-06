#pragma once

#include <QString>
#include <QStringList>

namespace cad::param { class ParamDocument; }

namespace cad::doc {

/// Reads/writes .gcad files (ZIP archives containing JSON).
///
/// ZIP internal structure:
///   manifest.json   — format version, app version, timestamps
///   document.json   — blocks, attachments, groups, free points, params, serials
///   variables.json  — variables + formulas + conditions
namespace DocumentFile {

/// File extension for garment CAD documents.
inline constexpr const char* kExtension = ".gcad";
/// File filter string for QFileDialog.
inline constexpr const char* kFilter = "Garment CAD (*.gcad)";

/// Save the document to a .gcad file (ZIP + JSON).
/// Writes to a temp file first, then renames (crash-safe).
/// @return true on success; on failure *error contains a message.
bool save(const QString& path, const cad::param::ParamDocument& doc,
          QString* error = nullptr);

/// Load a .gcad file into the document (clears existing content first).
/// @return true on success; on failure *error contains a message.
/// @param warnings Optional out-list receiving degradation notices from the
///        deserializer (unknown constraint / segment type / layer type were
///        replaced by safe defaults). Non-empty still means SUCCESS — the
///        document loaded, just with some values downgraded. Null = ignore.
bool load(const QString& path, cad::param::ParamDocument& doc,
          QString* error = nullptr, QStringList* warnings = nullptr);

} // namespace DocumentFile
} // namespace cad::doc
