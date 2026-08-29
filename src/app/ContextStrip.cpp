#include "ContextStrip.h"

#include <cmath>

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTimer>
#include <QKeyEvent>
#include <QStyle>
#include <QUndoStack>
#include <QAbstractButton>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"

#include "ui/Theme.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/Attachment.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "geometry/Units.h"
#include "geometry/Angle.h"
#include "document/commands/BlockCommands.h"

namespace cad::app {
namespace {

/// 悬停节流窗口 (CONTEXT_STRIP_DESIGN.md §4.2): 鼠标扫过一堆线段时条带不狂闪。
constexpr int kHoverThrottleMs = 80;
/// 长度/角度输入的防抖窗口 (与属性对话框同节奏)。
constexpr int kDebounceMs = 200;

} // namespace

ContextStrip::ContextStrip(cad::param::ParamDocument* paramDoc, QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(paramDoc)
{
    buildUi();

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        if (m_lenEdit->hasFocus()) applyLength();
        if (m_angleEdit->hasFocus()) applyAngle();
    });

    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(kHoverThrottleMs);
    connect(m_hoverTimer, &QTimer::timeout, this, &ContextStrip::flushHover);

    // 模型变了就回填 —— 旋转拖动每帧走 resolveForDrag, 它末尾照样 emit resolved
    // (ParamDocumentResolver.cpp:271 → :667), 所以角度格会跟着拖动实时变化,
    // 不需要宿主转发, 也不需要每帧刷状态栏 (避免 QStatusBar 反复 relayout)。
    if (m_paramDoc) {
        connect(m_paramDoc, &cad::param::ParamDocument::resolved,
                this, [this] { if (m_focus != StripFocus::Empty && !m_strokePreview) {
                                   refreshFields();
                                   refreshChrome();
                               } });
    }

    hideBar();
}

void ContextStrip::buildUi()
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    // 串号徽章: 等宽, 图纸编号感 (配色走全局 QSS QLabel#serialBadge)。
    m_idLabel = new ElaText(QString(), 12, this);
    m_idLabel->setObjectName(QStringLiteral("serialBadge"));
    m_idLabel->setStyleSheet(QStringLiteral("%1 font-size: 11px;")
                                 .arg(cad::ui::ThemeTokens::kMonospaceFamily));
    lay->addWidget(m_idLabel);

    // 字段标签只用于排版, 不需要持有 (文本恒定)。
    auto addField = [this, lay](const QString& caption, ElaLineEdit*& edit, int width,
                                const QString& placeholder) {
        auto* label = new ElaText(caption, 12, this);
        label->setObjectName(QStringLiteral("mutedText"));
        lay->addWidget(label);
        edit = new ElaLineEdit(this);
        edit->setPlaceholderText(placeholder);
        edit->setMaximumWidth(width);
        edit->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);
        lay->addWidget(edit);
    };

    addField(QString::fromUtf8("名称:"), m_nameEdit, 150,
             QString::fromUtf8("线段名称"));
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    addField(QString::fromUtf8("长度(cm):"), m_lenEdit, 110,
             QString::fromUtf8("数值或公式"));
    m_lenEdit->setObjectName(QStringLiteral("lenEdit"));
    addField(QString::fromUtf8("角度:"), m_angleEdit, 100,
             QString::fromUtf8("数值或公式"));
    m_angleEdit->setObjectName(QStringLiteral("angleEdit"));

    // 单位分段 (° | ⌒): 写 attachment.rotationMode。checkable + 互斥，
    // 选中态交给 Ela 主题表现，不硬编码颜色。
    auto* unitBox = new QWidget(this);
    auto* unitLay = new QHBoxLayout(unitBox);
    unitLay->setContentsMargins(0, 0, 0, 0);
    unitLay->setSpacing(0);
    m_btnUnitAngle = new ElaPushButton(QString::fromUtf8("°"), unitBox);
    m_btnUnitArc = new ElaPushButton(QString::fromUtf8("⌒"), unitBox);
    for (auto* b : {m_btnUnitAngle, m_btnUnitArc}) {
        b->setObjectName(QStringLiteral("unitSegment"));
        b->setCheckable(true);
        b->setFixedSize(38, 35);
        b->setCursor(Qt::PointingHandCursor);
        unitLay->addWidget(b);
    }
    m_btnUnitAngle->setToolTip(QString::fromUtf8("角度模式（度）"));
    m_btnUnitArc->setToolTip(QString::fromUtf8("弧长模式（cm）"));
    connect(m_btnUnitAngle, &QAbstractButton::clicked,
            this, [this] { onUnitToggled(false); });
    connect(m_btnUnitArc, &QAbstractButton::clicked,
            this, [this] { onUnitToggled(true); });
    lay->addWidget(unitBox);

    m_btnReverse = new ElaPushButton(QString::fromUtf8("换向"), this);
    m_btnReverse->setObjectName(QStringLiteral("reverseBtn"));
    m_btnReverse->setFixedSize(58, 35);
    m_btnReverse->setCursor(Qt::PointingHandCursor);
    connect(m_btnReverse, &QAbstractButton::clicked,
            this, &ContextStrip::onReverseClicked);
    lay->addWidget(m_btnReverse);

    m_btnBasis = new ElaPushButton(QString::fromUtf8("起点 → 终点"), this);
    m_btnBasis->setObjectName(QStringLiteral("basisBtn"));
    m_btnBasis->setFixedSize(110, 35);
    m_btnBasis->setToolTip(
        QString::fromUtf8("角度基准：起点 → 终点（换向后驱动另一端）"));
    lay->addWidget(m_btnBasis);

    m_badge = new ElaText(QString(), 12, this);
    m_badge->setObjectName(QStringLiteral("dimText"));
    lay->addWidget(m_badge);

    lay->addStretch();

    m_hint = new ElaText(QString(), 12, this);
    m_hint->setObjectName(QStringLiteral("dimText"));
    lay->addWidget(m_hint);

    // 名称立即应用; 长度 200ms 防抖 + Enter/失焦立即应用。
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this] { applyName(); });
    connect(m_lenEdit, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });
    connect(m_lenEdit, &QLineEdit::editingFinished, this, [this] { applyLength(); });
    // 角度: 连接角度会话 (二期) 下击键直通手势实时预览, 不 debounce 不 push
    // 命令; 普通锁定态 200ms 防抖 + Enter/失焦立即应用。
    connect(m_angleEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (m_connectSession) {
            emit connectAngleTextChanged(text);
            return;
        }
        m_debounce->start();
    });
    connect(m_angleEdit, &QLineEdit::editingFinished, this, [this] { applyAngle(); });

    for (auto* edit : {m_nameEdit, m_lenEdit, m_angleEdit})
        edit->installEventFilter(this);
}

