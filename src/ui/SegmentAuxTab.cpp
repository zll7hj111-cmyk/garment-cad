#include "SegmentAuxTab.h"

#include <algorithm>

#include "ElaTabWidget.h"
#include "ElaTabWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaScrollPageArea.h"
#include <QListWidget>
#include "ElaPushButton.h"
#include <QMouseEvent>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "canvas/CanvasScene.h"
#include "AuxPointForm.h"
#include "IntersectionForm.h"
#include "tools/LayerFeedback.h"
#include "ui/Theme.h"

namespace cad::tools {

namespace {

/// 跟随线角色色 — 跨层 badge 同族语义橙。CanvasStyle 侧同步定义 (canvas
/// 端点/跟随线高亮同色); 此处是 UI rich-text 侧, 与 CanvasStyle 手工同步。
/// 换主题时两处需一起改。
const QString kFollowerRoleColor = QStringLiteral("#D97706");

/// One connection at an endpoint of the current segment.
struct ConnEntry {
    bool    isLeader = false;  ///< true = related segment is the leader (基准线).
    QString segSerial;
    QString segName;
    QString pointSerial;
    QString pointName;
    double  angle = 0.0;
    QUuid   blockId;
    QUuid   segmentId;
    QString layerBadge;  ///< Cross-layer badge ("→ 层名"); empty = same layer.
};

/// Clickable bubble card describing one connection. Single-click selects the
/// related segment on canvas; double-click jumps the dialog to edit it.
class ConnCard : public ElaScrollPageArea
{
public:
    ConnCard(const ConnEntry& e, CanvasScene* scene,
             SegmentAuxTab* tab, QWidget* parent)
        : ElaScrollPageArea(parent), m_scene(scene), m_tab(tab)
        , m_blockId(e.blockId), m_segmentId(e.segmentId)
    {
        // ElaScrollPageArea's constructor hard-codes setFixedHeight(75); lift
        // it so the connection card sizes itself from its content.
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setCursor(Qt::PointingHandCursor);

        const QString role = e.isLeader ? QString::fromUtf8("\u57fa\u51c6\u7ebf")   // 基准线
                                        : QString::fromUtf8("\u8ddf\u968f\u7ebf");  // 跟随线
        const QString roleColor = e.isLeader ? cad::ui::Theme::tokens().text1.name()
                                             : kFollowerRoleColor;
        const QString angleLabel = e.isLeader
            ? QString::fromUtf8("跟随角度")        // 跟随角度（本线所有）
            : QString::fromUtf8("其跟随角度");    // 其跟随角度（跟随线所有）
        // Cross-layer badge ("→ 操作层1") appended after the arrow; empty for
        // same-layer connections (no markup, card keeps its original look).
        const QString badgeHtml = e.layerBadge.isEmpty() ? QString()
            : QStringLiteral(" <span style='%1'>%2</span>")
                .arg(cad::ui::Theme::purpleBadgeStyle(),
                     e.layerBadge.toHtmlEscaped());
        // Segment label: NAME first, serial as the fallback when unnamed
        // (有名称显示名称，无名称显示编号); same rule for the point.
        const QString segLabel = e.segName.isEmpty()
            ? cad::param::Serial::toHtml(e.segSerial)
            : e.segName.toHtmlEscaped();
        const QString pointLabel = e.pointName.isEmpty()
            ? cad::param::Serial::toHtml(e.pointSerial)
            : e.pointName.toHtmlEscaped();
        QString html = QStringLiteral(
            "<div style='margin:2px;'>"
            "<div><b style='color:%1;'>[%2]</b> %3 &nbsp;%4 &nbsp;<span style='color:__ACCENT__;'>&rarr;</span>%9</div>"
            "<div style='color:__MUTED__;'>\u70b9 %5 &middot; %6 &middot; %7 &ang;%8&deg;</div>"
            "</div>")
            .arg(roleColor, role,
                 segLabel,
                 e.segName.isEmpty() ? QStringLiteral("") : cad::param::Serial::toHtml(e.segSerial),
                 cad::param::Serial::toHtml(e.pointSerial),
                 pointLabel,
                 angleLabel,
                 QString::number(e.angle, 'f', 1),
                 badgeHtml);
        html.replace(QStringLiteral("__ACCENT__"), cad::ui::Theme::tokens().text1.name());
        html.replace(QStringLiteral("__MUTED__"), cad::ui::Theme::tokens().text3.name());

        setToolTip(e.isLeader
            ? QStringLiteral("基准线：当前线（跟随线）的端点吸附于该线。\n"
                             "跟随角度归属于当前线：以基准线在吸附点处的延长方向为 0°（直行），逆时针为正。")
            : QStringLiteral("跟随线：该线的端点吸附于当前线（基准线）。\n"
                             "显示的跟随角度归属于该跟随线；双击卡片可跳转编辑。"));

        auto* label = new ElaText(html, 13, this);
        label->setTextFormat(Qt::RichText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(5, 3, 5, 3);
        lay->addWidget(label);
    }

protected:
    void mousePressEvent(QMouseEvent*) override
    {
        if (m_scene && !m_blockId.isNull())
            m_scene->selectBlock(m_blockId);
    }
    void mouseDoubleClickEvent(QMouseEvent*) override
    {
        if (m_tab && !m_blockId.isNull() && !m_segmentId.isNull())
            emit m_tab->jumpRequested(m_blockId, m_segmentId);
    }

private:
    CanvasScene* m_scene;
    SegmentAuxTab* m_tab;
    QUuid m_blockId;
    QUuid m_segmentId;
};

} // namespace

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

