#pragma once

#include <QString>

namespace cad::param {

/// Utilities for human-readable entity serials.
///
/// A serial has the form `[5 random chars][type letter][sequence]`, e.g.
/// "a5sdfP1" (point), "k9x2bL3" (line/segment), "m3p7qG2" (group).
/// The 5-char random prefix guarantees global uniqueness; the letter+sequence
/// part is the human-friendly identifier shown in red, while the prefix is
/// rendered gray.
namespace Serial {

/// Generate a random lowercase-alphanumeric prefix (default 5 chars).
[[nodiscard]] QString randomPrefix(int len = 5);

/// Compose a full serial from a prefix, type letter and sequence number.
[[nodiscard]] QString make(const QString& prefix, QChar letter, int seq);

/// The 5-char random prefix (everything before the type letter).
[[nodiscard]] QString prefix(const QString& serial);

/// The human-friendly tag (type letter + sequence), e.g. "P1".
[[nodiscard]] QString tag(const QString& serial);

/// Rich-text HTML rendering: gray prefix + red tag. Suitable for QLabel.
[[nodiscard]] QString toHtml(const QString& serial);

} // namespace Serial

} // namespace cad::param
