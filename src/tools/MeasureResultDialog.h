#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;

namespace cad::tools {

/// Modal result dialog shown right after the measure tool commits a
/// two-point measurement (QuickAuxDialog pattern: the dialog is pure
/// data-in / data-out and NEVER touches the document — the caller reads
/// the fields after exec() == Accepted and pushes the commands).
///
/// Content:
///   - Read-only readout of the measured distance (Units::formatLength).
///   - Read-only reference name (e.g. "M_A3KX9") — the formula identity.
///   - Optional name input (display label, no reference effect).
///   - Optional comment input.
///
/// Accepted  → caller writes the filled name/comment via SetMeasureCommand.
/// Rejected  → the measure is kept as committed; nothing else happens.
class MeasureResultDialog : public QDialog
{
    Q_OBJECT

public:
    /// @param valueMm  Measured distance in mm (internal unit).
    /// @param refName  Reserved reference name of the new measure variable.
    /// @param name     Pre-filled display name (usually empty).
    /// @param comment  Pre-filled comment (usually empty).
    MeasureResultDialog(double valueMm,
                        const QString& refName,
                        const QString& name = QString(),
                        const QString& comment = QString(),
                        QWidget* parent = nullptr);

    /// The user-typed display name (trimmed; empty = keep default).
    [[nodiscard]] QString enteredName() const;
    /// The user-typed comment (trimmed; empty = keep default).
    [[nodiscard]] QString enteredComment() const;

private:
    QLineEdit* m_nameEdit    = nullptr;
    QLineEdit* m_commentEdit = nullptr;
};

} // namespace cad::tools
