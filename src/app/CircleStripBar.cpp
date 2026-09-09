#include "CircleStripBar.h"

#include <algorithm>
#include <cmath>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QUndoStack>

#include "ElaText.h"
#include "ElaLineEdit.h"

#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"
#include "ui/UiStrings.h"
#include "geometry/Angle.h"
#include "geometry/Epsilon.h"
#include "geometry/Units.h"
#include "parametric/Block.h"
#include "parametric/ConditionEngine.h"
#include "parametric/ParamDocument.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "document/commands/SegmentPropertyCommands.h"

namespace cad::app {

CircleStripBar::CircleStripBar(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(doc)
{
    buildUi();
}

void CircleStripBar::buildUi()
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    constexpr int kFieldH = 30;
    const auto& tk = cad::ui::Theme::tokens();

    // 圆专属徽标: 与线段条带徽标同色, 但固定写「圆」(不再借用线段徽标文案)。
    m_badge = new ElaText(QString::fromUtf8("圆"), 11, this);
    m_badge->setObjectName(QStringLiteral("stripCircleBadge"));
    m_badge->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 600; color: %1;").arg(tk.accent.name()));
    lay->addWidget(m_badge);

    m_serialLabel = new ElaText(QString(), 11, this);
    m_serialLabel->setObjectName(QStringLiteral("stripCircleSerial"));
    m_serialLabel->setStyleSheet(QStringLiteral(
        "#stripCircleSerial { %1 font-size: 10px; font-weight: 600; color: %2; background: %3; border: 1px solid %4; border-radius: 2px; padding: 2px 6px; }")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily, tk.text1.name(), tk.surface3.name(), tk.borderStrong.name()));
    lay->addWidget(m_serialLabel);

    auto addField = [this, lay](const QString& caption, ElaLineEdit*& edit, ElaText*& label,
                                int width, const QString& placeholder) {
        label = new ElaText(caption, 11, this);
        label->setObjectName(QStringLiteral("stripField"));
        label->setStyleSheet(QStringLiteral("font-size: 11px;"));
        lay->addWidget(label);
        edit = new ElaLineEdit(this);
        edit->setFixedHeight(kFieldH);
        edit->setFixedWidth(width);
        edit->setPlaceholderText(placeholder);
        edit->setStyleSheet(QStringLiteral("font-size: 11px;"));
        lay->addWidget(edit);
    };
    auto addUnit = [this, lay](const QString& unit) -> ElaText* {
        auto* label = new ElaText(unit, 11, this);
        label->setStyleSheet(QStringLiteral("font-size: 11px;"));
        lay->addWidget(label);
        return label;
    };

    addField(QString::fromUtf8("名称:"), m_nameEdit, m_nameLabel, 90, QString::fromUtf8("如: 领圆"));
    addField(QString::fromUtf8("半径:"), m_radiusEdit, m_radiusLabel, 65, QStringLiteral("0.0"));
    m_radiusUnit = addUnit(QStringLiteral("cm"));
    // 绘制会话的锁定指示 (一期补充): 半径被输入定值后显示。
    m_lockChip = new ElaText(QString::fromUtf8("已锁定"), 11, this);
    m_lockChip->setObjectName(QStringLiteral("stripCircleLock"));
    m_lockChip->setStyleSheet(QStringLiteral(
        "#stripCircleLock { font-size: 10px; font-weight: 600; color: %1; background: %2; border-radius: 2px; padding: 2px 6px; }")
        .arg(tk.accent.name(), tk.surface3.name()));
    m_lockChip->hide();
    lay->addWidget(m_lockChip);
    addField(QString::fromUtf8("直径:"), m_diameterEdit, m_diameterLabel, 65, QStringLiteral("0.0"));
    m_diameterUnit = addUnit(QStringLiteral("cm"));
    addField(QString::fromUtf8("周长:"), m_circumferenceEdit, m_circumferenceLabel, 65, QStringLiteral("0.0"));
    m_circumferenceUnit = addUnit(QStringLiteral("cm"));

    m_hint = new ElaText(QString(), 11, this);
    m_hint->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;").arg(tk.text2.name()));
    lay->addWidget(m_hint);

