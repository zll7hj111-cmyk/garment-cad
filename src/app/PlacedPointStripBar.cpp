#include "PlacedPointStripBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QAbstractButton>
#include <QUndoStack>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"

#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"
#include "parametric/ConditionEngine.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/ParamPoint.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "geometry/Units.h"
#include "document/commands/BlockCommands.h"
#include "document/CommandTexts.h"

namespace cad::app {

PlacedPointStripBar::PlacedPointStripBar(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(doc)
{
    buildUi();
}

void PlacedPointStripBar::buildUi()
{
    auto* ptLay = new QHBoxLayout(this);
    ptLay->setContentsMargins(0, 0, 0, 0);
    ptLay->setSpacing(4);

    constexpr int kFieldH = 30;
    const auto& tk = cad::ui::Theme::tokens();

    m_ptSerialLabel = new ElaText(QString(), 11, this);
    m_ptSerialLabel->setObjectName(QStringLiteral("stripPtSerial"));
    m_ptSerialLabel->setStyleSheet(QStringLiteral(
        "#stripPtSerial { %1 font-size: 10px; font-weight: 600; color: %2; background: %3; border: 1px solid %4; border-radius: 2px; padding: 2px 6px; }")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily, tk.text1.name(), tk.surface3.name(), tk.borderStrong.name()));
    ptLay->addWidget(m_ptSerialLabel);

    auto addPtField = [this, ptLay](const QString& caption, ElaLineEdit*& edit, int width, const QString& placeholder) {
        auto* label = new ElaText(caption, 11, this);
        label->setObjectName(QStringLiteral("stripField"));
        label->setStyleSheet(QStringLiteral("font-size: 11px;"));
        ptLay->addWidget(label);
        edit = new ElaLineEdit(this);
        edit->setFixedHeight(kFieldH);
        edit->setFixedWidth(width);
        edit->setPlaceholderText(placeholder);
        edit->setStyleSheet(QStringLiteral("font-size: 11px;"));
        ptLay->addWidget(edit);
    };

    addPtField(QString::fromUtf8("名称:"), m_ptNameEdit, 90, QString::fromUtf8("如: 领窝点"));
    addPtField(QString::fromUtf8("距离:"), m_ptDistEdit, 65, QString::fromUtf8("0.0"));
    auto* lblDistUnit = new ElaText(QStringLiteral("cm"), 11, this);
    lblDistUnit->setStyleSheet(QStringLiteral("font-size: 11px;"));
    ptLay->addWidget(lblDistUnit);

    addPtField(QString::fromUtf8("角度:"), m_ptAngleEdit, 65, QString::fromUtf8("0.0"));
    auto* lblAngleUnit = new ElaText(QStringLiteral("°"), 11, this);
    lblAngleUnit->setStyleSheet(QStringLiteral("font-size: 11px;"));
    ptLay->addWidget(lblAngleUnit);

    auto* lblBaseSeg = new ElaText(QString::fromUtf8("基准线段:"), 11, this);
    lblBaseSeg->setStyleSheet(QStringLiteral("font-size: 11px;"));
    ptLay->addWidget(lblBaseSeg);
    m_ptBaseSegLabel = new ElaText(QString(), 11, this);
    m_ptBaseSegLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 600; color: %1;").arg(tk.accent.name()));
    ptLay->addWidget(m_ptBaseSegLabel);

    m_btnDeletePlacedPt = new ElaPushButton(QString::fromUtf8("删除"), this);
    m_btnDeletePlacedPt->setFixedSize(46, kFieldH);
    m_btnDeletePlacedPt->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_btnDeletePlacedPt->setCursor(Qt::PointingHandCursor);
    m_btnDeletePlacedPt->setToolTip(cad::ui::TooltipFormatter::action(
        cad::cmd::texts::kDeletePlacedPoint,
        QStringLiteral("仅删除当前放置点，不影响基准线段与端点。快捷键 Del。")));
    connect(m_btnDeletePlacedPt, &QAbstractButton::clicked, this, &PlacedPointStripBar::onDeletePlacedPointClicked);
    ptLay->addWidget(m_btnDeletePlacedPt);

    m_ptHint = new ElaText(QString(), 11, this);
    m_ptHint->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;").arg(tk.text2.name()));
    ptLay->addWidget(m_ptHint);

    ptLay->addStretch();

    connect(m_ptDistEdit, &QLineEdit::textEdited, this, &PlacedPointStripBar::onPlacedPointDistEdited);
    connect(m_ptAngleEdit, &QLineEdit::textEdited, this, &PlacedPointStripBar::onPlacedPointAngleEdited);
    connect(m_ptNameEdit, &QLineEdit::editingFinished, this, [this] {
        if (m_isPlacedPointMode && !m_placePointSession) applyPlacedPointEdits();
    });

    for (auto* edit : {m_ptNameEdit, m_ptDistEdit, m_ptAngleEdit})
        edit->installEventFilter(this);
}

bool PlacedPointStripBar::setTarget(const QUuid& blockId, const QUuid& pointId)
{
    if (blockId.isNull() || pointId.isNull() || !m_paramDoc) {
        clearTarget();
        return false;
    }
    auto* blk = m_paramDoc->findBlock(blockId);
    if (!blk) { clearTarget(); return false; }
    auto* pt = blk->findPoint(pointId);
    if (!pt) { clearTarget(); return false; }

    m_isPlacedPointMode = true;
    m_placePointSession = false;
    m_placedBlockId = blockId;
    m_placedPointId = pointId;

    m_ptSerialLabel->setText(cad::param::Serial::tag(pt->serial));
    m_ptNameEdit->setText(pt->name);
    // 公式优先回显: 否则提交会把公式覆盖成数值 (CAN-P1-17 收口)。
    m_ptDistEdit->setText(pt->interpOffsetDistFormula.isEmpty()
        ? cad::geo::Units::formatCm(pt->interpOffsetDist)
        : pt->interpOffsetDistFormula);
    m_ptAngleEdit->setText(pt->interpOffsetAngleFormula.isEmpty()
        ? cad::geo::Units::formatDegValue(pt->interpOffsetAngle)
        : pt->interpOffsetAngleFormula);

    if (auto* seg = blk->findSegment(pt->hostSegmentId)) {
        QString tag = cad::param::Serial::tag(seg->serial);
        if (!seg->name.isEmpty()) tag += QStringLiteral("·") + seg->name;
        m_ptBaseSegLabel->setText(tag);
    } else {
        m_ptBaseSegLabel->setText(QString::fromUtf8("无宿主"));
    }

    m_btnDeletePlacedPt->setEnabled(true);
    m_ptHint->setText(QString::fromUtf8("Enter 应用修改 | Del/删除按钮 仅删除放置点"));

    show();
    update();
    return true;
}

void PlacedPointStripBar::clearTarget()
{
    m_isPlacedPointMode = false;
    m_placePointSession = false;
    m_placedBlockId = QUuid();
    m_placedPointId = QUuid();
    hide();
}

void PlacedPointStripBar::beginSession(const QString& baseSegName)
{
    m_isPlacedPointMode = true;
    m_placePointSession = true;
    m_placedBlockId = QUuid();
    m_placedPointId = QUuid();

    m_ptSerialLabel->setText(QString::fromUtf8("放置"));
    m_ptNameEdit->clear();
    m_ptDistEdit->clear();
    m_ptAngleEdit->clear();
    m_ptBaseSegLabel->setText(baseSegName.isEmpty() ? QString::fromUtf8("未选择 (请点击端点/线段)") : baseSegName);
    m_btnDeletePlacedPt->setEnabled(false);
    m_ptHint->setText(QString::fromUtf8("Tab 切换输入框 | 有输入锁方向 | 回车确认"));

    show();
    update();
}

void PlacedPointStripBar::updateValues(double distCm, double angleDeg, bool distLocked, bool angleLocked)
{
    if (!m_placePointSession) return;
    if (!distLocked && !m_ptDistEdit->hasFocus()) {
        m_ptDistEdit->setText(cad::geo::Units::formatNumberTrimmed(distCm));
    }
    if (!angleLocked && !m_ptAngleEdit->hasFocus()) {
        m_ptAngleEdit->setText(cad::geo::Units::formatDegValue(angleDeg));
    }
}

void PlacedPointStripBar::endSession()
{
    if (m_placePointSession) {
        clearTarget();
    }
}

void PlacedPointStripBar::focusNextField()
{
    if (!m_isPlacedPointMode) return;
    if (m_ptDistEdit->hasFocus()) {
        m_ptAngleEdit->setFocus();
        m_ptAngleEdit->selectAll();
    } else {
        m_ptDistEdit->setFocus();
        m_ptDistEdit->selectAll();
    }
}

bool PlacedPointStripBar::hasInputFocus() const
{
    return (m_ptNameEdit && m_ptNameEdit->hasFocus())
        || (m_ptDistEdit && m_ptDistEdit->hasFocus())
        || (m_ptAngleEdit && m_ptAngleEdit->hasFocus());
}

void PlacedPointStripBar::onPlacedPointDistEdited(const QString& text)
{
    if (!m_placePointSession) {
        applyPlacedPointEdits();
        return;
    }
    // 与线段编辑条同一判读入口 (CAN-P1-17): 数值直接发, 公式求值后作为
    // 实时预览值 (cm 域); 提交时公式原文写回 interpOffsetDistFormula。
    const auto parsed = cad::geo::parseNumberOrFormula(text);
    if (parsed.isNumber) {
        emit distChanged(parsed.value, true);
        return;
    }
    if (parsed.formula.isEmpty()) {
        emit distChanged(0.0, false);
        return;
    }
    if (m_paramDoc) {
        const auto r = cad::param::ConditionEngine::evaluate(
            parsed.formula, m_paramDoc->parameters(), m_paramDoc->conditions());
        if (r.ok) emit distChanged(r.value, true);
    }
}

void PlacedPointStripBar::onPlacedPointAngleEdited(const QString& text)
{
    if (!m_placePointSession) {
        applyPlacedPointEdits();
        return;
    }
    // 角度域: parseAngleText 剥掉度数符号, 其余同距离框 (CAN-P1-17)。
    const auto parsed = cad::geo::parseAngleText(text);
    if (parsed.isNumber) {
        emit angleChanged(parsed.value, true);
        return;
    }
    if (parsed.formula.isEmpty()) {
        emit angleChanged(0.0, false);
        return;
    }
    if (m_paramDoc) {
        const auto r = cad::param::ConditionEngine::evaluate(
            parsed.formula, m_paramDoc->parameters(), m_paramDoc->conditions());
        if (r.ok) emit angleChanged(r.value, true);
    }
}

void PlacedPointStripBar::applyPlacedPointEdits()
{
    if (!m_paramDoc || m_placedBlockId.isNull() || m_placedPointId.isNull()) return;
    auto* blk = m_paramDoc->findBlock(m_placedBlockId);
    if (!blk) return;
    auto* pt = blk->findPoint(m_placedPointId);
    if (!pt) return;

    cad::param::ParamPoint oldPt = *pt;
    cad::param::ParamPoint newPt = oldPt;

    newPt.name = m_ptNameEdit->text().trimmed();
    const auto dist = cad::geo::parseNumberOrFormula(m_ptDistEdit->text());
    if (dist.isNumber) {
        newPt.interpOffsetDist = cad::geo::Units::cmToMm(dist.value);
        newPt.interpOffsetDistFormula.clear();
    } else {
        newPt.interpOffsetDistFormula = dist.formula;
    }
    const auto angle = cad::geo::parseAngleText(m_ptAngleEdit->text());
    if (angle.isNumber) {
        newPt.interpOffsetAngle = angle.value;
        newPt.interpOffsetAngleFormula.clear();
    } else {
        newPt.interpOffsetAngleFormula = angle.formula;
    }

    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::EditPlacedPointCommand(m_paramDoc, m_placedBlockId, oldPt, newPt));
    } else {
        *pt = newPt;
        m_paramDoc->resolveAll();
    }
}

