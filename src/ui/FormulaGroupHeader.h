#pragma once

#include <QWidget>
#include <QUuid>
#include <QPoint>

class ElaText;
class ElaLineEdit;
class ElaToolButton;
class QTimer;

/// Collapsible section header for one formula group in the variable panel.
/// Layout: [caret] [name / inline rename edit] [(count)]  [dissolve ✕]
/// - Click toggles collapse, double-click on the name starts inline rename.
/// - Press + drag beyond the threshold starts a group-reorder drag.
/// - Accepts formula-card drops: dropping a card on the header moves it
///   to the end of this group (works while collapsed too).
class FormulaGroupHeader : public QWidget
{
    Q_OBJECT

public:
    explicit FormulaGroupHeader(const QUuid& groupId, const QString& name,
                                bool collapsed, int count,
                                QWidget* parent = nullptr);

    [[nodiscard]] QUuid groupId() const { return m_groupId; }

    void setName(const QString& name);
    void setCollapsed(bool collapsed);
    void setCount(int count);

    /// Open the inline rename editor (used right after group creation).
    void startRename();

    /// MIME type carried by a group drag (payload = group id string).
    static constexpr const char* kDragMimeType = "application/x-garmentcad-fgroup";

signals:
    void toggleRequested(const QUuid& groupId);            ///< Collapse/expand.
    void renameRequested(const QUuid& groupId, const QString& newName);
    void dissolveRequested(const QUuid& groupId);
    /// A formula card was dropped onto the header (append to this group).
    void formulaDropped(const QUuid& formulaId, const QUuid& groupId);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void updateCaret();
    void commitRename();
    void setDropHighlight(bool on);

    QUuid m_groupId;
    bool  m_collapsed = false;
    bool  m_dragging = false;    ///< True once a drag actually started.
    bool  m_dropping = false;    ///< Drop-target highlight (QSS attribute).
    QPoint m_pressPos;
    bool  m_pressed = false;

    ElaText*     m_caret = nullptr;
    ElaText*     m_nameLabel = nullptr;
    ElaLineEdit*   m_nameEdit = nullptr;
    ElaText*     m_countLabel = nullptr;
    ElaToolButton* m_dissolveBtn = nullptr;
};