    lay->addStretch();

    m_radiusEdit->setToolTip(cad::ui::TooltipFormatter::action(
        QString::fromUtf8("半径 R"),
        QString::fromUtf8("圆的半径 (cm 或公式)。唯一权威 = 圆段起点 p₀ 的 distance/distanceFormula, 修改它圆心不动。")));
    m_diameterEdit->setToolTip(cad::ui::TooltipFormatter::action(
        QString::fromUtf8("直径 D"),
        QString::fromUtf8("D = 2R (cm 或公式)。输入后自动换算为半径并落一步撤销; 直径是派生量, 不单独存模型。")));
    m_circumferenceEdit->setToolTip(cad::ui::TooltipFormatter::action(
        QString::fromUtf8("周长 C"),
        QString::fromUtf8("C = 2πR (cm 或公式)。输入后自动换算为半径并落一步撤销; 周长是派生量, 不单独存模型。")));

    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] { applyEdits(); });
    connect(m_radiusEdit, &QLineEdit::editingFinished, this, [this] { applyEdits(); });
    connect(m_radiusEdit, &QLineEdit::textEdited, this, &CircleStripBar::onRadiusEdited);
    connect(m_diameterEdit, &QLineEdit::editingFinished, this, &CircleStripBar::onDiameterEdited);
    connect(m_circumferenceEdit, &QLineEdit::editingFinished, this, &CircleStripBar::onCircumferenceEdited);

    for (auto* edit : {m_nameEdit, m_radiusEdit, m_diameterEdit, m_circumferenceEdit})
        edit->installEventFilter(this);

    updateHint();
}

bool CircleStripBar::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    if (m_session) endSession();   // 悬停/锁定接管时退出绘制态。
    if (blockId.isNull() || segmentId.isNull() || !m_paramDoc) {
        clearTarget();
        return false;
    }
    auto* blk = m_paramDoc->findBlock(blockId);
    const auto* seg = blk ? blk->findSegment(segmentId) : nullptr;
    if (!seg || seg->fitKind != cad::param::FitKind::Circle) {
        clearTarget();
        return false;
    }

    m_blockId = blockId;
    m_segmentId = segmentId;
    refresh();
    show();
    update();
    return true;
}

void CircleStripBar::clearTarget()
{
    m_blockId = QUuid();
    m_segmentId = QUuid();
    hide();
}

void CircleStripBar::beginSession()
{
    m_session = true;
    m_sessionLocked = false;
    m_blockId = QUuid();
    m_segmentId = QUuid();

    m_badge->setText(QString::fromUtf8("绘制"));
    // 编号/名称/周长让位: 绘制中只有半径是输入面 (设计 §5.5)。
    if (m_serialLabel) m_serialLabel->hide();
    if (m_nameLabel) m_nameLabel->hide();
    if (m_nameEdit) m_nameEdit->hide();
    if (m_circumferenceLabel) m_circumferenceLabel->hide();
    if (m_circumferenceEdit) m_circumferenceEdit->hide();
    if (m_circumferenceUnit) m_circumferenceUnit->hide();

    if (m_radiusLabel) m_radiusLabel->show();
    if (m_radiusUnit) m_radiusUnit->show();
    if (m_diameterLabel) m_diameterLabel->show();
    if (m_diameterUnit) m_diameterUnit->show();
    if (m_lockChip) m_lockChip->setVisible(false);
    {
        const QSignalBlocker br(m_radiusEdit);
        const QSignalBlocker bd(m_diameterEdit);
        m_radiusEdit->clear();
        m_diameterEdit->clear();
    }
    m_radiusEdit->setReadOnly(false);
    m_diameterEdit->setReadOnly(true);   // 直径是只读联动派生量。
    m_radiusEdit->show();
    m_diameterEdit->show();

    updateHint();
    show();
    update();
}