void ContextStrip::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
    if (!stack) return;
    // undo/redo 条带提交后必须重新回填, 否则条带停留在已撤销的值上。
    connect(stack, &QUndoStack::indexChanged, this, [this](int) {
        if (m_focus == StripFocus::Empty || m_strokePreview) return;
        if (m_paramDoc && m_paramDoc->findBlock(m_blockId)) {
            refreshFields();
            refreshChrome();
        }
    });
}

void ContextStrip::setCanvasView(QWidget* canvasView)
{
    m_canvasView = canvasView;
}

// ─────────────────────────────────────────────────────────────────────────────
// 连接角度会话 (CONTEXT_STRIP_DESIGN.md 二期): 连接手势的角度输入并入条带,
// 浮动 AngleHud 整体退场。条带是纯输入面 —— 击键/单位切换/Enter/Esc 全部经
// 信号回传宿主 (MainWindow → ToolManager → 选择工具 → ConnectGesture),
// 连接语义 (预览/换算/收尾/undo) 留在手势里。
// ─────────────────────────────────────────────────────────────────────────────

void ContextStrip::beginConnectAngleSession(const QUuid& blockId, const QUuid& segmentId,
                                            const QUuid& attachmentId, double initialAngle)
{
    if (attachmentId.isNull()) { endConnectAngleSession(); return; }
    const auto* blk = m_paramDoc ? m_paramDoc->findBlock(blockId) : nullptr;
    const auto* seg = blk ? blk->findSegment(segmentId) : nullptr;
    if (!blk || !seg) return;

    m_blockId = blockId;
    m_segmentId = segmentId;
    m_focus = StripFocus::Pinned;
    m_creationPinned = false;
    m_strokePreview = false;
    m_connectSession = true;
    m_connectAttId = attachmentId;
    m_connectInitialAngle = initialAngle;
    m_hoverTimer->stop();

    // 会话内仅角度可编辑 (名称/长度只读展示 —— 编辑会 push 命令, 破坏手势
    // 的整步 undo 宏)。
    setReadOnlyFields(true);
    m_angleEdit->setReadOnly(false);
    setConnectAngleValid(true);
    refreshFields();
    refreshChrome();
    show();
    m_angleEdit->setFocus();
    m_angleEdit->selectAll();   // 输入即替换 (同旧 HUD)
}

void ContextStrip::endConnectAngleSession()
{
    if (!m_connectSession) return;
    m_connectSession = false;
    m_connectAttId = QUuid();
    m_connectInitialAngle = 0.0;
    hideBar();
    returnFocusToCanvas();
}

void ContextStrip::setConnectAngleValid(bool valid)
{
    if (!m_connectSession) return;
    const bool invalid = !valid;
    if (m_angleEdit->property("angleInvalid").toBool() == invalid) return;  // 同值短路
    m_angleEdit->setProperty("angleInvalid", invalid);
    m_angleEdit->style()->unpolish(m_angleEdit);
    m_angleEdit->style()->polish(m_angleEdit);
}

// ─────────────────────────────────────────────────────────────────────────────
// 焦点上报
// ─────────────────────────────────────────────────────────────────────────────

