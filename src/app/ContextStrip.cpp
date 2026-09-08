#include "ContextStrip.h"
#include "PlacedPointStripBar.h"

#include <QApplication>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QPushButton>
#include <QTimer>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"

#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"
#include "ui/FormScaffold.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "ui/UiStrings.h"

namespace cad::app {
namespace {

constexpr int kHoverThrottleMs = 80;
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

    if (m_paramDoc) {
        connect(m_paramDoc, &cad::param::ParamDocument::resolved,
                this, [this] {
                    if (m_focus != StripFocus::Empty && !m_strokePreview) {
                        refreshFields();
                        refreshChrome();
                    }
                });
    }

    hideBar();
}

ContextStrip::~ContextStrip() = default;

void ContextStrip::buildUi()
{
    auto* rootLay = new QHBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    // ── 1. 线段条带容器 ──
    m_segmentBar = new QWidget(this);
    auto* lay = new QHBoxLayout(m_segmentBar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    constexpr int kFieldH = 30;
    const auto& tk = cad::ui::Theme::tokens();

    m_idLabel = new ElaText(QString(), 11, m_segmentBar);
    m_idLabel->setObjectName(QStringLiteral("stripSerial"));
    m_idLabel->setStyleSheet(QStringLiteral(
        "#stripSerial { %1 font-size: 10px; font-weight: 600; color: %2; background: %3; border: 1px solid %4; border-radius: 2px; padding: 2px 6px; }")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily, tk.text1.name(), tk.surface3.name(), tk.borderStrong.name()));
    lay->addWidget(m_idLabel);

    auto addField = [this, lay](const QString& caption, ElaLineEdit*& edit, int width,
                                const QString& placeholder) {
        auto* label = new ElaText(caption, 11, m_segmentBar);
        label->setObjectName(QStringLiteral("stripField"));
        label->setStyleSheet(QStringLiteral("font-size: 11px;"));
        lay->addWidget(label);
        if (!edit) {
            edit = new ElaLineEdit(m_segmentBar);
            edit->setStyleSheet(QStringLiteral("font-size: 11px;"));
        }
        edit->setFixedHeight(kFieldH);
        edit->setFixedWidth(width);
        edit->setPlaceholderText(placeholder);
        lay->addWidget(edit);
    };

    addField(QString::fromUtf8("名称:"), m_nameEdit, 75, QString::fromUtf8("如: 侧缝"));

    addField(QString::fromUtf8("长度:"), m_lenEdit, 65, QString::fromUtf8("0.0"));
    m_btnPasteLen = new ElaPushButton(QString::fromUtf8("粘贴"), m_segmentBar);
    m_btnPasteLen->setFixedSize(40, kFieldH);
    m_btnPasteLen->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_btnPasteLen->setCursor(Qt::PointingHandCursor);
    m_btnPasteLen->setToolTip(cad::ui::TooltipFormatter::actionWithShortcut(
        cad::ui::str::kPasteFormulaOrValue, QStringLiteral("Ctrl+V"),
        QStringLiteral("将剪贴板内容写入长度框并立即应用（自动清洗换行）")));
    connect(m_btnPasteLen, &QAbstractButton::clicked, this, &ContextStrip::onPasteLength);
    lay->addWidget(m_btnPasteLen);

    m_baseAngleEdit = new ElaLineEdit(m_segmentBar);
    m_baseAngleEdit->setReadOnly(true);
    m_baseAngleEdit->setFocusPolicy(Qt::NoFocus);
    m_baseAngleEdit->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;").arg(tk.text2.name()));
    m_baseAngleEdit->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("基准角度"),
        QStringLiteral("当前线段的基准方向角（度数）。有连接时为母线切向；无连接时为基准参考角。"),
        false));
    addField(QString::fromUtf8("基准:"), m_baseAngleEdit, 60, QString::fromUtf8("0.0"));

    addField(QString::fromUtf8("角度:"), m_angleEdit, 65, QString::fromUtf8("0.0"));
    m_btnPasteAngle = new ElaPushButton(QString::fromUtf8("粘贴"), m_segmentBar);
    m_btnPasteAngle->setFixedSize(40, kFieldH);
    m_btnPasteAngle->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_btnPasteAngle->setCursor(Qt::PointingHandCursor);
    m_btnPasteAngle->setToolTip(cad::ui::TooltipFormatter::actionWithShortcut(
        cad::ui::str::kPasteFormulaOrValue, QStringLiteral("Ctrl+V"),
        QStringLiteral("将剪贴板内容写入角度框并立即应用（自动清洗换行）")));
    connect(m_btnPasteAngle, &QAbstractButton::clicked, this, &ContextStrip::onPasteAngle);
    lay->addWidget(m_btnPasteAngle);

    const QString unitChip = cad::ui::chipButtonStyle();

    m_btnUnitAngle = new QPushButton(QStringLiteral("°"), m_segmentBar);
    m_btnUnitAngle->setCheckable(true);
    m_btnUnitAngle->setChecked(true);
    m_btnUnitAngle->setFixedSize(26, kFieldH);
    m_btnUnitAngle->setStyleSheet(unitChip);
    m_btnUnitAngle->setCursor(Qt::PointingHandCursor);
    m_btnUnitAngle->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("角度模式"),
        QStringLiteral("跟随基准线的角度偏移（度数）；旋转基准线时保持该角度")));
    lay->addWidget(m_btnUnitAngle);

    m_btnUnitArc = new QPushButton(QString::fromUtf8("⌒"), m_segmentBar);
    m_btnUnitArc->setCheckable(true);
    m_btnUnitArc->setFixedSize(26, kFieldH);
    m_btnUnitArc->setStyleSheet(unitChip);
    m_btnUnitArc->setCursor(Qt::PointingHandCursor);
    m_btnUnitArc->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("弧长模式"),
        QStringLiteral("沿基准圆周切向展开的弧长（cm）；切换保留当前几何并自动换算")));
    lay->addWidget(m_btnUnitArc);

    m_btnUnitChord = new QPushButton(QString::fromUtf8("↔"), m_segmentBar);
    m_btnUnitChord->setCheckable(true);
    m_btnUnitChord->setFixedSize(26, kFieldH);
    m_btnUnitChord->setStyleSheet(unitChip);
    m_btnUnitChord->setCursor(Qt::PointingHandCursor);
    m_btnUnitChord->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("开度 / 弦长模式"),
        QStringLiteral("端点到基准线的直线距离（开度，cm）；折角 0° 对应开度 0，切换自动几何换算")));
    lay->addWidget(m_btnUnitChord);

    m_unitGroup = new QButtonGroup(this);
    m_unitGroup->setExclusive(true);
    m_unitGroup->addButton(m_btnUnitAngle, 0);
    m_unitGroup->addButton(m_btnUnitArc, 1);
    m_unitGroup->addButton(m_btnUnitChord, 2);
    connect(m_unitGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (id == 1)      onUnitSelected(cad::param::RotationMode::ArcLength);
        else if (id == 2) onUnitSelected(cad::param::RotationMode::ChordLength);
        else              onUnitSelected(cad::param::RotationMode::Angle);
    });

    m_btnReverse = new ElaPushButton(QString::fromUtf8("换向"), m_segmentBar);
    m_btnReverse->setFixedSize(46, kFieldH);
    m_btnReverse->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_btnReverse->setCursor(Qt::PointingHandCursor);
    connect(m_btnReverse, &QAbstractButton::clicked, this, &ContextStrip::onReverseClicked);
    lay->addWidget(m_btnReverse);

    m_btnBasis = new ElaPushButton(QString(), m_segmentBar);
    m_btnBasis->setFixedSize(110, kFieldH);
    m_btnBasis->setObjectName(QStringLiteral("stripBasisBtn"));
    m_btnBasis->setStyleSheet(QStringLiteral(
        "#stripBasisBtn { %1 font-size: 10px; font-weight: 600; color: %2; background: %3; border: 1px solid %4; border-radius: 2px; padding: 2px 6px; }")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily, tk.text2.name(), tk.surface2.name(), tk.borderStrong.name()));
    m_btnBasis->setToolTip(cad::ui::TooltipFormatter::action(
        cad::ui::str::kAngleBasis,
        cad::ui::str::kStartToEndTip));
    lay->addWidget(m_btnBasis);

    m_btnPosDetach = new ElaPushButton(QString::fromUtf8("拆开"), m_segmentBar);
    m_btnPosDetach->setFixedSize(46, kFieldH);
    m_btnPosDetach->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_btnPosDetach->setCursor(Qt::PointingHandCursor);
    connect(m_btnPosDetach, &QAbstractButton::clicked, this, &ContextStrip::onPosDetachClicked);
    lay->addWidget(m_btnPosDetach);

    m_btnAngleDetach = new ElaPushButton(QString::fromUtf8("基准"), m_segmentBar);
    m_btnAngleDetach->setFixedSize(46, kFieldH);
    m_btnAngleDetach->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_btnAngleDetach->setCursor(Qt::PointingHandCursor);
    connect(m_btnAngleDetach, &QAbstractButton::clicked, this, &ContextStrip::onAngleDetachClicked);
    lay->addWidget(m_btnAngleDetach);

    m_badge = new ElaText(QString(), 11, m_segmentBar);
    m_badge->setObjectName(QStringLiteral("stripBadge"));
    m_badge->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 600; color: %1;").arg(tk.accent.name()));
    lay->addWidget(m_badge);

    m_hint = new ElaText(QString(), 11, m_segmentBar);
    m_hint->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;").arg(tk.text2.name()));
    lay->addWidget(m_hint);

    lay->addStretch();

    // ── 2. 放置点条带容器 (独立子控件) ──
    m_placedPointBar = new PlacedPointStripBar(m_paramDoc, this);
    connect(m_placedPointBar, &PlacedPointStripBar::distChanged, this, &ContextStrip::placePointDistChanged);
    connect(m_placedPointBar, &PlacedPointStripBar::angleChanged, this, &ContextStrip::placePointAngleChanged);
    connect(m_placedPointBar, &PlacedPointStripBar::committed, this, &ContextStrip::placePointCommitted);
    connect(m_placedPointBar, &PlacedPointStripBar::deleted, this, [this](const QUuid& bId, const QUuid& pId) {
        emit placedPointDeleted(bId, pId);
        hideBar();
    });
    connect(m_placedPointBar, &PlacedPointStripBar::cancelRequested, this, &ContextStrip::cancelRequested);
    connect(m_placedPointBar, &PlacedPointStripBar::returnFocusRequested, this, [this] {
        clearPlacedPoint();
        returnFocusToCanvas();
    });

    rootLay->addWidget(m_segmentBar);
    rootLay->addWidget(m_placedPointBar);
    m_placedPointBar->hide();

    connect(m_nameEdit, &QLineEdit::textChanged, this, [this] { applyName(); });
    connect(m_lenEdit, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });
    connect(m_lenEdit, &QLineEdit::editingFinished, this, [this] { applyLength(); });
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

