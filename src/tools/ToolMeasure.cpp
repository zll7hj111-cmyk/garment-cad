#include "ToolMeasure.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsView>
#include <QPen>
#include <QKeyEvent>
#include <QUndoStack>
#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"
#include "parametric/ParamDocument.h"
#include "parametric/MeasureVariable.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "document/commands/VariableCommands.h"
#include "ui/MeasureResultDialog.h"
#include "canvas/HudItem.h"

namespace cad::tools {

namespace {
/// Two points are considered "coincident on the measured axis" when the span
/// is below this epsilon (mm). Below the 0.1 mm display precision the result
/// would read 0.00 cm — refuse instead of publishing a useless measure.
constexpr double kAxisZeroEps = 0.05;
} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ToolDescriptor ToolMeasure::describe()
{
    ToolDescriptor d;
    d.id = ToolType::Measure;
    d.displayName = QString::fromUtf8("测量(&M)");
    d.iconName = QStringLiteral("ruler");
    // M 快捷键让给画布长按显示长度 (CanvasView::keyPressEvent), 测量不设快捷键。
    d.hintText = QString::fromUtf8("测量：点选第一个点 | 点选第二个点 → 自动发布距离变量 | 右键/Esc取消");
    d.factory = [] { return std::make_unique<ToolMeasure>(); };
    return d;
}

void ToolMeasure::onActivate(CanvasScene& scene, cad::param::ParamDocument* paramDoc)
{
    (void)scene;
    (void)paramDoc;
    m_state = State::SelectA;
    m_kind = cad::param::MeasureKind::Distance;
    // P2/L5 常驻实例: 清上次会话残留的吸附点。
    m_snapA.reset();
    m_hoverSnap.reset();

    // Persistent mode HUD: tells the user which mode is active even before
    // point A is picked (and after every W cycle).
    ensureHud()->setText(modeHint());
    m_hud->setVisible(true);
}

void ToolMeasure::onDeactivate()
{
    clearPreview();
    m_managed.clear();   // 统一释放 + 影子指针置空 (P1/L1)
}

// ---------------------------------------------------------------------------
// Input events
// ---------------------------------------------------------------------------

void ToolMeasure::mousePress(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    if (event->button() == Qt::RightButton) {
        resetToSelectA();
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 clickPos(sp.x(), sp.y());
    double zoom = m_scene->currentZoom();

    auto snap = m_snapEngine.findSnap(clickPos, m_paramDoc, zoom);
    if (!snap) return;

    if (m_state == State::SelectA) {
        // Commit first point.
        m_snapA = snap;
        m_state = State::SelectB;

        // Marker at A.
        if (!m_markerA) {
            constexpr double r = 5.0;
            m_markerA = new QGraphicsEllipseItem(-r, -r, r * 2.0, r * 2.0);
            QPen pen(m_scene->style()->snapPointColor, 2.0);
            pen.setCosmetic(true);
            m_markerA->setPen(pen);
            m_markerA->setBrush(Qt::NoBrush);
            m_markerA->setZValue(102.0);
            m_scene->addItem(m_markerA);
            m_managed.own(m_markerA, &m_markerA);
        }
        m_markerA->setPos(cad::geo::Coord::toScene(snap->worldPos));
        m_markerA->setVisible(true);
    } else {
        // Second point: ensure it is a different point, then commit.
        if (m_snapA && snap->pointId != m_snapA->pointId) {
            m_hoverSnap = snap;
            commitMeasure();
        }
    }
}

void ToolMeasure::mouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_scene || !m_paramDoc) return;

    const QPointF sp = event->scenePos();
    const cad::geo::Vec2 cursorPos(sp.x(), sp.y());
    m_lastCursor = cursorPos;
    double zoom = m_scene->currentZoom();

    updateHover(cursorPos, zoom);
    if (m_state == State::SelectB)
        updatePreview(cursorPos);
    else if (m_hud && m_hud->isVisible())
        m_hud->moveToPoint(cursorPos, m_scene->views().isEmpty() ? nullptr : m_scene->views().first());
}

void ToolMeasure::mouseRelease(QGraphicsSceneMouseEvent* event)
{
    (void)event;
}

void ToolMeasure::keyPress(QKeyEvent* event)
{
    if (event->key() == Qt::Key_W) {
        // 工具模式切换统一用 W 键（Tab 是焦点导航键，禁用）。
        cycleKind();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        resetToSelectA();
    }
}

// ---------------------------------------------------------------------------
// Mode helpers
// ---------------------------------------------------------------------------