void ContextStrip::setHoverTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_hoverBlock = blockId;
    m_hoverSegment = segmentId;
    // 节流窗口内只更新候选, 不重置计时 —— 持续移动时也每 80ms 结算一次,
    // 且结算的总是窗口末的最后一个候选。
    if (!m_hoverTimer->isActive())
        m_hoverTimer->start();
}

void ContextStrip::flushHover()
{
    if (m_focus == StripFocus::Pinned || m_strokePreview) return;   // 不抢锁定显示
    if (inputHasFocus()) return;                                    // 焦点保护

    const bool have = !m_hoverBlock.isNull() && !m_hoverSegment.isNull();
    const auto* blk = (have && m_paramDoc) ? m_paramDoc->findBlock(m_hoverBlock) : nullptr;
    const auto* seg = blk ? blk->findSegment(m_hoverSegment) : nullptr;
    if (!blk || !seg) { hideBar(); return; }

    m_blockId = m_hoverBlock;
    m_segmentId = m_hoverSegment;
    m_focus = StripFocus::Hover;
    m_creationPinned = false;
    m_strokePreview = false;
    setReadOnlyFields(true);
    refreshFields();
    refreshChrome();
    show();
}

void ContextStrip::setPinnedTarget(const QUuid& blockId, const QUuid& segmentId,
                                   bool grabFocus)
{
    // 连接角度会话期间条带显示由会话独占 (宿主驱动), 普通锁定上报一律忽略。
    if (m_connectSession) return;
    if (blockId.isNull() || segmentId.isNull()) { clearPinned(); return; }
    const auto* blk = m_paramDoc ? m_paramDoc->findBlock(blockId) : nullptr;
    const auto* seg = blk ? blk->findSegment(segmentId) : nullptr;
    if (!blk || !seg) return;

    m_blockId = blockId;
    m_segmentId = segmentId;
    m_focus = StripFocus::Pinned;
    m_creationPinned = false;
    m_strokePreview = false;
    m_editStartIndex = m_undoStack ? m_undoStack->index() : 0;
    setReadOnlyFields(false);
    refreshFields();
    refreshChrome();
    show();
    if (grabFocus) {
        m_nameEdit->setFocus();
        m_nameEdit->selectAll();
    }
}

void ContextStrip::clearHover()
{
    m_hoverBlock = QUuid();
    m_hoverSegment = QUuid();
    if (m_focus == StripFocus::Hover) hideBar();
}

void ContextStrip::clearPinned()
{
    if (m_focus != StripFocus::Pinned) return;
    m_focus = StripFocus::Empty;
    m_creationPinned = false;
    m_connectSession = false;
    m_connectAttId = QUuid();
    m_blockId = QUuid();
    m_segmentId = QUuid();
    // 回落到悬停候选 (有) 或隐藏。
    if (!m_hoverBlock.isNull() && !m_hoverSegment.isNull()) {
        m_hoverTimer->stop();
        flushHover();
    } else {
        hideBar();
    }
}

void ContextStrip::pinCreatedLine(const QUuid& blockId, const QUuid& segmentId,
                                  bool grabFocus)
{
    setPinnedTarget(blockId, segmentId, grabFocus);
    m_creationPinned = true;   // Esc = 撤销创建 (宿主接 cancelRequested)。
}

void ContextStrip::showStrokePreview(double lenCm, double angleDeg)
{
    // 0,0 = 落笔取消: 收起读数。
    if (std::abs(lenCm) < 1e-9 && std::abs(angleDeg) < 1e-9) { hideBar(); return; }

    m_strokePreview = true;
    m_focus = StripFocus::Empty;
    m_blockId = QUuid();
    m_segmentId = QUuid();
    m_idLabel->setText(QString::fromUtf8("新线"));

    const QSignalBlocker nb(m_nameEdit);
    m_nameEdit->clear();
    setReadOnlyFields(true);
    // 数值回显去尾零 (2026-12 用户拍板: 不要 .00/45.0)。
    m_lenEdit->setText(cad::geo::Units::formatNumberTrimmed(lenCm));
    m_angleEdit->setText(cad::geo::Units::formatDegValue(
        cad::geo::normalizeDeg360(angleDeg)));
    m_btnUnitAngle->setChecked(true);
    m_btnUnitArc->setChecked(false);
    m_btnUnitAngle->setEnabled(false);
    m_btnUnitArc->setEnabled(false);
    m_btnReverse->setEnabled(false);
    m_btnBasis->setText(QString::fromUtf8("起点 → 终点"));
    m_badge->setText(QString::fromUtf8("绘制中"));
    m_hint->setText(QString::fromUtf8("Esc 取消 · 落点后自动锁定"));
    show();
}

