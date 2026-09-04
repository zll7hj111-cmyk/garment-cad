#pragma once

#include <QtTest>
#include <QApplication>
#include <QKeyEvent>
#include <QFile>
#include <QTextStream>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QUndoStack>

#include <cmath>

#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "app/ContextStrip.h"
#include "tools/ToolManager.h"
#include "tools/ToolRotate.h"
#include "tools/RotateGizmo.h"
#include "tools/ToolSelect.h"
#include "ui/LinePropertyDialog.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include "parametric/ParamDocument.h"
#include "parametric/Duplicate.h"
#include "parametric/Serial.h"
#include "geometry/Angle.h"
#include "document/commands/BlockCommands.h"
#include "document/commands/ComponentCommands.h"
#include "TestHelpers.h"

using namespace cad::param;
using cad::geo::Vec2;
using cad::test::makeLine;
using cad::test::LineSetup;
using cad::test::layerIdAt;

class StripBridge
{
public:
    StripBridge(cad::param::ParamDocument* doc, cad::tools::ToolManager& tm)
        : strip(doc)
    {
        strip.setUndoStack(nullptr);
        strip.hide();
        QObject::connect(&tm, &cad::tools::ToolManager::pinnedTargetChanged,
                         &strip, [this](const QUuid& b, const QUuid& s) {
            pinnedBlock = b;
            pinnedSeg = s;
            if (s.isNull()) strip.clearPinned();
            else            strip.setPinnedTarget(b, s, /*grabFocus=*/false);
        });
        QObject::connect(&tm, &cad::tools::ToolManager::hoverTargetChanged,
                         &strip, [this](const QUuid& b, const QUuid& s) {
            if (s.isNull()) strip.clearHover();
            else            strip.setHoverTarget(b, s);   // 80ms 节流传到 strip
        });
        QObject::connect(&tm, &cad::tools::ToolManager::hintOverrideChanged,
                         &strip, [this](const QString& h) { hint = h; });
        QObject::connect(&tm, &cad::tools::ToolManager::rotateAnchorStateChanged,
                         &strip, [this](bool active, bool anchorIsEnd, bool canToggle,
                                        const QString& reason) {
            strip.setRotateAnchorState(active, anchorIsEnd, canToggle, reason);
        });
        QObject::connect(&strip, &cad::app::ContextStrip::reverseRequested,
                         &tm, &cad::tools::ToolManager::forwardReverseRequest);
    }

    cad::app::ContextStrip strip;
    QUuid pinnedBlock;
    QUuid pinnedSeg;
    QString hint;
};

inline void sendConfirm(CanvasView& view)
{
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier,
                      view.viewport()->rect().center());
    QTest::qWait(10);
}

inline void sendKeyEsc(CanvasView& view)
{
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&view, &ev);
}

inline void sendKeyX(CanvasView& view)
{
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(&view, &ev);
}

inline double worldAngleDeg(const ParamDocument& doc, const QUuid& blockId)
{
    const Block* b = doc.findBlock(blockId);
    if (!b || b->segments.empty()) return 0.0;
    const Segment& seg = b->segments.front();
    const ParamPoint* sp = b->findPoint(seg.startPointId);
    const ParamPoint* ep = b->findPoint(seg.endPointId);
    if (!sp || !ep || !sp->resolved || !ep->resolved) return 0.0;
    const Vec2 w1 = b->transform.toWorld(sp->resolvedPos);
    const Vec2 w2 = b->transform.toWorld(ep->resolvedPos);
    return (w2 - w1).angle() * 180.0 / M_PI;
}

inline const Attachment* cloneAttachment(const ParamDocument& doc,
                                         const QUuid& cloneId, const QUuid& originalId)
{
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == cloneId && a.toBlockId == originalId && !a.isPin)
            return &a;
    return nullptr;
}

inline const Attachment* followerAttachmentOf(const ParamDocument& doc,
                                              const QUuid& followerId)
{
    for (const auto& a : doc.attachments())
        if (a.fromBlockId == followerId && !a.isPin)
            return &a;
    return nullptr;
}

inline Attachment attachCloneToOriginal(ParamDocument& doc, const Block& clone,
                                        const QUuid& originalId,
                                        const LineSetup& orig, double followerAngle)
{
    Attachment att;
    att.fromBlockId = clone.id;
    att.fromPointId = clone.points.front().id;
    att.toBlockId = originalId;
    att.toPointId = orig.startId;
    att.toSegmentId = orig.segId;
    att.followerAngle = followerAngle;
    att.rotationMode = RotationMode::Angle;
    return att;
}

