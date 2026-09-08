#pragma once

#include <QUuid>
#include <QWidget>

class ElaText;
class QPushButton;

namespace cad::param {
class ParamDocument;
struct Attachment;
}

namespace cad::ui {

class PointRefEdit;

class SegmentAngleRefCard : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentAngleRefCard(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    void refresh();

    void setShadowBasisMode(bool shadowBasis);

signals:
    void changed();
    void rejectRequested(const QString& reason);

private:
    void onAngleRefPointResolved(const QUuid& blockId, const QUuid& pointId);
    void onAngleRefPoint2Resolved(const QUuid& blockId, const QUuid& pointId);
    void onIndependentToggled(bool checked);
    void onLinkCurrentLineClicked();
    void onResetBenchmarkClicked();

    void refreshAngleRefRow(const cad::param::Attachment* att);

    cad::param::ParamDocument* m_doc = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;
    bool m_shadowBasisMode = false;

    ElaText*      m_lblDirWord = nullptr;
    PointRefEdit* m_angleRefPoint = nullptr;
    ElaText*      m_lblArrow = nullptr;
    PointRefEdit* m_angleRefPoint2 = nullptr;
    QPushButton*  m_btnResetBenchmark = nullptr;
    QPushButton*  m_btnIndependent = nullptr;
    QPushButton*  m_btnLinkCurrent = nullptr;
};

} // namespace cad::ui