void ContextStrip::hideBar()
{
    m_hoverTimer->stop();
    if (m_focus == StripFocus::Pinned && !m_creationPinned) {
        // 外部直接隐藏 (工具切换等): 连锁定一起解除, 避免残留不可见的焦点。
        m_focus = StripFocus::Empty;
        m_blockId = QUuid();
        m_segmentId = QUuid();
    } else if (m_focus == StripFocus::Hover) {
        // 悬停态隐藏 = 移出: 焦点归一化为 Empty —— "Hover" 语义是条带可见
        // 的只读预览, 隐藏后不得残留悬停焦点 (三期只读悬停移出即收起)。
        m_focus = StripFocus::Empty;
        m_blockId = QUuid();
        m_segmentId = QUuid();
    }
    m_strokePreview = false;
    m_creationPinned = false;
    m_connectSession = false;   // 会话随条带隐藏一起结束 (宿主同步调用 endConnectAngleSession)
    m_connectAttId = QUuid();
    m_rotateSession = false;    // 旋转锚心会话随条带隐藏一起结束 (工具 deactivate 两路幂等)
    m_rotateAnchorIsEnd = false;
    m_rotateCanToggle = false;
    m_rotateReason.clear();
    hide();
}

void ContextStrip::cancelCreation()
{
    // 回退到创建点: 撤销创建命令 + 自那以后推入的每一条条带编辑 (线消失)。
    if (m_undoStack && m_undoStack->index() > m_editStartIndex)
        m_undoStack->setIndex(m_editStartIndex);
    m_creationPinned = false;
    hideBar();
}

bool ContextStrip::readOnly() const
{
    return m_nameEdit->isReadOnly();
}

// ─────────────────────────────────────────────────────────────────────────────
// 回填
// ─────────────────────────────────────────────────────────────────────────────

void ContextStrip::refreshFields()
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    m_idLabel->setText(cad::param::Serial::tag(seg->serial));

    // 焦点保护: 正在输入的字段不回填, 否则旋转拖动或模型广播会打断敲击。
    if (!m_nameEdit->hasFocus()) {
        const QSignalBlocker nb(m_nameEdit);
        m_nameEdit->setText(seg->name);
    }

    if (!m_lenEdit->hasFocus()) {
        const QSignalBlocker lb(m_lenEdit);
        if (!seg->lengthFormula.isEmpty()) {
            m_lenEdit->setText(seg->lengthFormula);
        } else {
            const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
            const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
            if (sp && ep && sp->resolved && ep->resolved) {
                const double mm = sp->resolvedPos.distanceTo(ep->resolvedPos);
                m_lenEdit->setText(cad::geo::Units::formatNumberTrimmed(
                    cad::geo::Units::mmToCm(mm)));
            } else {
                m_lenEdit->clear();
            }
        }
    }

    if (!m_angleEdit->hasFocus()) {
        const QSignalBlocker ab(m_angleEdit);
        if (const cad::param::Attachment* att = findEditAttachment()) {
            if (att->rotationMode == cad::param::RotationMode::ArcLength) {
                m_angleEdit->setText(att->arcLengthFormula.isEmpty()
                    ? foldedArcDisplay(att)
                    : att->arcLengthFormula);
            } else {
                // 存储 α ∈ [0,360) → 显示带符号折角（2026-08 v3 定稿，
                // 与旧旋转 HUD currentAngleDeg 同解）。自由线分支在 else 里
                // 仍显示绝对世界角（normalizeDeg360）。
                m_angleEdit->setText(att->followerAngleFormula.isEmpty()
                    ? cad::geo::Units::formatDegValue(
                          cad::geo::normalizeDeg180(att->followerAngle))
                    : att->followerAngleFormula);
            }
        } else if (const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId)) {
            if (!ep->angleFormula.isEmpty()) {
                m_angleEdit->setText(ep->angleFormula);
            } else {
                const double rotDeg = block->transform.rotation * 180.0 / M_PI;
                double worldDeg = cad::geo::normalizeDeg360(ep->angle + rotDeg);
                // 旋转会话 (2026-12): 自由线角度显示跟随锚心基准 —— 锚在终点
                // 时基准 = 终点→起点方向, 显示 = 模型方向 + 180° (与旋转工具
                // currentAngleDeg 同解); 换向/点端点切锚心时读数随之翻转。
                if (m_rotateSession && m_rotateAnchorIsEnd)
                    worldDeg = cad::geo::normalizeDeg360(worldDeg + 180.0);
                m_angleEdit->setText(cad::geo::Units::formatDegValue(worldDeg));
            }
        } else {
            m_angleEdit->clear();
        }
    }
}