class TestRotateCopy : public QObject
{
    Q_OBJECT

private slots:
    // ── 模型层 ──
    void cloneAttachesToOriginalWithRelativeAngle();
    void cloneFollowsOriginalRotation();
    void rotateCopyCommandUndoRedo();
    void formulaLockedOriginalCopyIsFree();

    // ── 影子角度通道 (拆开影子基准 R6/R8, DETACH_SHADOW_DESIGN.md §7.2) ──
    void shadowChannel_formulaLockRotatesShadowKeepsP3();

    // ── UI 层完整事件链 ──
    void ctrlDragRotateCopyCommits();
    void diagonalFreeLineCopyOverlapsOriginal();
    void ctrlDragZeroAngleDiscards();
    void escCancelsCopy();
    void consecutiveCopiesAllAttachToOriginal();
    /// 复制提交后条带锁定回原线段 (原 rotateCopyCommitHidesHud —— 旋转工具
    /// 不再持有 HUD, 同一条防呆改由"复制期间解除锁定、提交后锁回原线"承担)。
    void rotateCopyCommitRestoresStripTarget();

    // ── 公式锁定跟随线 ──
    void lockedFollowerRotationBakesFormula();
    void lockedFollowerRotateCopyWorks();
    void propertyDialogShowsFollowValue();

    // ── 终点指向 (endTarget) 锁定 ──
    void endTargetRotationReleasesAim();
    void endTargetRotateCopyDropsAim();
    void endTargetRotateCopyKeepsOriginalAim();
    void endTargetRotateCopyIdleGestureKeepsOriginalAim();
    void endTargetRotateCopyUndoRedoKeepsOriginalAim();
    void midGestureCtrlConvertsToCopy();

    // ── 锚心切换 (起点 ↔ 终点) ──
    void xToggleSwitchesAnchorToEndPoint();
    void clickEndPointSwitchesAnchor();
    void connectedLineXAnchorSwitchBlocked();
    void independentAngleLineRotatesBlockKeepsPin();
    void endAnchorRotateCopyAttachesToEnd();
    void endAnchorLineFollowsCursor();
    /// 条带「换向」在旋转会话内 = 切换锚心 (2026-12): 转交 ToolRotate 切锚心,
    /// gizmo pivot 环随锚心移动; 连接线换向 = no-op (与 X 键同守卫)。
    void stripReverseTogglesAnchor();
    /// 锚心切换 (换向/点端点) 全链路同步条带 (2026-12): 选中即进旋转会话
    /// (基准读数锚心端在前 + 角度字段随锚心基准 ±180°), 换向/点端点切换时
    /// 条带基准与角度随之翻转, 状态栏提示带锚心确定信息。
    void anchorSwitchSyncsStrip();

    // ── 单位切换 (角度 ↔ 弧长) 与多圈归一化 —— 判据走上下文属性条 ──
    void modeSwitchKeepsFormula();
    void angleModeOverflowNormalized();
    void arcLengthModeOverflowNormalized();

    // ── 公式线角度格显示公式原文 (2026-12 统一, 用户报告 "用了变量参数,
    // 显示的是换算的数值") —— 判据从 HUD 迁移到 ContextStrip 角度格 ──
    void stripShowsFormulaForFormulaDrivenAngle();
    void stripShowsFormulaForFormulaDrivenArc();

    // ── D15 单线确认流 (用户拍板 2026-08-27) ──
    void d15GateRequiresConfirmBeforeDrag();    // 选中态按住拖动 = no-op; 确认后才能转
    void d15DragCommitDropsToSelected();        // 提交后回落选中态, 再拖需再确认
    void d15BlankClickClearsSelectedTarget();   // 选中态点空白 = 取消选择
    void d15AnchorFollowsClickedEnd();          // 锚心跟随点击端 (自由线近端; 连接线恒取挂接端)

    // ── TOOL_SYSTEM_AUDIT P0 (2026-08-29) ──
    /// H2: 确认门有可视 —— gizmo 样式 + 状态栏提示 (原第三条表达是 HUD
    /// caption 后缀, 一期随 AngleHud 退场迁入状态栏)。
    void d15ConfirmStateHasVisualAndHint();

    // ── 旋转复制快捷键与对齐点/参数自动发布 (2026-09) ──
    void rotateCopyAutoPublishesParentParameter();
    void rotateCopyFourStepFlow();
    void dragJitterDoesNotReleaseUntilPhysicalRelease();
    void gizmoDisplayConsistencyAcrossModes();

    // ── 多选框选与端点锚心旋转 (2026-09) ──
    void marqueeSelectionAndPivotSnapRotate();
    void adoptSelectionFromSelectToolAndRotate();
};

