#include "ToolSmartPen.h"
#include "ToolManager.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QSet>
#include <QFontMetricsF>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QtMath>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

#include "canvas/BlockItem.h"
#include "HitTester.h"
#include "canvas/CanvasScene.h"
#include "canvas/HudItem.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Attachment.h"
#include "parametric/MeasureVariable.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "geometry/CurveMath.h"
#include "ui/QuickAuxDialog.h"
#include "LeaderCandidatePicker.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/DocumentCommands.h"

namespace cad::tools {

void ToolSmartPen::openAuxDialog(const SegmentSnapResult& segSnap, bool forStart)
{
    if (!m_paramDoc || m_auxDialog) return;
    auto* block = m_paramDoc->findBlock(segSnap.blockId);
    auto* seg = block ? block->findSegment(segSnap.segmentId) : nullptr;
    if (!block || !seg) return;

    // 端点延长线 D7 (EXTEND_LINE_DESIGN.md): 尾巴上不允许建立辅助点 —— 辅助点
    // 按本体定义, 落在尾巴上会与"比例点不随延长漂移"冲突。吸附/连接/测量仍可用。
    if (!block->segmentSnapWithinBase(segSnap.segmentId, segSnap.t)) {
        if (m_scene)
            m_scene->showToast(QString::fromUtf8(
                "请在本体范围内建立辅助点（延长尾巴上不支持建点）"));
        return;
    }

    hideSegMarker();

    // Prepared point: Interpolated defaults, percent = projection t so the
    // confirmed point lands exactly under the X marker. The name stays empty
    // — the serial is the identity (dual-track naming).
    cad::param::ParamPoint pt;
    pt.constraint = cad::param::PointConstraint::Interpolated;
    pt.hostSegmentId = seg->id;
    pt.isAuxiliary = true;
    pt.visible = true;
    pt.showName = false;
    pt.interpPercent = segSnap.t;
    pt.serial = m_paramDoc->newPointSerial();

    QWidget* parentWidget = m_scene && !m_scene->views().isEmpty()
        ? m_scene->views().first() : nullptr;
    auto* dlg = new cad::ui::QuickAuxDialog(pt, block->findPoint(seg->startPointId),
                                   block->findPoint(seg->endPointId), parentWidget);
    // NOTE: no WA_DeleteOnClose — the dialog schedules its own deleteLater()
    // on close (ElaAppBar's default close path would destroy it mid-call).
    // NON-modal on purpose: the user must be able to switch to the variable
    // panel and copy a formula while this dialog stays open.
    m_auxDialog = dlg;
    m_auxDialogForStart = forStart;
    m_auxDialogSegSnap = segSnap;
    showDialogBlockedFeedback();

    QObject::connect(dlg, &QDialog::finished, dlg, [this](int result) {
        auto* dlg = m_auxDialog.data();
        m_auxDialog = nullptr;
        clearDialogBlockedCursor();   // M10: 关闭即恢复画布光标
        if (result == QDialog::Accepted && dlg)
            onAuxDialogAccepted(dlg->point());
        // Rejected / closed: nothing created, stroke state untouched
        // (Idle stays Idle; Drawing keeps its rubber band).
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void ToolSmartPen::onAuxDialogAccepted(const cad::param::ParamPoint& pt)
{
    if (!m_paramDoc || !m_scene) return;

    auto snap = commitAuxPoint(pt, m_auxDialogSegSnap.blockId,
                               m_auxDialogSegSnap.segmentId);
    if (!snap) return;  // host segment vanished while the dialog was open

    if (m_auxDialogForStart) {
        if (m_state != State::Idle) return;  // safety: state drifted
        setupSnappedStart(*snap);
        startStroke(Qt::NoModifier);
    } else {
        if (m_state != State::Drawing) return;  // safety: state drifted
        // 起点自由 + 终点辅助点 = 翻转 (与 mousePress 终点吸附同一规则):
        // 辅助点成为新线起点, 原起点位置成为自由终点, 然后走统一的起点
        // 连接路径 —— 否则 commitLine 会退化成 createFreeLine, 连接根本
        // 不建立 (无 Attachment → 拖动保护失效, 用户双击面板手动连才生效).
        if (!m_startSnap) {
            const cad::geo::Vec2 origStart = m_startPoint;
            m_startPoint = snap->worldPos;
            m_startSnap = snap;
            m_leaderPicker->setRefDirDeg(0.0);
            commitLine(origStart, std::nullopt);
            return;
        }
        commitLine(snap->worldPos, snap);
    }
}

std::optional<SnapResult> ToolSmartPen::commitAuxPoint(
    const cad::param::ParamPoint& pt,
    const QUuid& blockId, const QUuid& segmentId)
{
    if (!m_paramDoc) return std::nullopt;
    auto* block = m_paramDoc->findBlock(blockId);
    auto* seg = block ? block->findSegment(segmentId) : nullptr;
    if (!block || !seg) return std::nullopt;

    // Own undo step (建点与建线分开撤销): the aux point belongs to the host
    // segment and survives deletion of the borrowing line.
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::AddAuxPointCommand(
            m_paramDoc, blockId, segmentId, pt));
    } else {
        block->addPoint(pt);
        seg->auxPointIds.push_back(pt.id);
        m_paramDoc->resolveAll();
    }

    // Synthesize a SnapResult on the resolved point so the normal
    // attached/bridge flow can pin to it.
    const auto* hb = m_paramDoc->findBlock(blockId);
    const auto* created = hb ? hb->findPoint(pt.id) : nullptr;
    if (!created || !created->resolved)
        return std::nullopt;
    return SnapResult{
        .worldPos  = hb->transform.toWorld(created->resolvedPos),
        .blockId   = blockId,
        .pointId   = pt.id,
        .pointName = created->name
    };
}

} // namespace cad::tools