/// 弧长模式的显示值 = 带符号折角弧长 (cm)（2026-08 v3 定稿，与旧旋转 HUD
/// currentModeValue 同解）：先把多圈弧长按恒等映射落到 ±180° 侧，再换算
/// 弧长 cm —— 多圈不爆表（3 圈 ≡ 0° 折叠显示 0cm）。
QString ContextStrip::foldedArcDisplay(const cad::param::Attachment* att) const
{
    if (!att || !m_paramDoc || m_blockId.isNull()) return QStringLiteral("0");
    const auto* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk) return QStringLiteral("0");
    const double radius = blk->segmentLengthAtPoint(att->fromPointId);
    const double alphaDeg = (radius > 1e-9)
        ? cad::geo::arcMmToDeg(att->arcLength, radius) : 0.0;
    const double foldDeg = cad::geo::normalizeDeg180(alphaDeg);
    return cad::geo::Units::formatDegValue(
        cad::geo::Units::mmToCm(cad::geo::degToArcMm(foldDeg, radius)));
}

void ContextStrip::refreshChrome()
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const cad::param::Attachment* att = findEditAttachment();
    const bool isBridge = block->isBridge || !seg->lengthFormula.isEmpty();

    // 桥线段只读 (设计 §2.3): 桥线长度 = 测量变量、角度 = 被动值, 绝不覆写
    // 测量链接 —— 长度/角度置灰, 名称仍可编辑 (与旧 SegmentEditBar 同规则)。
    // 连接角度会话 (二期): 仅角度可编辑, 名称/长度只读展示。
    if (m_connectSession) {
        m_nameEdit->setReadOnly(true);
        m_lenEdit->setReadOnly(true);
        m_angleEdit->setReadOnly(false);
    } else if (m_focus == StripFocus::Pinned) {
        m_lenEdit->setReadOnly(isBridge);
        m_angleEdit->setReadOnly(isBridge);
    }

    // 单位段: 弧长模式只有跟随连接才有意义; 自由线/桥线禁用。
    const bool arc = att && att->rotationMode == cad::param::RotationMode::ArcLength;
    m_btnUnitAngle->setChecked(!arc);
    m_btnUnitArc->setChecked(arc);
    const bool unitEnabled = (att != nullptr) && m_focus == StripFocus::Pinned;
    m_btnUnitAngle->setEnabled(unitEnabled);
    m_btnUnitArc->setEnabled(unitEnabled);

    // 换向: 命令内资格检查为权威, 这里只做预判 (置灰 + 中文原因)。
    // 连接角度会话 (二期) 禁用换向 —— 换向会 push ReverseSegmentCommand,
    // 破坏手势整步 undo 宏。
    // 旋转会话 (2026-12): 换向 = 切换锚心, 资格由工具上报 (Ready 且无连接),
    // 点击转发给工具 (ToolRotate::onReverseRequested), 不 push 命令。
    QString reason;
    bool canRev;
    if (m_rotateSession) {
        canRev = m_rotateCanToggle && m_focus == StripFocus::Pinned;
        reason = canRev
            ? QString::fromUtf8("切换锚心（起点 ↔ 终点）：旋转将绕另一端，画布箭头随之翻转")
            : (m_rotateReason.isEmpty()
                   ? QString::fromUtf8("当前状态不可切换锚心")
                   : m_rotateReason);
    } else {
        canRev = m_focus == StripFocus::Pinned
                 && !m_connectSession
                 && cad::cmd::ReverseSegmentCommand::canReverse(
                        m_paramDoc, m_blockId, m_segmentId, &reason);
        if (canRev)
            reason = QString::fromUtf8("交换起点/终点身份：换向后修改长度/角度驱动另一端，几何位置不变");
    }
    m_btnReverse->setEnabled(canRev);
    m_btnReverse->setToolTip(reason);

    // 状态徽标: 一眼看出这条线受谁驱动。
    if (isBridge) {
        m_badge->setText(QString::fromUtf8("桥线"));
    } else if (seg->isCurve()) {
        m_badge->setText(QString::fromUtf8("曲线"));
    } else if (att) {
        const auto* leader = m_paramDoc->findBlock(att->toBlockId);
        const auto* leaderSeg = leader ? leader->findSegment(att->toSegmentId) : nullptr;
        m_badge->setText(leaderSeg
            ? QString::fromUtf8("跟随 %1").arg(cad::param::Serial::tag(leaderSeg->serial))
            : QString::fromUtf8("跟随"));
    } else {
        m_badge->setText(QString::fromUtf8("自由"));
    }

    // 角度基准读数 (换向按钮已承担切换职能, 这里是只读说明)。
    // 2026-12: 显示真实串号 tag (与属性对话框 SegmentAngleCard 同口径, 换向
    // 后翻转); 旋转会话内锚心端在前 (换向 = 切换锚心, 读数随锚心走)。
    const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
    const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
    const auto tagOf = [](const cad::param::ParamPoint* p) {
        return p ? cad::param::Serial::tag(p->serial)
                 : QStringLiteral("?");
    };
    m_btnBasis->setText(m_rotateSession
        ? QString::fromUtf8("%1 → %2").arg(
              tagOf(m_rotateAnchorIsEnd ? ep : sp),
              tagOf(m_rotateAnchorIsEnd ? sp : ep))
        : QString::fromUtf8("%1 → %2").arg(tagOf(sp), tagOf(ep)));
    m_btnBasis->setToolTip(m_rotateSession
        ? QString::fromUtf8("锚心：旋转支点所在端（换向 = 切换锚心）")
        : QString::fromUtf8("角度基准：起点 → 终点（换向后驱动另一端）"));

    // 描边与提示: 悬停 = 虚线只读, 锁定 = 实线可编辑 (QSS 由宿主主题提供)。
    setProperty("stripFocus", m_focus == StripFocus::Pinned ? "pinned" : "hover");
    style()->unpolish(this);
    style()->polish(this);
    m_hint->setText(m_connectSession
        ? QString::fromUtf8("Enter 确认 · Esc 取消")
        : m_focus == StripFocus::Pinned
            ? QString::fromUtf8("Esc 解除锁定 · Enter 确认")
            : QString::fromUtf8("点击线段锁定后可编辑"));
}