void ToolMeasure::cycleKind()
{
    switch (m_kind) {
        case cad::param::MeasureKind::Distance:   m_kind = cad::param::MeasureKind::Horizontal; break;
        case cad::param::MeasureKind::Horizontal: m_kind = cad::param::MeasureKind::Vertical;   break;
        case cad::param::MeasureKind::Vertical:   m_kind = cad::param::MeasureKind::Distance;   break;
    }
    if (!m_hud) return;
    if (m_state == State::SelectA) {
        // Before point A: only the mode hint matters (position follows the
        // cursor on the next mouse move).
        m_hud->setText(modeHint());
    } else {
        // Mid-selection: refresh the preview + readout immediately.
        updatePreview(m_lastCursor);
    }
}

QString ToolMeasure::modeHint() const
{
    switch (m_kind) {
        case cad::param::MeasureKind::Horizontal:
            return QStringLiteral("\u6c34\u5e73\u6d4b\u91cf\uff1a\u70b9\u9009\u7b2c\u4e00\u4e2a\u70b9");  // 水平测量：点选第一个点
        case cad::param::MeasureKind::Vertical:
            return QStringLiteral("\u5782\u76f4\u6d4b\u91cf\uff1a\u70b9\u9009\u7b2c\u4e00\u4e2a\u70b9");  // 垂直测量：点选第一个点
        case cad::param::MeasureKind::Distance:
            break;
    }
    return QStringLiteral("\u8ddd\u79bb\u6d4b\u91cf\uff1a\u70b9\u9009\u7b2c\u4e00\u4e2a\u70b9");  // 距离测量：点选第一个点
}

double ToolMeasure::spanValue(const cad::geo::Vec2& a, const cad::geo::Vec2& b) const
{
    switch (m_kind) {
        case cad::param::MeasureKind::Horizontal: return std::abs(b.x - a.x);
        case cad::param::MeasureKind::Vertical:   return std::abs(b.y - a.y);
        case cad::param::MeasureKind::Distance:   break;
    }
    return a.distanceTo(b);
}

