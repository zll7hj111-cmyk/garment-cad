#pragma once

#include <QWidget>
#include <QUuid>

class ElaLineEdit;
class ElaText;
class QPushButton;
class QButtonGroup;
class QVBoxLayout;

namespace cad::param {
class ParamDocument;
class Block;
struct Segment;
}

namespace cad::ui {

/// 正交拐角偏置独立子部件 (LineOrthoOffsetCard, 2026-09-06 重构落地)
/// 职责：
///   - 拐角偏置输入框与公式标签
///   - 无/左/右三态方向切换胶囊组
///   - 画布中心基准轴虚线显隐开关 (👁 基准轴)
///   - 实时斜长显示标签
class LineOrthoOffsetCard : public QWidget
{
    Q_OBJECT

public:
    explicit LineOrthoOffsetCard(QWidget* parent = nullptr);
    ~LineOrthoOffsetCard() override = default;

    void setContext(cad::param::ParamDocument* doc, const QUuid& blockId, const QUuid& segmentId);
    void populate(const cad::param::Block& block, const cad::param::Segment& seg, bool isCurve);
    void apply(cad::param::Block* block, cad::param::Segment* seg);
    void refreshHypotLabel(const cad::param::Block& block, const cad::param::Segment& seg);

    [[nodiscard]] ElaLineEdit* editOrthoDist() const { return m_editOrthoDist; }
    [[nodiscard]] QButtonGroup* orthoDirGroup() const { return m_orthoDirGroup; }
    [[nodiscard]] QPushButton* btnToggleAxis() const { return m_btnToggleAxis; }
    [[nodiscard]] ElaText* lblOrthoHypot() const { return m_lblOrthoHypot; }

signals:
    void orthoChanged();
    void liveUpdated();
    void sceneRefreshRequested();

public slots:
    void onOrthoDistEdited();
    void onOrthoDirChanged(int id);
    void onToggleCenterAxis();

private:
    cad::param::ParamDocument* m_paramDoc = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    ElaText*       m_lblOrthoFx       = nullptr;
    ElaLineEdit*   m_editOrthoDist    = nullptr;
    QPushButton*   m_btnOrthoNone     = nullptr;
    QPushButton*   m_btnOrthoLeft     = nullptr;
    QPushButton*   m_btnOrthoRight    = nullptr;
    QButtonGroup*  m_orthoDirGroup    = nullptr;
    QPushButton*   m_btnToggleAxis    = nullptr;
    ElaText*       m_lblOrthoHypot    = nullptr;
};

} // namespace cad::ui