    // --- "点连接" tab ---
    auto* connPage = new QWidget(this);
    auto* layout = new QVBoxLayout(connPage);
    layout->setSpacing(8);

    auto* header = new ElaText(QString::fromUtf8("\u8fde\u63a5\u5173\u7cfb\uff1a"), 13, connPage);
    // 连接关系：
    header->setStyleSheet("font-weight:600;");
    layout->addWidget(header);

    auto* scrollContent = new QWidget(connPage);
    m_auxConnLayout = new QVBoxLayout(scrollContent);
    m_auxConnLayout->setContentsMargins(0, 0, 0, 0);
    m_auxConnLayout->setSpacing(6);
    m_auxConnLayout->addStretch();
    layout->addWidget(scrollContent, 1);

    auto* connHint = new ElaText(QString::fromUtf8("\u00b7 \u5355\u51fb\u5361\u7247\u9009\u4e2d\u5bf9\u5e94\u7ebf\u6761\uff0c\u53cc\u51fb\u8df3\u8f6c\u7f16\u8f91\n"
                          "\u00b7 \u6784\u9020\u89d2\u4ee5\u5bbf\u4e3b\u7ebf\u6bb5\u65b9\u5411\u4e3a 0\u00b0 \u57fa\u51c6"), 13, connPage);
    connHint->setStyleSheet("font-size:11px;");
    layout->addWidget(connHint);