void ContextStrip::setReadOnlyFields(bool readOnly)
{
    m_nameEdit->setReadOnly(readOnly);
    m_lenEdit->setReadOnly(readOnly);
    m_angleEdit->setReadOnly(readOnly);
}

// ─────────────────────────────────────────────────────────────────────────────
// 编辑应用
// ─────────────────────────────────────────────────────────────────────────────

void ContextStrip::applyName()
{
    if (!m_paramDoc || m_blockId.isNull() || m_strokePreview) return;
    if (m_focus != StripFocus::Pinned || m_connectSession) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block || !block->findSegment(m_segmentId)) return;

    auto st = snapshotState();
    st.segName = m_nameEdit->text().trimmed();
    commitState(std::move(st));
}

void ContextStrip::applyLength()
{
    if (!m_paramDoc || m_blockId.isNull() || m_strokePreview) return;
    if (m_focus != StripFocus::Pinned || m_connectSession) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    // 桥线长度 = 测量变量, 被动值 —— 绝不覆写测量链接。
    if (!seg->lengthFormula.isEmpty()) return;

    const QString text = m_lenEdit->text().trimmed();
    if (text.isEmpty()) return;
    if (!block->findPoint(seg->endPointId)) return;

    auto st = snapshotState();
    const auto parsed = cad::geo::parseNumberOrFormula(text);
    if (parsed.isNumber) {
        st.lengthFormula.clear();
        st.endDistanceFormula.clear();
        st.endDistance = cad::geo::Units::cmToMm(parsed.value);
    } else {
        st.lengthFormula = parsed.formula;
        st.endDistanceFormula = parsed.formula;
    }
    commitState(std::move(st));
}

void ContextStrip::applyAngle()
{
    if (!m_paramDoc || m_blockId.isNull() || m_strokePreview) return;
    if (m_focus != StripFocus::Pinned || m_connectSession) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;
    if (!seg->lengthFormula.isEmpty()) return;   // 桥线: 角度同样被动

    const QString text = m_angleEdit->text().trimmed();
    if (text.isEmpty()) return;

    const auto parsed = cad::geo::parseNumberOrFormula(text);
    double targetDeg = parsed.value;
    if (!parsed.isNumber) {
        auto r = cad::param::ConditionEngine::evaluate(
            parsed.formula, m_paramDoc->parameters(), {});
        if (!r.ok) return;                        // 无效: 保留最后一次有效几何
        targetDeg = r.value;
    }

    auto st = snapshotState();
    if (const cad::param::Attachment* att = findEditAttachment()) {
        st.attId = att->id;
        if (att->rotationMode == cad::param::RotationMode::ArcLength) {
            st.arcLength = cad::geo::Units::cmToMm(targetDeg);
            st.arcLengthFormula = parsed.isNumber ? QString() : parsed.formula;
        } else {
            st.followerAngle = targetDeg;
            st.followerAngleFormula = parsed.isNumber ? QString() : parsed.formula;
        }
    } else {
        auto* ep = block->findPoint(seg->endPointId);
        if (!ep) return;
        if (ep->constraint != cad::param::PointConstraint::Polar) {
            const auto* sp = block->findPoint(seg->startPointId);
            if (!sp || !sp->resolved || !ep->resolved) return;
            st.endConstraint = static_cast<int>(cad::param::PointConstraint::Polar);
            st.endRefPointId = seg->startPointId;
            st.endDistance = sp->resolvedPos.distanceTo(ep->resolvedPos);
        }
        // 存储角是局部角 (相对块旋转); 字段显示世界角 —— 与属性对话框同补偿。
        // 旋转会话 (2026-12): 字段显示锚心基准角 (锚在终点 = 模型方向 +180),
        // 输入回写必须减回偏移 —— 否则用户按条带读数输入, 线会偏 180°。
        const double rotDeg = block->transform.rotation * 180.0 / M_PI;
        const double anchorOffset = (m_rotateSession && m_rotateAnchorIsEnd) ? 180.0 : 0.0;
        st.endAngle = (targetDeg - anchorOffset) - rotDeg;
        st.endAngleFormula.clear();
    }
    commitState(std::move(st));
}

