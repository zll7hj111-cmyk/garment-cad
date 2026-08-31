#include "ui/SegmentAuxTab.h"

#include <algorithm>

#include "ElaTabWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ElaText.h"
#include <QListWidget>
#include "ElaPushButton.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "ui/AuxPointForm.h"
#include "ui/IntersectionForm.h"

namespace cad::ui {

SegmentAuxTab::SegmentAuxTab(cad::param::ParamDocument* doc,
                             CanvasScene* scene,
                             const std::function<void()>& sceneRefresh,
                             const std::function<void()>& debounceRestart,
                             QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(doc)
    , m_scene(scene)
    , m_sceneRefresh(sceneRefresh)
    , m_debounceRestart(debounceRestart)
{
}

void SegmentAuxTab::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
}

void SegmentAuxTab::build(ElaTabWidget* tabs)
{
    // --- "辅助点" tab ---
    auto* auxPage = new QWidget(this);
    auto* auxLayout = new QVBoxLayout(auxPage);
    auxLayout->setSpacing(8);

    // --- Aux point list ---
    m_auxList = new QListWidget(auxPage);
    m_auxList->setMaximumHeight(120);
    m_auxList->setSelectionMode(QAbstractItemView::SingleSelection);
    auxLayout->addWidget(m_auxList);

    // --- Add / Remove buttons ---
    auto* btnRow = new QHBoxLayout();
    auto* btnAdd = new ElaPushButton(QString::fromUtf8("+ \u6dfb\u52a0"), auxPage);  // + 添加
    auto* btnRemove = new ElaPushButton(QString::fromUtf8("\u2212 \u5220\u9664"), auxPage);  // − 删除
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnRemove);
    btnRow->addStretch();
    auxLayout->addLayout(btnRow);

    connect(btnAdd,    &QPushButton::clicked, this, &SegmentAuxTab::onAdd);
    connect(btnRemove, &QPushButton::clicked, this, &SegmentAuxTab::onRemove);
    connect(m_auxList, &QListWidget::itemSelectionChanged, this, &SegmentAuxTab::onSelectionChanged);

    // --- Edit forms: aux (Interpolated) and intersection, toggled by type ---
    m_auxForm = new AuxPointForm(auxPage);
    m_auxForm->setVisible(false);
    auxLayout->addWidget(m_auxForm);

    m_ixForm = new IntersectionForm(auxPage);
    m_ixForm->setVisible(false);
    auxLayout->addWidget(m_ixForm);

    // Field commits apply immediately; text changes restart the global debounce.
    connect(m_auxForm, &AuxPointForm::dirty, this,
            [this]() { if (m_debounceRestart) m_debounceRestart(); });
    connect(m_auxForm, &AuxPointForm::edited,
            this, &SegmentAuxTab::onLiveUpdate);
    connect(m_ixForm, &IntersectionForm::dirty, this,
            [this]() { if (m_debounceRestart) m_debounceRestart(); });
    connect(m_ixForm, &IntersectionForm::edited,
            this, &SegmentAuxTab::onLiveUpdate);
    connect(m_ixForm, &IntersectionForm::aimCleared, this, [this]() {
        if (m_currentAuxId.isNull() || !m_paramDoc) return;
        cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
        cad::param::ParamPoint* pt = block ? block->findPoint(m_currentAuxId) : nullptr;
        if (pt) pt->interAimPointId = QUuid();
        onLiveUpdate();
    });

    // --- Hint ---
    auto* hint = new ElaText(QString::fromUtf8("\u00b7 \u8f85\u52a9\u70b9\u4f4d\u7f6e = \u8ba1\u91cf\u7aef\u70b9 + \u65b9\u5411 \u00d7 (\u8ddd\u79bb\u00d7\u767e\u5206\u6bd4 + \u5e38\u91cf) + \u504f\u79fb(\u89d2\u5ea6,\u8ddd\u79bb)\n"
                          "\u00b7 \u8ba1\u7b97\u65b9\u5411\u51b3\u5b9a\u4ece\u8d77\u70b9\u8fd8\u662f\u7ec8\u70b9\u5f00\u59cb\u8ba1\u91cf\uff0c\u504f\u8f6c\u89d2\u4ee5\u8be5\u65b9\u5411\u4e3a 0\u00b0\n"
                          "\u00b7 \u767e\u5206\u6bd4\u53ef\u8d85\u51fa [0,1] \u5b9e\u73b0\u5916\u63d2\n"
                          "\u00b7 \u8f85\u52a9\u70b9\u53ef\u4f5c\u4e3a\u5176\u4ed6\u7ebf\u6bb5\u7684\u7aef\u70b9\u6216\u9644\u7740\u76ee\u6807"), 13, auxPage);
    hint->setStyleSheet("font-size:11px;");
    auxLayout->addWidget(hint);
    auxLayout->addStretch();