void ContextStrip::setCanvasView(QWidget* canvasView)
{
    m_canvasView = canvasView;
}

void ContextStrip::setHoverTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_hoverBlock = blockId;
    m_hoverSegment = segmentId;
    if (!m_hoverTimer->isActive())
        m_hoverTimer->start();
}

void ContextStrip::flushHover()
{
    if (isPlacedPointMode()) return;
    if (m_focus == StripFocus::Pinned || m_strokePreview) return;
    if (inputHasFocus()) return;
    if (QApplication::mouseButtons() != Qt::NoButton) return;

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
    if (m_connectSession) return;
    if (blockId.isNull() || segmentId.isNull()) { clearPinned(); return; }
    const auto* blk = m_paramDoc ? m_paramDoc->findBlock(blockId) : nullptr;
    const auto* seg = blk ? blk->findSegment(segmentId) : nullptr;
    if (!blk || !seg) return;

    if (isPlacedPointMode()) {
        if (m_placedPointBar) m_placedPointBar->clearTarget();
        if (m_segmentBar) m_segmentBar->show();
    }

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
    m_hoverTimer->stop();
    m_hoverBlock = QUuid();
    m_hoverSegment = QUuid();
    if (m_focus == StripFocus::Hover) hideBar();
}

void ContextStrip::clearPinned()
{
    if (isPlacedPointMode()) {
        clearPlacedPoint();
        return;
    }
    if (m_focus != StripFocus::Pinned) return;
    m_focus = StripFocus::Empty;
    m_creationPinned = false;
    m_connectSession = false;
    m_connectAttId = QUuid();
    m_blockId = QUuid();
    m_segmentId = QUuid();
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
    m_creationPinned = true;
}

