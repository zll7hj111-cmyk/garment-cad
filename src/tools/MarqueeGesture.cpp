#include "MarqueeGesture.h"

#include <cmath>

#include <QGraphicsRectItem>
#include <QPen>

#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "geometry/Units.h"

namespace cad::tools {

namespace {

/// Liang-Barsky clipping; also counts either endpoint inside the rect.
bool segmentIntersectsRect(const cad::geo::Vec2& p1, const cad::geo::Vec2& p2,
                           const QRectF& r)
{
    if (r.contains(QPointF(p1.x, p1.y)) || r.contains(QPointF(p2.x, p2.y)))
        return true;

    double t0 = 0.0, t1 = 1.0;
    const double dx = p2.x - p1.x, dy = p2.y - p1.y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {p1.x - r.left(), r.right() - p1.x,
                         p1.y - r.top(),  r.bottom() - p1.y};
    for (int i = 0; i < 4; ++i) {
        if (std::abs(p[i]) < 1e-12) {
            if (q[i] < 0) return false;   // parallel and outside
        } else {
            const double rr = q[i] / p[i];
            if (p[i] < 0) { if (rr > t1) return false; if (rr > t0) t0 = rr; }
            else          { if (rr < t0) return false; if (rr < t1) t1 = rr; }
        }
    }
    return t0 <= t1;
}

} // namespace

MarqueeGesture::MarqueeGesture(CanvasScene* scene, cad::param::ParamDocument* doc)
    : m_scene(scene)
    , m_doc(doc)
{
}

MarqueeGesture::~MarqueeGesture()
{
    cancel();
}

QSet<QUuid> MarqueeGesture::expandWithGroups(cad::param::ParamDocument* doc,
                                             const QSet<QUuid>& ids)
{
    QSet<QUuid> result = ids;
    if (!doc) return result;
    for (const QUuid& id : ids) {
        const QUuid gid = doc->groupOfBlock(id);
        if (gid.isNull()) continue;
        const QList<QUuid> members = doc->blocksInGroup(gid);
        for (const QUuid& memberId : members)
            result.insert(memberId);
    }
    return result;
}

void MarqueeGesture::begin(const cad::geo::Vec2& pos, const QSet<QUuid>& baseSelection)
{
    if (!m_scene) return;
    cancel();
    m_start = pos;
    m_base = baseSelection;

    m_item = new QGraphicsRectItem();
    QPen pen(QColor(0, 120, 215), 0);      // cosmetic 1px dash
    pen.setStyle(Qt::DashLine);
    m_item->setPen(pen);
    m_item->setBrush(QColor(0, 120, 215, 25));
    m_item->setZValue(9999);
    m_scene->addItem(m_item);
    m_item->setRect(QRectF());
    m_item->show();
}

void MarqueeGesture::update(const cad::geo::Vec2& pos)
{
    if (!m_item || !m_doc || !m_scene) return;

    const QRectF userRect = QRectF(QPointF(m_start.x, m_start.y),
                                   QPointF(pos.x, pos.y)).normalized();
    // User → scene coords for the on-screen rectangle.
    const QPointF stl = cad::geo::Coord::toScene(userRect.left(),  userRect.top());
    const QPointF sbr = cad::geo::Coord::toScene(userRect.right(), userRect.bottom());
    m_item->setRect(QRectF(stl, sbr).normalized());

    syncPreview(expandWithGroups(m_doc, toggleOf(userRect)));
}

QSet<QUuid> MarqueeGesture::end(const cad::geo::Vec2& pos)
{
    if (!m_item) return m_base;
    const QRectF userRect = QRectF(QPointF(m_start.x, m_start.y),
                                   QPointF(pos.x, pos.y)).normalized();
    cancel();
    if (!m_doc) return m_base;
    return expandWithGroups(m_doc, toggleOf(userRect));
}

void MarqueeGesture::cancel()
{
    if (!m_scene || !m_item) return;
    m_scene->removeItem(m_item);
    delete m_item;
    m_item = nullptr;
}

QSet<QUuid> MarqueeGesture::hitsIn(const QRectF& rectUser) const
{
    QSet<QUuid> result;
    if (!m_doc) return result;

    for (const auto& blk : m_doc->blocks()) {
        // Marquee only selects blocks on the active layer.
        if (blk.layer != m_doc->activeLayer()) continue;
        for (const auto& seg : blk.segments) {
            const auto* sp = blk.findPoint(seg.startPointId);
            const auto* ep = blk.findPoint(seg.endPointId);
            if (!sp || !ep || !sp->resolved || !ep->resolved) continue;
            const cad::geo::Vec2 w1 = blk.transform.toWorld(sp->resolvedPos);
            const cad::geo::Vec2 w2 = blk.transform.toWorld(ep->resolvedPos);
            if (segmentIntersectsRect(w1, w2, rectUser)) {
                result.insert(blk.id);
                break;  // one intersecting segment is enough for this block
            }
        }
    }
    return result;
}

QSet<QUuid> MarqueeGesture::toggleOf(const QRectF& rectUser) const
{
    // Live preview of the prospective toggle result (base XOR intersecting),
    // expanded to whole groups (group = minimal selection unit).
    QSet<QUuid> prospective = m_base;
    for (const QUuid& id : hitsIn(rectUser)) {
        if (prospective.contains(id)) prospective.remove(id);
        else                          prospective.insert(id);
    }
    return prospective;
}

void MarqueeGesture::syncPreview(const QSet<QUuid>& prospective)
{
    if (!m_doc || !m_scene) return;
    for (const auto& blk : m_doc->blocks()) {
        if (BlockItem* bi = m_scene->findBlockItem(blk.id)) {
            bi->setToolSelected(prospective.contains(blk.id));
            bi->setToolLocked(false);  // marquee invalidates confirmation
        }
    }
}

} // namespace cad::tools