    tabs->addTab(auxPage, QString::fromUtf8("\u8f85\u52a9\u70b9"));  // 辅助点
}

void SegmentAuxTab::refreshList()
{
    if (!m_auxList) return;
    m_auxList->clear();

    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    for (const auto& auxId : seg->auxPointIds) {
        const cad::param::ParamPoint* pt = block->findPoint(auxId);
        if (!pt) continue;
        QString label = cad::param::Serial::tag(pt->serial);
        if (!pt->name.isEmpty())
            label += QStringLiteral(" \u00b7 ") + pt->name;
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, auxId);
        m_auxList->addItem(item);
    }

    m_auxForm->setVisible(m_auxList->count() > 0 && m_auxList->currentRow() >= 0);
}

void SegmentAuxTab::populateFields()
{
    if (!m_auxList || m_auxList->currentRow() < 0) {
        m_auxForm->setVisible(false);
        m_ixForm->setVisible(false);
        return;
    }

    auto* item = m_auxList->currentItem();
    if (!item) { m_auxForm->setVisible(false); m_ixForm->setVisible(false); return; }

    const QUuid auxId = item->data(Qt::UserRole).toUuid();
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const cad::param::ParamPoint* pt = block->findPoint(auxId);
    if (!pt) { m_auxForm->setVisible(false); m_ixForm->setVisible(false); return; }

    const cad::param::Segment* seg = block->findSegment(m_segmentId);

    // --- Intersection point → IntersectionForm ---
    if (pt->constraint == cad::param::PointConstraint::Intersection) {
        m_auxForm->setVisible(false);

        QString originLabel = QString::fromUtf8("\u5df2\u5220\u9664");  // 已删除
        for (const auto& ob : m_paramDoc->blocks()) {
            const auto* op = ob.findPoint(pt->refPointA);
            if (op) {
                originLabel = cad::param::Serial::tag(op->serial);
                if (!op->name.isEmpty())
                    originLabel += QStringLiteral(" \u00b7 ") + op->name;
                break;
            }
        }
        m_ixForm->setOriginLabel(originLabel);

        // Aim point (指向点) label, searched across all blocks.
        QString aimLabel;
        if (!pt->interAimPointId.isNull()) {
            for (const auto& ob : m_paramDoc->blocks()) {
                const auto* ap = ob.findPoint(pt->interAimPointId);
                if (ap) {
                    aimLabel = cad::param::Serial::tag(ap->serial);
                    if (!ap->name.isEmpty())
                        aimLabel += QStringLiteral(" \u00b7 ") + ap->name;
                    break;
                }
            }
        }
        m_ixForm->setAimLabel(aimLabel);

        double segWorldDir = 0.0;
        if (seg) {
            const auto* sp = block->findPoint(seg->startPointId);
            const auto* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
                cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
                segWorldDir = std::atan2(w2.y - w1.y, w2.x - w1.x) * 180.0 / M_PI;
            }
        }
        m_ixForm->setSegmentWorldDir(segWorldDir);

        m_ixForm->loadFrom(*pt);
        m_ixForm->setVisible(true);
        return;
    }

    // --- Interpolated (aux) point → AuxPointForm ---
    m_ixForm->setVisible(false);

    if (seg) {
        std::vector<std::pair<QUuid, QString>> refPts;
        for (const auto& aid : seg->auxPointIds) {
            if (aid == auxId) continue;
            const auto* ap = block->findPoint(aid);
            if (!ap) continue;
            QString label = cad::param::Serial::tag(ap->serial);
            if (!ap->name.isEmpty())
                label += QStringLiteral(" \u00b7 ") + ap->name;
            refPts.emplace_back(ap->id, label);
        }
        m_auxForm->setRefPointList(refPts);
    }

    m_auxForm->loadFrom(*pt);
    m_auxForm->setVisible(true);
}

void SegmentAuxTab::refreshDirLabels()
{
    if (!m_auxForm || !m_paramDoc) return;
    const cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    const cad::param::Segment* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    m_auxForm->setEndpointLabels(block->findPoint(seg->startPointId),
                                 block->findPoint(seg->endPointId));
}

