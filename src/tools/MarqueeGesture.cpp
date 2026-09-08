#include "MarqueeGesture.h"

#include <cmath>

#include <QGraphicsRectItem>
#include <QPen>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "canvas/BlockItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Component.h"
#include "geometry/Units.h"
#include "geometry/Epsilon.h"
#include "tools/HitTester.h"  // isInteractiveBlock（影子/图层过滤唯一规则源）

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
        if (std::abs(p[i]) < cad::geo::kGeomEpsTight) {
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

QSet<QUuid> MarqueeGesture::expandWithGroups(cad::param::ParamDocument* doc, const QSet<QUuid>& ids)
{
    if (!doc || ids.isEmpty()) return ids;
    QSet<QUuid> expanded = ids;
    for (const QUuid& id : ids) {
        if (const auto* comp = doc->componentOfBlock(id)) {
            for (const QUuid& mid : comp->memberBlockIds) {
                expanded.insert(mid);
            }
        }
    }
    return expanded;
}

void MarqueeGesture::begin(const cad::geo::Vec2& pos, const QSet<QUuid>& baseSelection)
{
    if (!m_scene) return;
    cancel();
    m_start = pos;
    m_base = baseSelection;

    m_item = new QGraphicsRectItem();
    m_managed.own(m_item, &m_item);
    const QColor marquee = m_scene->style()->marqueeColor;   // 审计 P0-1: 与 TransientOverlay 同源
    QPen pen(marquee, 0);      // cosmetic 1px dash
    pen.setStyle(Qt::DashLine);
    m_item->setPen(pen);
    m_item->setBrush(QColor(marquee.red(), marquee.green(), marquee.blue(), 25));
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
    m_managed.clear();
}

QSet<QUuid> MarqueeGesture::hitsIn(const QRectF& rectUser) const
{
    QSet<QUuid> result;
    if (!m_doc) return result;

    for (const auto& blk : m_doc->blocks()) {
        // 框选资格 = 与点选同一谓词（活动层 + 非影子）；原先只比 layer，
        // 导致影子块能被框选却点不中（TOOL-P1-12）。
        if (!isInteractiveBlock(blk, *m_doc)) continue;
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