bool ToolMeasure::axisCoincident(const cad::geo::Vec2& a, const cad::geo::Vec2& b) const
{
    switch (m_kind) {
        case cad::param::MeasureKind::Horizontal: return std::abs(b.x - a.x) < kAxisZeroEps;
        case cad::param::MeasureKind::Vertical:   return std::abs(b.y - a.y) < kAxisZeroEps;
        case cad::param::MeasureKind::Distance:   break;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Hover / preview
// ---------------------------------------------------------------------------

void ToolMeasure::updateHover(const cad::geo::Vec2& pos, double zoom)
{
    auto snap = m_snapEngine.findSnap(pos, m_paramDoc, zoom);
    m_hoverSnap = snap;
    if (!m_scene->views().isEmpty()) {
        if (snap)
            m_scene->views().first()->setCursor(Qt::CrossCursor);
        else
            m_scene->views().first()->unsetCursor();
    }
}

void ToolMeasure::updatePreview(const cad::geo::Vec2& cursorPos)
{
    if (!m_snapA) return;

    // Prefer the snapped hover point's position for a stable readout.
    cad::geo::Vec2 endPos = cursorPos;
    if (m_hoverSnap) endPos = m_hoverSnap->worldPos;

    const cad::geo::Vec2 wa = m_snapA->worldPos;

    // Dimension-line style preview: 距离 = straight A→cursor; 水平 = horizontal
    // span at the cursor's y (both x projected); 垂直 = vertical span at the
    // cursor's x (both y projected). The line visually answers "what is being
    // measured" for each mode.
    cad::geo::Vec2 lineStart = wa;
    switch (m_kind) {
        case cad::param::MeasureKind::Horizontal:
            lineStart = cad::geo::Vec2(wa.x, endPos.y);
            break;
        case cad::param::MeasureKind::Vertical:
            lineStart = cad::geo::Vec2(endPos.x, wa.y);
            break;
        case cad::param::MeasureKind::Distance:
            break;
    }

    if (!m_previewLine) {
        m_previewLine = new QGraphicsLineItem();
        QPen pen(QColor(0xFF, 0x98, 0x00), 1.4);  // amber
        pen.setCosmetic(true);
        pen.setStyle(Qt::DashLine);
        m_previewLine->setPen(pen);
        m_previewLine->setZValue(101.0);
        m_scene->addItem(m_previewLine);
        m_managed.own(m_previewLine, &m_previewLine);
    }
    m_previewLine->setLine(QLineF(cad::geo::Coord::toScene(lineStart),
                                  cad::geo::Coord::toScene(endPos)));
    m_previewLine->setVisible(true);

    if (m_hud) {
        const double valueMm = spanValue(wa, endPos);
        QString text = cad::geo::Units::formatLength(valueMm);
        switch (m_kind) {
            case cad::param::MeasureKind::Horizontal:
                text.prepend(QStringLiteral("\u6c34\u5e73 "));  // 水平
                break;
            case cad::param::MeasureKind::Vertical:
                text.prepend(QStringLiteral("\u5782\u76f4 "));  // 垂直
                break;
            case cad::param::MeasureKind::Distance:
                break;
        }
        m_hud->setText(text);
        QGraphicsView* view = m_scene->views().isEmpty() ? nullptr : m_scene->views().first();
        m_hud->moveToPoint(endPos, view);
        m_hud->setVisible(true);
    }
}

void ToolMeasure::clearPreview()
{
    if (m_previewLine) m_previewLine->setVisible(false);
    if (m_markerA)     m_markerA->setVisible(false);
    if (m_hud)         m_hud->setVisible(false);
}

void ToolMeasure::resetToSelectA()
{
    clearPreview();
    m_snapA.reset();
    m_state = State::SelectA;
}

// ---------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------

void ToolMeasure::commitMeasure()
{
    if (!m_paramDoc || !m_snapA || !m_hoverSnap) return;

    const cad::geo::Vec2 wa = m_snapA->worldPos;
    const cad::geo::Vec2 wb = m_hoverSnap->worldPos;

    // 水平/垂直模式: 两点在测量轴上重合时拒绝创建 (dx≈0 / dy≈0 会量出
    // 0.00cm, 没有意义)。第二击被忽略, 停留在 SelectB 让用户换点或 W 切模式。
    if (axisCoincident(wa, wb)) {
        if (m_scene) {
            m_scene->showToast(
                m_kind == cad::param::MeasureKind::Horizontal
                    ? QStringLiteral("\u4e24\u70b9\u6c34\u5e73\u91cd\u5408\uff0c\u65e0\u6cd5\u6c34\u5e73\u6d4b\u91cf\uff1a\u8bf7\u6362\u70b9\u6216\u6309 W \u5207\u6362\u8ddd\u79bb/\u5782\u76f4\u6a21\u5f0f")  // 两点水平重合，无法水平测量：请换点或按 W 切换距离/垂直模式
                    : QStringLiteral("\u4e24\u70b9\u5782\u76f4\u91cd\u5408\uff0c\u65e0\u6cd5\u5782\u76f4\u6d4b\u91cf\uff1a\u8bf7\u6362\u70b9\u6216\u6309 W \u5207\u6362\u8ddd\u79bb/\u6c34\u5e73\u6a21\u5f0f"));  // 两点垂直重合，无法垂直测量：请换点或按 W 切换距离/水平模式
        }
        return;  // 停留在 SelectB (不重置), 用户可以换点继续或 W 切模式。
    }

    cad::param::MeasureVariable mv;
    mv.blockA = m_snapA->blockId;
    mv.pointA = m_snapA->pointId;
    mv.blockB = m_hoverSnap->blockId;
    mv.pointB = m_hoverSnap->pointId;
    mv.kind = m_kind;
    mv.value = spanValue(wa, wb);
    // Reference names are uppercase by convention (CopyChip force-uppercases
    // them for display/editing); generate uppercase so the stored refName
    // matches what the user sees and types back into formula fields.
    mv.refName = QStringLiteral("M_") + cad::param::Serial::randomPrefix().toUpper();

    // 1) Commit the measure through the undo stack (undoable). Fall back to
    //    a direct add when no stack is injected (headless unit tests).
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddMeasureCommand(m_paramDoc, mv));
    else
        m_paramDoc->addMeasure(mv);

    // 2) Result dialog (pure data in/out — it never touches the document).
    //    Accepted → write refName/name/comment via the existing
    //    SetMeasureCommand as a second, independent undo step; Rejected/Esc
    //    keeps the measure as committed and continues silently. 引用名输入框
    //    初始为空: 用户填了就用用户的 (大写), 留空保留自动生成的 M_xxx。
    QWidget* parent = (m_scene && !m_scene->views().isEmpty())
        ? static_cast<QWidget*>(m_scene->views().first()) : nullptr;
    cad::ui::MeasureResultDialog dlg(mv.value, mv.refName, QString(), QString(), m_kind, parent);
    if (dlg.exec() == QDialog::Accepted) {
        const QString newRef     = dlg.enteredRefName();
        const QString newName    = dlg.enteredName();
        const QString newComment = dlg.enteredComment();
        if (!newRef.isEmpty() || !newName.isEmpty() || !newComment.isEmpty()) {
            cad::param::MeasureVariable updated = mv;
            if (!newRef.isEmpty())     updated.refName    = newRef;
            if (!newName.isEmpty())    updated.name       = newName;
            if (!newComment.isEmpty()) updated.comment    = newComment;
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::SetMeasureCommand(m_paramDoc, updated));
            else
                m_paramDoc->updateMeasure(updated);
            mv = updated;
        }
    }

    // 3) Status feedback through the shared canvas toast channel.
    if (m_scene)
        m_scene->showToast(QStringLiteral("\xe5\xb7\xb2\xe6\xb5\x8b\xe9\x87\x8f ") + mv.refName);  // 已测量 M_xxx

    // Stay active; ready for the next measurement.
    resetToSelectA();
}

} // namespace cad::tools