void SegmentAuxTab::saveSnapshots(const cad::param::Segment* seg)
{
    m_auxSnapshots.clear();
    m_auxAddedIds.clear();
    if (!seg || !m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    for (const auto& auxId : seg->auxPointIds) {
        const cad::param::ParamPoint* apt = block->findPoint(auxId);
        if (!apt) continue;
        AuxSnapshot snap;
        snap.pointId          = apt->id;
        snap.percent          = apt->interpPercent;
        snap.percentFormula   = apt->interpPercentFormula;
        snap.constant         = apt->interpConstant;
        snap.constantFormula  = apt->interpConstantFormula;
        snap.fromEnd          = apt->interpFromEnd;
        snap.showName         = apt->showName;
        snap.name             = apt->name;
        m_auxSnapshots.push_back(snap);
    }
}

void SegmentAuxTab::restoreSnapshots()
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // Remove session-added aux points.
    for (const auto& addedId : m_auxAddedIds) {
        auto& ids = seg->auxPointIds;
        ids.erase(std::remove(ids.begin(), ids.end(), addedId), ids.end());
        auto& pts = block->points;
        pts.erase(std::remove_if(pts.begin(), pts.end(),
            [&addedId](const cad::param::ParamPoint& p) { return p.id == addedId; }),
            pts.end());
    }
    // Restore snapshotted values.
    for (const auto& snap : m_auxSnapshots) {
        cad::param::ParamPoint* apt = block->findPoint(snap.pointId);
        if (!apt) continue;
        apt->interpPercent          = snap.percent;
        apt->interpPercentFormula   = snap.percentFormula;
        apt->interpConstant         = snap.constant;
        apt->interpConstantFormula  = snap.constantFormula;
        apt->interpFromEnd          = snap.fromEnd;
        apt->showName               = snap.showName;
        apt->name                   = snap.name;
    }
    block->rebuildPointIndex();
}

void SegmentAuxTab::onSelectionChanged()
{
    m_currentAuxId = QUuid();
    if (m_auxList && m_auxList->currentItem())
        m_currentAuxId = m_auxList->currentItem()->data(Qt::UserRole).toUuid();
    populateFields();
}

void SegmentAuxTab::onAdd()
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Interpolated;
    pt.hostSegmentId = m_segmentId;
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;
    pt.interpPercent = 0.5;
    pt.interpConstant = 0.0;
    pt.interpOffsetAngle = 0.0;
    pt.interpOffsetDist = 0.0;
    pt.interpFromEnd = false;

    pt.serial = m_paramDoc->newPointSerial();

    const QUuid ptId = block->addPoint(pt);
    seg->auxPointIds.push_back(ptId);
    m_auxAddedIds.push_back(ptId);

    refreshList();
    m_sceneRefresh();

    m_auxList->setCurrentRow(m_auxList->count() - 1);
}

void SegmentAuxTab::onRemove()
{
    if (!m_auxList || m_auxList->currentRow() < 0) return;
    if (!m_paramDoc) return;

    auto* item = m_auxList->currentItem();
    if (!item) return;

    const QUuid auxId = item->data(Qt::UserRole).toUuid();
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    auto& ids = seg->auxPointIds;
    ids.erase(std::remove(ids.begin(), ids.end(), auxId), ids.end());

    auto& pts = block->points;
    pts.erase(std::remove_if(pts.begin(), pts.end(),
        [&auxId](const cad::param::ParamPoint& p) { return p.id == auxId; }),
        pts.end());
    block->rebuildPointIndex();

    auto addedIt = std::find(m_auxAddedIds.begin(), m_auxAddedIds.end(), auxId);
    if (addedIt != m_auxAddedIds.end())
        m_auxAddedIds.erase(addedIt);

    refreshList();
    m_sceneRefresh();
}

void SegmentAuxTab::onLiveUpdate()
{
    if (m_currentAuxId.isNull()) return;
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::ParamPoint* pt = block->findPoint(m_currentAuxId);
    if (!pt) return;

    if (pt->constraint == cad::param::PointConstraint::Intersection)
        m_ixForm->applyTo(*pt);
    else
        m_auxForm->applyTo(*pt);

    if (m_auxList) {
        for (int i = 0; i < m_auxList->count(); ++i) {
            if (m_auxList->item(i)->data(Qt::UserRole).toUuid() == m_currentAuxId) {
                QString label = cad::param::Serial::tag(pt->serial);
                if (!pt->name.isEmpty())
                    label += QStringLiteral(" \u00b7 ") + pt->name;
                m_auxList->item(i)->setText(label);
                break;
            }
        }
    }

    m_sceneRefresh();
}

} // namespace cad::ui
