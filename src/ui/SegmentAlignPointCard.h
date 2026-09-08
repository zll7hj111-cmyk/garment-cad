#pragma once

#include <QUuid>
#include <QWidget>

namespace cad::param {
class ParamDocument;
}

namespace cad::ui {

class PointRefEdit;

class SegmentAlignPointCard : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentAlignPointCard(cad::param::ParamDocument* doc, QWidget* parent = nullptr);

    void setTarget(const QUuid& blockId, const QUuid& segmentId);
    void refresh();
    void flashRed(int ms = 900);

signals:
    void changed();
    void rejectRequested(const QString& reason);

private:
    void onAlignPointResolved(const QUuid& blockId, const QUuid& pointId);

    cad::param::ParamDocument* m_doc = nullptr;
    QUuid m_blockId;
    QUuid m_segmentId;

    PointRefEdit* m_alignPointEdit = nullptr;
};

} // namespace cad::ui
