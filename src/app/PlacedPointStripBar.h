#pragma once

#include <QWidget>
#include <QUuid>

class ElaText;
class ElaLineEdit;
class ElaPushButton;
class QUndoStack;

namespace cad::param {
class ParamDocument;
}

namespace cad::app {

/// 放置点专属底部属性条控件 (从 ContextStrip 剥离以解耦放置点生命周期与状态爆炸)。
class PlacedPointStripBar : public QWidget
{
    Q_OBJECT

public:
    explicit PlacedPointStripBar(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setDoc(cad::param::ParamDocument* doc) { m_paramDoc = doc; }
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }

    /// 选中放置点: 条带填入目标放置点数据
    bool setTarget(const QUuid& blockId, const QUuid& pointId);
    void clearTarget();

    /// 激活放置点手势会话
    void beginSession(const QString& baseSegName = QString());
    void updateValues(double distCm, double angleDeg, bool distLocked, bool angleLocked);
    void endSession();
    void focusNextField();

    [[nodiscard]] bool isSession() const { return m_placePointSession; }
    [[nodiscard]] bool hasTarget() const { return !m_placedBlockId.isNull() && !m_placedPointId.isNull(); }
    [[nodiscard]] bool isPlacedPointMode() const { return m_isPlacedPointMode; }
    [[nodiscard]] bool hasInputFocus() const;

    [[nodiscard]] QUuid blockId() const { return m_placedBlockId; }
    [[nodiscard]] QUuid pointId() const { return m_placedPointId; }

    void applyTheme();

signals:
    void distChanged(double distCm, bool locked);
    void angleChanged(double angleDeg, bool locked);
    void committed();
    void deleted(const QUuid& blockId, const QUuid& pointId);
    void cancelRequested();
    void returnFocusRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildUi();
    void applyPlacedPointEdits();
    void onDeletePlacedPointClicked();
    void onPlacedPointDistEdited(const QString& text);
    void onPlacedPointAngleEdited(const QString& text);

    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUndoStack*                m_undoStack = nullptr;

    ElaText*       m_ptSerialLabel = nullptr;
    ElaLineEdit*   m_ptNameEdit = nullptr;
    ElaLineEdit*   m_ptDistEdit = nullptr;
    ElaLineEdit*   m_ptAngleEdit = nullptr;
    ElaText*       m_ptBaseSegLabel = nullptr;
    ElaPushButton* m_btnDeletePlacedPt = nullptr;
    ElaText*       m_ptHint = nullptr;

    bool           m_isPlacedPointMode = false;
    bool           m_placePointSession = false;
    QUuid          m_placedBlockId;
    QUuid          m_placedPointId;
};

} // namespace cad::app