void ContextStrip::onUnitToggled(bool wantArc)
{
    // 连接角度会话 (二期): 单位切换经信号回传手势 (几何保持换算、公式驱动
    // 拒绝切换都在手势侧); emit 后立即 refreshChrome 兜底 —— 被拒时按钮弹回
    // 原模式 (拒绝路径不触发 resolved, 否则按钮会停在错误的一侧)。
    if (m_connectSession) {
        if (m_connectAttId.isNull()) return;
        const auto target = wantArc ? cad::param::RotationMode::ArcLength
                                    : cad::param::RotationMode::Angle;
        emit connectAngleModeChanged(target);
        refreshChrome();
        return;
    }
    if (m_focus != StripFocus::Pinned || !m_paramDoc || m_blockId.isNull()) return;
    const cad::param::Attachment* att = findEditAttachment();
    if (!att) return;   // 自由线无 rotationMode 概念
    const auto target = wantArc ? cad::param::RotationMode::ArcLength
                                : cad::param::RotationMode::Angle;
    if (att->rotationMode == target) return;

    // 公式驱动（角度/弧长表达式）：模式切换只是显示单位变化，绝不换算
    // 烘焙公式 —— 表达式必须原样保留（与 LinePropertyDialog 同规则; 原
    // ToolRotate::onHudModeChanged 的同一条保护，一期随 HUD 退场迁到这里）。
    const bool hasFormula = (att->rotationMode == cad::param::RotationMode::ArcLength)
        ? !att->arcLengthFormula.isEmpty()
        : !att->followerAngleFormula.isEmpty();
    if (hasFormula) {
        refreshChrome();   // 按钮弹回原模式
        return;
    }

    // 几何保持切换 (旧旋转 HUD onHudModeChanged 同解): 切换必须按存储域
    // α 换算值, 不能只翻单位 —— 否则弧长 3 圈 (≡0° 折叠) 切到角度会显示
    // 错误数值。当前显示角 = 带符号折角, 先回存储域 [0,360) 再换算。
    const double radius = m_paramDoc->findBlock(m_blockId)
                              ? m_paramDoc->findBlock(m_blockId)
                                    ->segmentLengthAtPoint(att->fromPointId)
                              : 0.0;
    const double curFoldDeg = (att->rotationMode
                                   == cad::param::RotationMode::ArcLength)
        ? ((radius > 1e-9) ? cad::geo::normalizeDeg180(
                                 cad::geo::arcMmToDeg(att->arcLength, radius))
                           : 0.0)
        : cad::geo::normalizeDeg180(att->followerAngle);
    const double alpha = cad::geo::normalizeDeg360(curFoldDeg);

    auto st = snapshotState();
    st.attId = att->id;
    st.rotationMode = static_cast<int>(target);
    if (target == cad::param::RotationMode::ArcLength) {
        st.arcLength = cad::geo::degToArcMm(alpha, radius);
        st.arcLengthFormula.clear();
    } else {
        st.followerAngle = alpha;
        st.followerAngleFormula.clear();
    }
    commitState(std::move(st));
}

void ContextStrip::onReverseClicked()
{
    if (m_focus != StripFocus::Pinned || !m_paramDoc || m_blockId.isNull()) return;
    // 旋转会话 (2026-12): 换向 = 切换锚心 —— 转发给激活工具 (ToolRotate 切
    // 锚心 + gizmo pivot 环移动), 绝不 push ReverseSegmentCommand。
    if (m_rotateSession) {
        emit reverseRequested(m_blockId, m_segmentId);
        return;
    }
    QString reason;
    if (!cad::cmd::ReverseSegmentCommand::canReverse(
            m_paramDoc, m_blockId, m_segmentId, &reason))
        return;
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::ReverseSegmentCommand(
            m_paramDoc, m_blockId, m_segmentId));
    } else {
        cad::cmd::ReverseSegmentCommand cmd(m_paramDoc, m_blockId, m_segmentId);
        cmd.redo();
    }
}

void ContextStrip::setRotateAnchorState(bool active, bool anchorIsEnd,
                                        bool canToggle, const QString& reason)
{
    m_rotateSession = active;
    m_rotateAnchorIsEnd = anchorIsEnd;
    m_rotateCanToggle = canToggle;
    m_rotateReason = reason;
    if (m_focus != StripFocus::Empty) {
        // 锚心切换也要刷新角度字段 —— 自由线角度显示随锚心基准 ±180°。
        refreshFields();
        refreshChrome();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 快照与提交
// ─────────────────────────────────────────────────────────────────────────────

const cad::param::Attachment* ContextStrip::findEditAttachment() const
{
    if (!m_paramDoc) return nullptr;
    // 连接角度会话 (二期): 直接按会话附件 id 取 —— 组件级连接
    // (fromBlockId 为空, 借用暴露端点) 与线级连接都适用, 不做端点匹配。
    if (m_connectSession && !m_connectAttId.isNull())
        return m_paramDoc->attachmentsView().byId(m_connectAttId);
    if (m_blockId.isNull()) return nullptr;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return nullptr;

    // 本段的角度由挂在本段起点或终点的跟随连接驱动。块可能有多条线段而跟随
    // 挂在另一条的端点上 —— 块级首配会显示并编辑**错误的**角度 (同属性对话框
    // 与 SegmentEditBar 的匹配规则)。钉 (pin) 永不驱动角度。
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId != m_blockId || att.isPin) continue;
        if (att.fromPointId == seg->startPointId
            || att.fromPointId == seg->endPointId)
            return &att;
    }
    return nullptr;
}