void PlacedPointStripBar::onDeletePlacedPointClicked()
{
    if (!m_paramDoc || m_placedBlockId.isNull() || m_placedPointId.isNull()) return;
    const QUuid bId = m_placedBlockId;
    const QUuid pId = m_placedPointId;
    clearTarget();
    if (m_undoStack) {
        m_undoStack->push(new cad::cmd::RemovePlacedPointCommand(m_paramDoc, bId, pId));
    }
    emit deleted(bId, pId);
}

void PlacedPointStripBar::applyTheme()
{
    const auto& tk = cad::ui::Theme::tokens();
    if (m_ptSerialLabel) {
        m_ptSerialLabel->setStyleSheet(QStringLiteral(
            "#stripPtSerial { %1 font-size: 10px; font-weight: 600; color: %2; background: %3; border: 1px solid %4; border-radius: 2px; padding: 2px 6px; }")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily, tk.text1.name(), tk.surface3.name(), tk.borderStrong.name()));
    }
    if (m_ptHint) {
        m_ptHint->setStyleSheet(QStringLiteral("font-size: 11px; color: %1;").arg(tk.text2.name()));
    }
    update();
}

bool PlacedPointStripBar::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        ke->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);

        if (watched == m_ptNameEdit || watched == m_ptDistEdit || watched == m_ptAngleEdit) {
            if (m_placePointSession) {
                if (ke->key() == Qt::Key_Escape) {
                    emit cancelRequested();
                    return true;
                }
                if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                    emit committed();
                    return true;
                }
                if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
                    focusNextField();
                    return true;
                }
            } else if (m_isPlacedPointMode) {
                if (ke->key() == Qt::Key_Escape) {
                    clearTarget();
                    emit returnFocusRequested();
                    return true;
                }
                if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                    applyPlacedPointEdits();
                    emit returnFocusRequested();
                    return true;
                }
                if (ke->key() == Qt::Key_Tab) {
                    if (watched == m_ptNameEdit)       { m_ptDistEdit->setFocus();  m_ptDistEdit->selectAll(); }
                    else if (watched == m_ptDistEdit)  { m_ptAngleEdit->setFocus(); m_ptAngleEdit->selectAll(); }
                    else if (watched == m_ptAngleEdit) { m_ptNameEdit->setFocus();  m_ptNameEdit->selectAll(); }
                    return true;
                }
                if (ke->key() == Qt::Key_Backtab) {
                    if (watched == m_ptAngleEdit)      { m_ptDistEdit->setFocus();  m_ptDistEdit->selectAll(); }
                    else if (watched == m_ptDistEdit)  { m_ptNameEdit->setFocus();  m_ptNameEdit->selectAll(); }
                    else if (watched == m_ptNameEdit)  { m_ptAngleEdit->setFocus(); m_ptAngleEdit->selectAll(); }
                    return true;
                }
            }
            return QWidget::eventFilter(watched, event);
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace cad::app