void CircleStripBar::updateSessionValues(double radiusCm, bool locked)
{
    if (!m_session) return;
    if (m_sessionLocked != locked) {
        m_sessionLocked = locked;
        if (m_lockChip) m_lockChip->setVisible(locked);
        updateHint();
    }
    // 不覆盖正在输入的框 (条带铁律); 未锁定时半径框跟随光标实时读数。
    if (!locked && m_radiusEdit && !m_radiusEdit->hasFocus()) {
        const QSignalBlocker blocker(m_radiusEdit);
        m_radiusEdit->setText(cad::geo::Units::formatNumberTrimmed(radiusCm));
    }
    if (m_diameterEdit) {
        const QString d = cad::geo::Units::formatNumberTrimmed(2.0 * radiusCm);
        if (m_diameterEdit->text() != d) {
            const QSignalBlocker blocker(m_diameterEdit);
            m_diameterEdit->setText(d);
        }
    }
}

void CircleStripBar::endSession()
{
    if (!m_session) return;
    m_session = false;
    m_sessionLocked = false;
    m_badge->setText(QString::fromUtf8("圆"));
    for (auto* w : {static_cast<QWidget*>(m_serialLabel), static_cast<QWidget*>(m_nameLabel),
                    static_cast<QWidget*>(m_nameEdit), static_cast<QWidget*>(m_circumferenceLabel),
                    static_cast<QWidget*>(m_circumferenceEdit), static_cast<QWidget*>(m_circumferenceUnit)}) {
        if (w) w->show();
    }
    if (m_lockChip) m_lockChip->hide();
    clearTarget();
}

void CircleStripBar::refresh()
{
    if (!m_paramDoc || m_blockId.isNull() || m_segmentId.isNull()) return;
    const auto* blk = m_paramDoc->findBlock(m_blockId);
    const auto* seg = blk ? blk->findSegment(m_segmentId) : nullptr;
    if (!blk || !seg || seg->fitKind != cad::param::FitKind::Circle) return;

    m_serialLabel->setText(cad::param::Serial::tag(seg->serial));

    // 不覆盖正在输入的框 (条带铁律: 输入期间 refresh 必须让路)。
    auto setText = [](ElaLineEdit* edit, const QString& text) {
        if (!edit || edit->hasFocus() || edit->text() == text) return;
        const QSignalBlocker blocker(edit);
        edit->setText(text);
    };

    setText(m_nameEdit, seg->name);

    const double rMm = blk->circleRadiusMm(*seg);
    // 半径公式优先回显: 否则提交会把公式覆盖成数值。
    const auto* sp = blk->findPoint(seg->startPointId);
    setText(m_radiusEdit, (sp && !sp->distanceFormula.isEmpty())
        ? sp->distanceFormula
        : cad::geo::Units::formatCm(rMm));
    setText(m_diameterEdit, cad::geo::Units::formatCm(2.0 * rMm));
    setText(m_circumferenceEdit, cad::geo::Units::formatCm(2.0 * cad::geo::kPi * rMm));

    updateHint();
}

void CircleStripBar::updateHint()
{
    if (!m_hint) return;
    if (m_session) {
        m_hint->setText((m_sessionLocked
            ? QString::fromUtf8("已锁定半径 · %1 落圆 · %2 取消")
            : QString::fromUtf8("拖动或输入半径 · %1 落圆 · %2 取消"))
            .arg(cad::ui::kbdBadge(QStringLiteral("Enter")),
                 cad::ui::kbdBadge(QStringLiteral("Esc"))));
        return;
    }
    m_hint->setText(m_editable
        ? QString::fromUtf8("%1 应用 · %2 切换字段 · %3 解除锁定")
              .arg(cad::ui::kbdBadge(QStringLiteral("Enter")),
                   cad::ui::kbdBadge(QStringLiteral("Tab")),
                   cad::ui::kbdBadge(QStringLiteral("Esc")))
        : QString::fromUtf8("悬停预览 (只读) · 点击选中后可编辑"));
}

void CircleStripBar::setEditable(bool editable)
{
    m_editable = editable;
    if (m_session) {   // 绘制态的可编辑性由会话决定, 不随悬停/锁定漂移。
        m_radiusEdit->setReadOnly(false);
        m_diameterEdit->setReadOnly(true);
        updateHint();
        return;
    }
    for (auto* edit : {m_nameEdit, m_radiusEdit, m_diameterEdit, m_circumferenceEdit}) {
        if (edit) edit->setReadOnly(!editable);
    }
    updateHint();
}

