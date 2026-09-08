#pragma once

#include <QUuid>
#include <QWidget>

class ElaLineEdit;
class QPushButton;
class ElaText;

namespace cad::param {
class ParamDocument;
}

namespace cad::ui {

class SegmentShadowBasisCard : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentShadowBasisCard(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    void refresh();

signals:
    void changed();

private:
    void onShadowAngleEdited();
    void onClearShadowClicked();

    cad::param::ParamDocument* m_doc = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    ElaText*     m_lblBasis = nullptr;
    ElaLineEdit* m_shadowAngleEdit = nullptr;
    QPushButton* m_btnClearShadow = nullptr;
};

} // namespace cad::ui
