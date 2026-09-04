#pragma once

#include <QFrame>
#include <QToolButton>
#include <QUuid>
#include <QList>
#include <functional>

class ElaText;
class ElaLineEdit;
class QVBoxLayout;

namespace cad::param { struct Segment; }

namespace cad::ui {

// ---------------------------------------------------------------------------
// Eye toggle button (visibility switch with theme-aware icons)
// ---------------------------------------------------------------------------
class EyeToggle : public QToolButton
{
    Q_OBJECT
public:
    explicit EyeToggle(QWidget* parent = nullptr, int size = 20);
    void applyTheme();

private:
    void syncIcon();
    int m_size = 20;
};

// ---------------------------------------------------------------------------
// Segment row widget inside a layer card
// ---------------------------------------------------------------------------
class SegmentRow : public QWidget
{
    Q_OBJECT
public:
    SegmentRow(const QUuid& blockId, const cad::param::Segment& seg,
               bool layerHidden, QWidget* parent = nullptr);

    [[nodiscard]] QUuid blockId() const { return m_blockId; }
    [[nodiscard]] EyeToggle* eye() const { return m_eye; }
    [[nodiscard]] QToolButton* moreBtn() const { return m_moreBtn; }

    void setRowInfo(const QString& name, bool visible);
    void updateNameVisual(const QString& name, bool visible);

protected:
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QUuid m_blockId;
    EyeToggle* m_eye = nullptr;
    ElaText* m_nameLbl = nullptr;
    ElaText* m_tagLbl = nullptr;
    QToolButton* m_moreBtn = nullptr;
};

// ---------------------------------------------------------------------------
// Layer card: technical border + left accent indicator + header + collapsible segment list
// ---------------------------------------------------------------------------
class LayerCard : public QFrame
{
    Q_OBJECT
public:
    struct SegmentInfo {
        QUuid blockId;
        QString name;
        QString serial;
        bool visible;
    };

    LayerCard(int index, const QUuid& layerId, const QString& name, bool visible,
              bool isActive, int segCount, bool isAux, QWidget* parent = nullptr);

    void setOnRenameCommitted(std::function<void(int, const QString&)> cb) { m_onRenameCommitted = std::move(cb); }
    void setOnMenuRequested(std::function<void(const QPoint&)> cb) { m_onMenuRequested = std::move(cb); }

    void startRename();
    void addSegmentRow(SegmentRow* row);
    void setLayerName(const QString& name);
    void setLayerVisible(bool visible);
    void setSegCount(int n);

    [[nodiscard]] SegmentRow* findSegmentRow(const QUuid& blockId) const;
    [[nodiscard]] QList<SegmentRow*> segmentRows() const;
    void removeSegmentRow(const QUuid& blockId);

    [[nodiscard]] int layerIndex() const { return m_index; }
    [[nodiscard]] QUuid layerId() const { return m_layerId; }
    [[nodiscard]] EyeToggle* eye() const { return m_eye; }
    [[nodiscard]] ElaText* nameLabel() const { return m_nameLabel; }
    [[nodiscard]] QWidget* headerWidget() const { return m_headerWidget; }
    [[nodiscard]] QToolButton* collapseBtn() const { return m_collapseBtn; }
    [[nodiscard]] QToolButton* menuBtn() const { return m_menuBtn; }
    [[nodiscard]] QWidget* segList() const { return m_segList; }

    void setCollapsed(bool collapsed);
    void applyTheme();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void applyCardStyle();
    void updateNameStyle();
    void updateEmptyRowVisibility();

    int m_index;
    QUuid m_layerId;
    bool m_isActive;
    bool m_isAux;
    bool m_visible;
    QToolButton* m_collapseBtn = nullptr;
    EyeToggle* m_eye = nullptr;
    ElaText* m_nameLabel = nullptr;
    ElaLineEdit* m_nameEdit = nullptr;
    ElaText* m_auxBadge = nullptr;
    ElaText* m_activeBadge = nullptr;
    ElaText* m_countPill = nullptr;
    QToolButton* m_menuBtn = nullptr;
    QWidget* m_headerWidget = nullptr;
    QFrame* m_innerSep = nullptr;
    QWidget* m_segList = nullptr;
    QVBoxLayout* m_segListLayout = nullptr;
    ElaText* m_emptyRow = nullptr;
    std::function<void(int, const QString&)> m_onRenameCommitted;
    std::function<void(const QPoint&)> m_onMenuRequested;
};

} // namespace cad::ui
