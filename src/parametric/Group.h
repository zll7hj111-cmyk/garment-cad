#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

/// A first-class attachment group. Membership is derived from the attachment
/// graph (a connected component of >= 2 blocks); the group itself only carries
/// a stable identity (hidden internal UUID + human-readable serial) and a
/// user-editable name.
struct Group {
    QUuid   id = QUuid::createUuid();  ///< Internal stable identifier (not shown).
    QString serial;  ///< Human-readable ID, e.g. "m3p7qG1" (assigned by ParamDocument).
    QString name;    ///< User-editable group name (default empty).
};

} // namespace cad::param