void ContextStrip::hideBar()
{
    m_hoverTimer->stop();
    if (m_focus == StripFocus::Pinned && !m_creationPinned) {
        m_focus = StripFocus::Empty;
        m_blockId = QUuid();
        m_segmentId = QUuid();
    } else if (m_focus == StripFocus::Hover) {
        m_focus = StripFocus::Empty;
        m_blockId = QUuid();
        m_segmentId = QUuid();
    }
    m_strokePreview = false;
    m_creationPinned = false;
    m_connectSession = false;
    m_connectAttId = QUuid();
    m_rotateAnchor = {};

    if (m_placedPointBar) m_placedPointBar->clearTarget();
    if (m_segmentBar) m_segmentBar->show();

    hide();
}

bool ContextStrip::readOnly() const
{
    return m_nameEdit->isReadOnly();
}

void ContextStrip::setReadOnlyFields(bool readOnly)
{
    m_nameEdit->setReadOnly(readOnly);
    m_lenEdit->setReadOnly(readOnly);
    m_angleEdit->setReadOnly(readOnly);
    if (m_btnPasteLen) m_btnPasteLen->setEnabled(!readOnly);
    if (m_btnPasteAngle) m_btnPasteAngle->setEnabled(!readOnly);
}

bool ContextStrip::inputHasFocus() const
{
    return m_nameEdit->hasFocus() || m_lenEdit->hasFocus() || m_angleEdit->hasFocus()
        || (m_placedPointBar && m_placedPointBar->hasInputFocus());
}

