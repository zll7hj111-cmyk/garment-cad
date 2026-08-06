#pragma once

#include <QUuid>
#include <QString>

namespace cad::param {

/// A USER-authored group (成组): a pure SELECTION convenience (选择快捷方式,
/// 2026-08-04 设计定稿) — membership is explicit (written into ParamDocument's
/// block-group map by createGroup), NOT derived from the attachment graph.
/// The group places ZERO constraints on its members: connections attach/
/// detach freely, members drag/delete individually (删除后成员不足 2 时组自动
/// 解散), and grouping never alters geometry (no severing, no aim clearing).
/// Its only role: whole-group selection via badge click / toggle (整组进整组出).
/// The group carries a stable identity (hidden internal UUID + human-readable
/// serial) and a user-editable name.
struct Group {
    QUuid   id = QUuid::createUuid();  ///< Internal stable identifier (not shown).
    QString serial;  ///< Human-readable ID, e.g. "m3p7qG1" (assigned by ParamDocument).
    QString name;    ///< User-editable group name (default empty).
};

} // namespace cad::param