cad::cmd::SegmentEditBarCommand::State ContextStrip::snapshotState() const
{
    cad::cmd::SegmentEditBarCommand::State st;
    if (!m_paramDoc || m_blockId.isNull()) return st;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return st;

    st.segName = seg->name;
    st.lengthFormula = seg->lengthFormula;
    if (const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId)) {
        st.endDistance = ep->distance;
        st.endDistanceFormula = ep->distanceFormula;
        st.endAngle = ep->angle;
        st.endAngleFormula = ep->angleFormula;
        st.endConstraint = static_cast<int>(ep->constraint);
        st.endRefPointId = ep->refPointId;
    }
    if (const cad::param::Attachment* att = findEditAttachment()) {
        st.attId = att->id;
        st.followerAngle = att->followerAngle;
        st.followerAngleFormula = att->followerAngleFormula;
        st.arcLength = att->arcLength;
        st.arcLengthFormula = att->arcLengthFormula;
        st.rotationMode = static_cast<int>(att->rotationMode);
    }
    return st;
}

void ContextStrip::commitState(cad::cmd::SegmentEditBarCommand::State st)
{
    if (!m_paramDoc || m_blockId.isNull()) return;
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::SegmentEditBarCommand(
            m_paramDoc, m_blockId, m_segmentId, std::move(st)));
    } else {
        // 无 undo 栈 (无头单测): 直接应用。
        cad::cmd::SegmentEditBarCommand cmd(m_paramDoc, m_blockId, m_segmentId,
                                            std::move(st));
        cmd.redo();
    }
}

bool ContextStrip::inputHasFocus() const
{
    return m_nameEdit->hasFocus() || m_lenEdit->hasFocus() || m_angleEdit->hasFocus();
}

void ContextStrip::returnFocusToCanvas()
{
    if (m_canvasView) m_canvasView->setFocus();
    else              clearFocus();
}

QString ContextStrip::badgeText() const
{
    return m_badge ? m_badge->text() : QString();
}

QString ContextStrip::basisText() const
{
    return m_btnBasis ? m_btnBasis->text() : QString();
}

// ─────────────────────────────────────────────────────────────────────────────
// 键盘
// ─────────────────────────────────────────────────────────────────────────────

bool ContextStrip::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        // 输入包含: 吞掉工具单字母快捷键 (V/L/C/R/B/I/A/H), 编辑时不得切工具。
        auto* ke = static_cast<QKeyEvent*>(event);
        ke->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        // 连接角度会话 (二期): Enter/Esc 是会话收尾键, 由手势处理 ——
        // 不得走普通锁定的 解除锁定/撤销创建 路径。
        if (m_connectSession) {
            if (ke->key() == Qt::Key_Escape) {
                emit connectAngleCancelled();
                return true;
            }
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                emit connectAngleCommitted();
                return true;
            }
            return QWidget::eventFilter(watched, event);
        }
        if (ke->key() == Qt::Key_Escape) {
            if (m_creationPinned) {
                emit cancelRequested();     // 宿主撤销创建命令 (删线)
            } else {
                clearPinned();              // 普通锁定: 解除锁定, 焦点回画布
                returnFocusToCanvas();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (watched == m_nameEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            } else if (watched == m_angleEdit) {
                returnFocusToCanvas();       // 走完最后一个字段 → 回画布
            }
            return true;
        }
        if (ke->key() == Qt::Key_Tab) {
            if (watched == m_nameEdit)       { m_lenEdit->setFocus();  m_lenEdit->selectAll(); }
            else if (watched == m_lenEdit)   { m_angleEdit->setFocus(); m_angleEdit->selectAll(); }
            else if (watched == m_angleEdit) { m_nameEdit->setFocus();  m_nameEdit->selectAll(); }
            return true;
        }
        if (ke->key() == Qt::Key_Backtab) {
            if (watched == m_angleEdit)      { m_lenEdit->setFocus();  m_lenEdit->selectAll(); }
            else if (watched == m_lenEdit)   { m_nameEdit->setFocus(); m_nameEdit->selectAll(); }
            else if (watched == m_nameEdit)  { m_angleEdit->setFocus(); m_angleEdit->selectAll(); }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace cad::app
