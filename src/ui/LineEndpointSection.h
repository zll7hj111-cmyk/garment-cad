#pragma once

#include <QWidget>
#include <QUuid>
#include <QString>

class ElaLineEdit;
class ElaText;
class QPushButton;

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
}

class CanvasScene;

namespace cad::ui {

class NoteButton;
class PointRefEdit;

/// Endpoint micro-cards section for LinePropertyDialog (D1 / §6.1 / §6.2).
/// Manages:
///   - Top/bottom physical slot point badges (P1 / P2)
///   - Endpoint names, note buttons, and show-name toggle chips
///   - Endpoint extend edits (extendStart / extendEnd in cm) and validation
///   - In-slot connection (connect-to / detach / connection summary badge)
///   - Middle direction axis and reverse-direction arrow button (↓ / ↑)
class LineEndpointSection : public QWidget
{
    Q_OBJECT

public:
    explicit LineEndpointSection(cad::param::ParamDocument* paramDoc,
                                CanvasScene* scene,
                                QWidget* parent = nullptr);
    ~LineEndpointSection() override = default;

    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    void populateFromModel(const cad::param::Block& block, const cad::param::Segment& seg);
    void applyToModel(cad::param::Block* block, cad::param::Segment* seg);

    [[nodiscard]] QString startName() const;
    [[nodiscard]] QString endName() const;
    [[nodiscard]] QString startAnnotation() const;
    [[nodiscard]] QString endAnnotation() const;
    [[nodiscard]] bool startShowName() const;
    [[nodiscard]] bool endShowName() const;

    void refreshEndpointConnRows();
    void refreshEndpointExtends();
    void refreshDirectionArrow();

    [[nodiscard]] ElaLineEdit* editStartName() const { return m_editStartName; }
    [[nodiscard]] ElaLineEdit* editEndName() const { return m_editEndName; }
    [[nodiscard]] QPushButton* chkShowStartName() const { return m_chkShowStartName; }
    [[nodiscard]] QPushButton* chkShowEndName() const { return m_chkShowEndName; }
    [[nodiscard]] ElaLineEdit* editStartExtend() const { return m_editStartExtend; }
    [[nodiscard]] ElaLineEdit* editEndExtend() const { return m_editEndExtend; }
    [[nodiscard]] QPushButton* btnDirectionArrow() const { return m_btnDirectionArrow; }

    static QUuid fixedTopPointId(const cad::param::Block* block,
                                 const cad::param::Segment* seg);

signals:
    void liveUpdated();
    void directionArrowClicked();
    void connectionChanged();

private slots:
    void onStartNoteEdited(const QString& text);
    void onEndNoteEdited(const QString& text);
    void onStartExtendEdited();
    void onEndExtendEdited();
    void onStartConnectResolved(const QUuid& blockId, const QUuid& pointId);
    void onStartDetachClicked();
    void onEndConnectResolved(const QUuid& blockId, const QUuid& pointId);
    void onEndDetachClicked();
    void onDirectionArrowClickedInternal();

private:
    QWidget* buildEndpoint(bool isStart);
    void applyEndpointExtend(ElaLineEdit* edit, bool isTop);
    QString endpointExtendDisableReason(const cad::param::Block& block,
                                        const cad::param::Segment& seg,
                                        const QUuid& pointId) const;

    cad::param::ParamDocument* m_paramDoc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;
    bool m_topIsStart = true;

    // Start (top slot) widgets
    ElaText* m_lblStartPtId = nullptr;
    ElaLineEdit* m_editStartName = nullptr;
    NoteButton* m_noteStart = nullptr;
    QPushButton* m_chkShowStartName = nullptr;
    ElaLineEdit* m_editStartExtend = nullptr;
    PointRefEdit* m_refStartConnect = nullptr;
    QPushButton* m_btnStartDetach = nullptr;
    ElaText* m_lblStartConn = nullptr;

    // Middle axis & direction arrow
    QPushButton* m_btnDirectionArrow = nullptr;

    // End (bottom slot) widgets
    ElaText* m_lblEndPtId = nullptr;
    ElaLineEdit* m_editEndName = nullptr;
    NoteButton* m_noteEnd = nullptr;
    QPushButton* m_chkShowEndName = nullptr;
    ElaLineEdit* m_editEndExtend = nullptr;
    PointRefEdit* m_refEndConnect = nullptr;
    QPushButton* m_btnEndDetach = nullptr;
    ElaText* m_lblEndConn = nullptr;
};

} // namespace cad::ui
