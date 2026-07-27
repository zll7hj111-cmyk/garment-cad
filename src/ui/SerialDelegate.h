#pragma once

#include <QStyledItemDelegate>

namespace cad::ui {

/// Custom data role carrying the raw serial string for two-color rendering.
enum SerialDataRole { SerialRole = Qt::UserRole + 100 };

/// Item delegate that renders a readable serial (e.g. "a5sdfP1") as a gray
/// random prefix followed by a bold red type-tag ("P1"). Falls back to default
/// rendering when the item carries no SerialRole data.
class SerialDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SerialDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
};

} // namespace cad::ui