bool CircleStripBar::hasInputFocus() const
{
    return m_nameEdit->hasFocus() || m_radiusEdit->hasFocus()
        || m_diameterEdit->hasFocus() || m_circumferenceEdit->hasFocus();
}

QString CircleStripBar::badgeText() const
{
    return m_badge ? m_badge->text() : QString();
}

QString CircleStripBar::serialText() const
{
    return m_serialLabel ? m_serialLabel->text() : QString();
}

void CircleStripBar::applyEdits()
{
    if (m_suppressApply) return;
    if (m_session) return;   // 绘制态没有模型目标, 输入走会话信号。
    if (!m_paramDoc || m_blockId.isNull() || m_segmentId.isNull()) return;
    auto* blk = m_paramDoc->findBlock(m_blockId);
    auto* seg = blk ? blk->findSegment(m_segmentId) : nullptr;
    if (!blk || !seg || seg->fitKind != cad::param::FitKind::Circle) return;

    auto st = cad::cmd::SegmentEditBarCommand::State::captureFrom(*m_paramDoc, m_blockId, m_segmentId);
    bool changed = false;

    const QString name = m_nameEdit->text().trimmed();
    if (name != seg->name) {
        st.segName = name;
        changed = true;
    }

    // 半径槽只在文本真的偏离回显值时落笔: 否则仅仅把焦点移出输入框
    // (editingFinished) 就会把 2 位小数的显示值写回模型并压一条空撤销步。
    const auto* sp = blk->findPoint(seg->startPointId);
    const double rMm = blk->circleRadiusMm(*seg);
    const QString shownRadius = (sp && !sp->distanceFormula.isEmpty())
        ? sp->distanceFormula
        : cad::geo::Units::formatCm(rMm);
    if (m_radiusEdit->text().trimmed() != shownRadius.trimmed()) {
        const auto parsed = cad::geo::parseNumberOrFormula(m_radiusEdit->text());
        if (!parsed.isNumber && parsed.formula.isEmpty()) return;
        if (parsed.isNumber) {
            // 负半径在圆语义下无意义: 与属性面板一致地夹到 0。
            const double newRadiusMm = std::max(cad::geo::Units::cmToMm(parsed.value), 0.0);
            if (std::abs(st.startDistance - newRadiusMm) > cad::geo::kGeomEps
                || !st.startDistanceFormula.isEmpty()) {
                st.startDistance = newRadiusMm;
                st.startDistanceFormula.clear();
                changed = true;
            }
        } else if (st.startDistanceFormula != parsed.formula) {
            st.startDistanceFormula = parsed.formula;
            changed = true;
        }
    }

    if (!changed) return;

    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::SegmentEditBarCommand(
            m_paramDoc, m_blockId, m_segmentId, std::move(st)));
    } else {
        cad::cmd::SegmentEditBarCommand cmd(m_paramDoc, m_blockId, m_segmentId, std::move(st));
        cmd.redo();
    }
    refresh();
}

void CircleStripBar::onRadiusEdited()
{
    if (!m_session) return;
    // 与放置点同一判读口径 (CAN-P1-17): 数值直接发, 公式求值后作为实时预览
    // 值 (cm 域) 发, 两种都算「锁定」; 清空 = 解锁, 恢复跟随光标。
    const auto parsed = cad::geo::parseNumberOrFormula(m_radiusEdit->text());
    if (parsed.isNumber) {
        emit sessionRadiusChanged(std::max(parsed.value, 0.0), true);
        return;
    }
    if (parsed.formula.isEmpty()) {
        emit sessionRadiusChanged(0.0, false);
        return;
    }
    if (m_paramDoc) {
        const auto r = cad::param::ConditionEngine::evaluate(
            parsed.formula, m_paramDoc->parameters(), m_paramDoc->conditions());
        if (r.ok) emit sessionRadiusChanged(std::max(r.value, 0.0), true);
    }
}