    tabs->addTab(connPage, QString::fromUtf8("\u70b9\u8fde\u63a5"));  // 点连接
}

void SegmentAuxTab::refreshConnections()
{
    if (!m_auxConnLayout) return;

    // Clear existing cards (keep trailing stretch)
    while (m_auxConnLayout->count() > 1) {
        QLayoutItem* it = m_auxConnLayout->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // Points that may host an incoming connection: both ENDPOINTS and every
    // auxiliary point of this segment (端点 + 辅助点).
    QList<QUuid> hostPoints;
    hostPoints << seg->startPointId << seg->endPointId;
    for (const auto& auxId : seg->auxPointIds)
        hostPoints << auxId;

    bool anyFound = false;

    // Effective follower angle for display (弧长模式: 弧长 0 = 角度 0° 折叠基准).
    auto makeCard = [&](bool isLeader, const cad::param::Block* otherBlk,
                        const cad::param::Segment* otherSeg,
                        const cad::param::ParamPoint* hostPt,
                        const cad::param::Attachment& att) {
        double dispAngle = att.followerAngle;
        if (att.rotationMode == cad::param::RotationMode::ArcLength) {
            double arcMm = att.arcLength;
            if (!att.arcLengthFormula.isEmpty()) {
                auto r = cad::param::ConditionEngine::evaluate(
                    att.arcLengthFormula, m_paramDoc->parameters(), {});
                if (r.ok) arcMm = cad::geo::Units::cmToMm(r.value);
            }
            // Radius = the FOLLOWER's segment length at its connection point
            // (incoming: otherBlk is the follower; outgoing: THIS block is).
            const cad::param::Block* followerBlk = isLeader ? block : otherBlk;
            const double radius = followerBlk->segmentLengthAtPoint(att.fromPointId);
            // 弧长 = 线夹角恒等映射（2026-08 定稿）：弧长 0 = 0° 折叠、
            // πr = 180° 开平，与 Resolver/HUD 同基准。显示 = 带符号折角
            // （v3 定稿：折叠 0 / 垂直 ±90 / 开平 ±180，符号 = 折向）。
            dispAngle = (radius > 1e-9)
                ? (arcMm / radius) * 180.0 / M_PI : 0.0;
            dispAngle = std::fmod(dispAngle, 360.0);
            if (dispAngle < 0.0) dispAngle += 360.0;
            if (dispAngle > 180.0) dispAngle -= 360.0;
        } else if (!att.followerAngleFormula.isEmpty()) {
            auto r = cad::param::ConditionEngine::evaluate(
                att.followerAngleFormula, m_paramDoc->parameters(), {});
            if (r.ok) dispAngle = r.value;
        } else {
            // 角度模式：存储 α ∈ [0, 360°) → 显示带符号折角（v3 定稿）。
            dispAngle = std::fmod(dispAngle, 360.0);
            if (dispAngle > 180.0) dispAngle -= 360.0;
        }
        auto* card = new ConnCard(
            ConnEntry{isLeader, otherSeg->serial, otherSeg->name,
                      hostPt->serial, hostPt->name,
                      dispAngle, otherBlk->id, otherSeg->id,
                      crossLayerBadge(m_paramDoc, att)},
            m_scene, this, m_auxConnLayout->widget());
        m_auxConnLayout->insertWidget(m_auxConnLayout->count() - 1, card);
    };

    // Incoming: other segments attach to THIS segment's points (被哪条线段连接).
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.toBlockId != m_blockId) continue;
        if (!hostPoints.contains(att.toPointId)) continue;

        const cad::param::ParamPoint* hostPt = block->findPoint(att.toPointId);
        if (!hostPt) continue;
        anyFound = true;
        const cad::param::Block* fb = m_paramDoc->findBlock(att.fromBlockId);
        if (!fb || fb->segments.empty()) continue;
        const cad::param::Segment& fseg = fb->segments.front();
        makeCard(false, fb, &fseg, hostPt, att);
    }

    // Outgoing: THIS segment hangs on a leader (本线连接了谁). The related
    // segment shown is the LEADER; the point is our own connection point.
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId != m_blockId) continue;
        if (att.isPin) continue;   // 桥 pin 无跟随角度，不在点连接列表
        if (!hostPoints.contains(att.fromPointId)) continue;

        const cad::param::ParamPoint* hostPt = block->findPoint(att.fromPointId);
        if (!hostPt) continue;
        anyFound = true;
        const cad::param::Block* lb = m_paramDoc->findBlock(att.toBlockId);
        if (!lb || lb->segments.empty()) continue;
        const cad::param::Segment* lseg = att.toSegmentId.isNull()
            ? nullptr : lb->findSegment(att.toSegmentId);
        if (!lseg) lseg = &lb->segments.front();
        makeCard(true, lb, lseg, hostPt, att);
    }

    if (!anyFound) {
        auto* empty = new ElaText(QString::fromUtf8("\uff08\u65e0\u70b9\u8fde\u63a5\uff09"), 13, m_auxConnLayout->widget());  // （无点连接）
        empty->setStyleSheet("padding:8px;");
        empty->setAlignment(Qt::AlignCenter);
        m_auxConnLayout->insertWidget(m_auxConnLayout->count() - 1, empty);
    }
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

} // namespace cad::tools
