#pragma once

#include "ElaDialog.h"
#include <QString>

#include "parametric/MeasureVariable.h"

class ElaLineEdit;

namespace cad::ui {

class NoteButton;

/// Modal result dialog shown right after the measure tool commits a
/// two-point measurement (QuickAuxDialog pattern: the dialog is pure
/// data-in / data-out and NEVER touches the document — the caller reads
/// the fields after exec() == Accepted and pushes the commands).
///
/// Content:
///   - Read-only readout of the measured distance (Units::formatLength).
///   - Optional reference-name input, initially EMPTY (the caller keeps its
///     auto-generated refName when the field is left empty; a typed value
///     replaces it — uppercase by convention).
///   - Optional name input (display label, no reference effect).
///   - Optional comment note button (NoteButton).
///
/// Accepted  → caller writes the filled refName/name/comment via
///             SetMeasureCommand.
/// Rejected  → the measure is kept as committed; nothing else happens.
class MeasureResultDialog : public ElaDialog
{
    Q_OBJECT

public:
    /// @param valueMm  Measured distance in mm (internal unit).
    /// @param refName  Reserved reference name of the new measure variable
    ///                 (used only when the user leaves the field empty).
    /// @param name     Pre-filled display name (usually empty).
    /// @param comment  Pre-filled comment (usually empty).
    /// @param kind     Measurement kind — drives the readout label
    ///                 (距离/水平/垂直 实测值).
    MeasureResultDialog(double valueMm,
                        const QString& refName,
                        const QString& name = QString(),
                        const QString& comment = QString(),
                        cad::param::MeasureKind kind = cad::param::MeasureKind::Distance,
                        QWidget* parent = nullptr);

    /// The user-typed reference name (trimmed/uppercased; empty = keep the
    /// auto-generated one).
    [[nodiscard]] QString enteredRefName() const;
    /// The user-typed display name (trimmed; empty = keep default).
    [[nodiscard]] QString enteredName() const;
    /// The user-typed comment (trimmed; empty = keep default).
    [[nodiscard]] QString enteredComment() const;

private:
    ElaLineEdit* m_refEdit  = nullptr;
    ElaLineEdit* m_nameEdit = nullptr;
    NoteButton*  m_noteBtn  = nullptr;
};

} // namespace cad::ui
