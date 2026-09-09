#pragma once

#include <QWidget>
#include <QUuid>
#include <QString>

class ElaLineEdit;
class ElaText;
class QPushButton;
class CanvasScene;   // global namespace (canvas 层不在 cad::ui 里)

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
}
namespace cad::ui {

/// 圆拟合段 (Segment::fitKind == FitKind::Circle) 的专属几何区。
///
/// 为什么单独一个区而不复用 LineGeometrySection:
///  - 圆的唯一尺寸权威是**起点 Polar 的 distance** (D2), 而线段区的「长度」
///    写的是终点 distance; 对圆写终点距离会被 syncCircleFitRadius 覆盖, 静默失效。
///  - 圆的「角度」是**基准角 a₀** (写起点 angle, D18), 而线段区的角度卡写的是
///    终点角度; 对圆那是包角, 语义相反 (写 360° 会被当成整圆, 写 0° 会退化)。
///  - 圆度复用 Segment::tension (D17), 但线段区的张力框标签与取值范围都不同。
class CircleGeometrySection : public QWidget
{
    Q_OBJECT

public:
    CircleGeometrySection(cad::param::ParamDocument* paramDoc,
                          CanvasScene* scene,
                          QWidget* parent = nullptr);

    void setTarget(const QUuid& blockId, const QUuid& segmentId);

    void populateFromModel(const cad::param::Block& block,
                           const cad::param::Segment& seg);
    void applyToModel(cad::param::Block* block, cad::param::Segment* seg);

    /// 场景重解算后刷新只读派生量 (弧长/弦长) 与公式生效后的 D/C 回读。
    void refreshDerived();

    // 测试用访问器
    [[nodiscard]] ElaLineEdit* editRadius() const { return m_editRadius; }
    [[nodiscard]] ElaLineEdit* editDiameter() const { return m_editDiameter; }
    [[nodiscard]] ElaLineEdit* editCircumference() const { return m_editCircumference; }
    [[nodiscard]] ElaLineEdit* editBaseAngle() const { return m_editBaseAngle; }
    [[nodiscard]] ElaLineEdit* editSweep() const { return m_editSweep; }
    [[nodiscard]] ElaLineEdit* editRoundness() const { return m_editRoundness; }
    [[nodiscard]] QPushButton* detachButton() const { return m_btnDetach; }
    [[nodiscard]] QPushButton* publishButton() const { return m_btnPublish; }

signals:
    void liveUpdated();
    void sceneRefreshRequested();
    /// D14 解除圆约束已执行：fitKind 不再是 Circle，属性对话框须重填以切回线段区。
    void detachRequested();

private:
    void buildRows();
    void fill(bool force);
    void onDiameterEdited();
    void onCircumferenceEdited();
    void onPublishLength();
    void onDetach();

    cad::param::ParamDocument* m_paramDoc = nullptr;
    CanvasScene* m_scene = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    ElaLineEdit* m_editRadius        = nullptr;
    ElaLineEdit* m_editDiameter      = nullptr;
    ElaLineEdit* m_editCircumference = nullptr;
    ElaLineEdit* m_editBaseAngle     = nullptr;
    ElaLineEdit* m_editSweep         = nullptr;
    ElaLineEdit* m_editRoundness     = nullptr;

    ElaText* m_lblFxR  = nullptr;
    ElaText* m_lblFxD  = nullptr;
    ElaText* m_lblFxC  = nullptr;
    ElaText* m_lblFxA0 = nullptr;

    ElaText* m_lblArcLength = nullptr;
    ElaText* m_lblChord     = nullptr;
    QPushButton* m_btnPublish = nullptr;
    QPushButton* m_btnDetach = nullptr;
};

} // namespace cad::ui
