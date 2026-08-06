#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>

namespace cad::param {

class ParamDocument;

/// Serializes / deserializes a ParamDocument to/from JSON.
/// Used by DocumentFile (ZIP I/O) and unit tests.
namespace DocumentSerializer {

/// Serialize the full document state into a JSON object.
/// The result contains "document" and "variables" sub-objects.
[[nodiscard]] QJsonObject serialize(const ParamDocument& doc);

/// Restore document state from a JSON object produced by serialize().
/// Clears the document first, then rebuilds all entities.
/// @param warnings Optional out-list receiving degradation notices: values
///        that could not be interpreted (unknown constraint / segment type /
///        role / line style / layer type) were silently replaced by safe
///        defaults. Null = ignore. A non-empty list still means the load
///        SUCCEEDED — the document is valid, just degraded.
void deserialize(ParamDocument& doc, const QJsonObject& root,
                 QStringList* warnings = nullptr);

} // namespace DocumentSerializer
} // namespace cad::param
