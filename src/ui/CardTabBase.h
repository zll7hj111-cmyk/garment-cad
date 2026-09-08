#pragma once

#include <QWidget>
#include <QString>
#include <functional>

class ElaScrollArea;
class VirtualCardList;
class QUndoStack;
class ElaText;
class QPushButton;

namespace cad::param {
class ParamDocument;
}

namespace cad::ui {

/// Base class for card list tab pages in VariablePanel (VariableTab, FormulaTab, LinkedTab).
/// Encapsulates the recessed scroll area, empty state scaffolding, theme re-styling,
/// and virtual card list lifecycle.
class CardTabBase : public QWidget
{
    Q_OBJECT

public:
    explicit CardTabBase(cad::param::ParamDocument* doc, QWidget* parent = nullptr);
    ~CardTabBase() override = default;

    virtual void setUndoStack(QUndoStack* stack);
    virtual void applyTheme();

    void setEmptyHintVisible(bool visible);

protected:
    void setupListPage(const QString& emptyTitle, const QString& emptyGuide,
                       const QString& ghostAddText = QString(),
                       const std::function<void()>& onGhostAdd = nullptr);

    cad::param::ParamDocument* m_doc = nullptr;
    QUndoStack* m_undoStack = nullptr;

    ElaScrollArea*   m_scroll = nullptr;
    QWidget*         m_container = nullptr;
    VirtualCardList* m_host = nullptr;
    QWidget*         m_emptyBox = nullptr;
    ElaText*         m_emptyTitle = nullptr;
    ElaText*         m_emptyGuide = nullptr;
    QPushButton*     m_emptyGhost = nullptr;
};

} // namespace cad::ui
