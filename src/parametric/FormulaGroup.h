#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

/// A single-level named group for formula variables in the variable panel.
/// Membership is recorded on each FormulaVariable via its groupId field;
/// the group itself only carries identity, a user-editable name and the
/// collapsed state (persisted with the document).
struct FormulaGroup {
    QUuid   id = QUuid::createUuid();
    QString name;              ///< User-editable group name.
    bool    collapsed = false; ///< Whether the group is folded in the panel.
};

} // namespace cad::param