void ContextStrip::returnFocusToCanvas()
{
    if (m_canvasView) m_canvasView->setFocus();
    else              clearFocus();
}

void ContextStrip::setPlacedPointTarget(const QUuid& blockId, const QUuid& pointId)
{
    if (m_placedPointBar && m_placedPointBar->setTarget(blockId, pointId)) {
        m_segmentBar->hide();
        m_focus = StripFocus::Pinned;
        show();
        update();
    } else {
        clearPlacedPoint();
    }
}

void ContextStrip::clearPlacedPoint()
{
    if (m_placedPointBar) m_placedPointBar->clearTarget();
    if (m_segmentBar) m_segmentBar->show();
    hideBar();
}

void ContextStrip::beginPlacePointSession(const QString& baseSegName)
{
    if (m_placedPointBar) {
        m_segmentBar->hide();
        m_placedPointBar->beginSession(baseSegName);
        m_focus = StripFocus::Pinned;
        show();
        update();
    }
}

void ContextStrip::updatePlacePointValues(double distCm, double angleDeg, bool distLocked, bool angleLocked)
{
    if (m_placedPointBar) m_placedPointBar->updateValues(distCm, angleDeg, distLocked, angleLocked);
}

void ContextStrip::endPlacePointSession()
{
    if (m_placedPointBar && m_placedPointBar->isSession()) {
        clearPlacedPoint();
    }
}

void ContextStrip::focusNextPlacedPointField()
{
    if (m_placedPointBar) m_placedPointBar->focusNextField();
}

bool ContextStrip::isPlacedPointMode() const
{
    return m_placedPointBar && m_placedPointBar->isPlacedPointMode();
}

bool ContextStrip::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        ke->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);

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
                emit cancelRequested();
            } else {
                clearPinned();
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
                returnFocusToCanvas();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Tab) {
            if (watched == m_nameEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            } else if (watched == m_angleEdit) {
                m_nameEdit->setFocus();
                m_nameEdit->selectAll();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Backtab) {
            if (watched == m_angleEdit) {
                m_lenEdit->setFocus();
                m_lenEdit->selectAll();
            } else if (watched == m_lenEdit) {
                m_nameEdit->setFocus();
                m_nameEdit->selectAll();
            } else if (watched == m_nameEdit) {
                m_angleEdit->setFocus();
                m_angleEdit->selectAll();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace cad::app