void CircleStripBar::onDiameterEdited()
{
    if (m_session) return;   // 绘制态直径是只读联动, 不反算。
    const auto d = cad::geo::parseNumberOrFormula(m_diameterEdit->text());
    QString radiusText;
    if (d.isNumber) {
        radiusText = cad::geo::Units::formatCm(cad::geo::Units::cmToMm(d.value) * 0.5);
    } else if (!d.formula.isEmpty()) {
        radiusText = QStringLiteral("(%1)/2").arg(d.formula);
    } else {
        return;
    }
    {
        const QSignalBlocker blocker(m_radiusEdit);
        m_radiusEdit->setText(radiusText);
    }
    applyEdits();
}

void CircleStripBar::onCircumferenceEdited()
{
    if (m_session) return;   // 绘制态周长不在条带上。
    const auto c = cad::geo::parseNumberOrFormula(m_circumferenceEdit->text());
    QString radiusText;
    if (c.isNumber) {
        radiusText = cad::geo::Units::formatCm(cad::geo::Units::cmToMm(c.value) / (2.0 * cad::geo::kPi));
    } else if (!c.formula.isEmpty()) {
        radiusText = QStringLiteral("(%1)/(%2)")
            .arg(c.formula, QString::number(2.0 * cad::geo::kPi, 'g', 17));
    } else {
        return;
    }
    {
        const QSignalBlocker blocker(m_radiusEdit);
        m_radiusEdit->setText(radiusText);
    }
    applyEdits();
}

void CircleStripBar::focusNextField(bool backwards)
{
    ElaLineEdit* order[4] = {m_nameEdit, m_radiusEdit, m_diameterEdit, m_circumferenceEdit};
    int idx = 0;
    for (int i = 0; i < 4; ++i) {
        if (order[i] && order[i]->hasFocus()) { idx = i; break; }
    }
    const int next = backwards ? (idx + 3) % 4 : (idx + 1) % 4;
    if (order[next]) {
        order[next]->setFocus();
        order[next]->selectAll();
    }
}

void CircleStripBar::applyTheme()
{
    if (!m_badge) return;
    const auto& tk = cad::ui::Theme::tokens();
    m_badge->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 600; color: %1;").arg(tk.accent.name()));
    m_serialLabel->setStyleSheet(QStringLiteral(
        "#stripCircleSerial { %1 font-size: 10px; font-weight: 600; color: %2; background: %3; border: 1px solid %4; border-radius: 2px; padding: 2px 6px; }")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily, tk.text1.name(), tk.surface3.name(), tk.borderStrong.name()));
    m_hint->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;").arg(tk.text2.name()));
    if (m_lockChip) {
        m_lockChip->setStyleSheet(QStringLiteral(
            "#stripCircleLock { font-size: 10px; font-weight: 600; color: %1; background: %2; border-radius: 2px; padding: 2px 6px; }")
            .arg(tk.accent.name(), tk.surface3.name()));
    }
}

bool CircleStripBar::eventFilter(QObject* watched, QEvent* event)
{
    const bool isField = (watched == m_nameEdit || watched == m_radiusEdit
                          || watched == m_diameterEdit || watched == m_circumferenceEdit);
    if (!isField) return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::ShortcutOverride) {
        // 条带输入框吞掉画布单键快捷键 (CONTEXT_STRIP_DESIGN.md §4 铁律)。
        event->accept();
        return true;
    }
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);

    auto* ke = static_cast<QKeyEvent*>(event);
    if (m_session) {
        // 绘制态只有一个输入面: 半径。
        switch (ke->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            emit sessionCommitted();
            emit returnFocusRequested();
            return true;
        case Qt::Key_Escape:
            emit sessionCancelled();
            emit returnFocusRequested();
            return true;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            m_radiusEdit->setFocus();
            m_radiusEdit->selectAll();
            return true;
        default:
            break;
        }
        return QWidget::eventFilter(watched, event);
    }
    switch (ke->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        applyEdits();
        emit returnFocusRequested();
        return true;
    case Qt::Key_Escape:
        // Esc = 解除锁定 (不落笔): 焦点回落会同步触发 editingFinished,
        // 用抑制位挡住那一次提交。
        m_suppressApply = true;
        emit cancelRequested();
        m_suppressApply = false;
        return true;
    case Qt::Key_Tab:
        focusNextField(false);
        return true;
    case Qt::Key_Backtab:
        focusNextField(true);
        return true;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace cad::app
