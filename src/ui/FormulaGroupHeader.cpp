#include "FormulaGroupHeader.h"

#include "FormulaCard.h"
#include "IconHelper.h"

#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDrag>
#include <QApplication>
#include <QStyleOption>
#include <QPainter>
#include <QTimer>

namespace {
const QColor kCaretColor(0x5D, 0x6D, 0x7E);
} // namespace

FormulaGroupHeader::FormulaGroupHeader(const QUuid& groupId, const QString& name,
                                       bool collapsed, int count, QWidget* parent)
    : QWidget(parent)
    , m_groupId(groupId)
    , m_collapsed(collapsed)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("FormulaGroupHeader"));
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(28);
    setDropHighlight(false);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 4, 0);
    layout->setSpacing(5);

    m_caret = new QLabel(this);
    m_caret->setFixedSize(12, 12);
    m_caret->setStyleSheet("background: transparent;");
    layout->addWidget(m_caret, 0);

    m_nameLabel = new QLabel(name, this);
    m_nameLabel->setToolTip(QStringLiteral("双击重命名"));
    m_nameLabel->setStyleSheet(
        "font-size: 12px; font-weight: bold; color: #34495E; background: transparent;");
    m_nameLabel->installEventFilter(this);
    layout->addWidget(m_nameLabel, 0);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setFixedHeight(20);
    m_nameEdit->setStyleSheet(
        "QLineEdit { font-size: 12px; font-weight: bold; color: #34495E;"
        "  background: #FFF; border: 1px solid #2E86C1; border-radius: 4px; padding: 0 4px; }");
    m_nameEdit->setVisible(false);
    layout->addWidget(m_nameEdit, 1);

    m_countLabel = new QLabel(this);
    m_countLabel->setStyleSheet(
        "font-size: 11px; color: #85929E; background: transparent;");
    layout->addWidget(m_countLabel, 0);

    layout->addStretch();

    m_dissolveBtn = new QToolButton(this);
    m_dissolveBtn->setIcon(cad::ui::IconHelper::icon2State(
        QStringLiteral("x"), QColor(0xB0, 0xB0, 0xB0), Qt::white));
    m_dissolveBtn->setIconSize(QSize(11, 11));
    m_dissolveBtn->setToolTip(QStringLiteral("解散分组（成员回到未分组）"));
    m_dissolveBtn->setFixedSize(18, 18);
    m_dissolveBtn->setCursor(Qt::PointingHandCursor);
    m_dissolveBtn->setVisible(false);
    m_dissolveBtn->setStyleSheet(
        "QToolButton { background: transparent; border: none; border-radius: 9px; }"
        "QToolButton:hover { background: #E74C3C; }");
    layout->addWidget(m_dissolveBtn, 0);

    connect(m_dissolveBtn, &QToolButton::clicked, this,
            [this]() { emit dissolveRequested(m_groupId); });
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &FormulaGroupHeader::commitRename);

    setCount(count);
    updateCaret();
}

void FormulaGroupHeader::setName(const QString& name)
{
    m_nameLabel->setText(name);
}

void FormulaGroupHeader::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    updateCaret();
}

void FormulaGroupHeader::setCount(int count)
{
    m_countLabel->setText(QStringLiteral("(%1)").arg(count));
}

void FormulaGroupHeader::startRename()
{
    m_nameEdit->setText(m_nameLabel->text());
    m_nameLabel->setVisible(false);
    m_nameEdit->setVisible(true);
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void FormulaGroupHeader::commitRename()
{
    if (!m_nameEdit->isVisible())
        return;
    const QString newName = m_nameEdit->text().trimmed();
    m_nameEdit->setVisible(false);
    m_nameLabel->setVisible(true);
    if (!newName.isEmpty() && newName != m_nameLabel->text())
        emit renameRequested(m_groupId, newName);
}

void FormulaGroupHeader::updateCaret()
{
    const QString name = m_collapsed ? QStringLiteral("caret-right")
                                     : QStringLiteral("caret-down");
    m_caret->setPixmap(cad::ui::IconHelper::iconByName(name, kCaretColor)
                           .pixmap(12, 12));
}

void FormulaGroupHeader::setDropHighlight(bool on)
{
    setStyleSheet(QStringLiteral(
        "QWidget#FormulaGroupHeader {"
        "  background-color: %1;"
        "  border: 1px %2;"
        "  border-radius: 5px;"
        "}"
        "QWidget#FormulaGroupHeader:hover { background-color: #E3E7EB; }")
        .arg(on ? QStringLiteral("#D6EAF8") : QStringLiteral("#E9EDF0"),
             on ? QStringLiteral("solid #2E86C1") : QStringLiteral("solid transparent")));
}

// ─── Mouse: click = toggle (deferred), drag = reorder ───

void FormulaGroupHeader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        m_dragging = false;
        m_pressPos = event->pos();
    }
    QWidget::mousePressEvent(event);
}

void FormulaGroupHeader::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressed && !m_dragging
        && (event->buttons() & Qt::LeftButton)
        && (event->pos() - m_pressPos).manhattanLength()
               >= QApplication::startDragDistance()) {
        m_dragging = true;
        auto* mime = new QMimeData();
        mime->setData(kDragMimeType, m_groupId.toByteArray());
        auto* drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(grab());
        drag->setHotSpot(event->pos());
        drag->exec(Qt::MoveAction);
        m_pressed = false;
    }
    QWidget::mouseMoveEvent(event);
}

void FormulaGroupHeader::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed && !m_dragging) {
        // 单击立即 toggle — NO double-click guard. Deferring the toggle for
        // the system double-click interval made the cards appear ~500ms late
        // (the perceived lag). A double-click simply toggles twice (ending
        // where it started), which is harmless for a fold header; double-
        // clicking the NAME label still opens rename.
        emit toggleRequested(m_groupId);
    }
    m_pressed = false;
    QWidget::mouseReleaseEvent(event);
}

void FormulaGroupHeader::enterEvent(QEnterEvent*)
{
    m_dissolveBtn->setVisible(true);
}

void FormulaGroupHeader::leaveEvent(QEvent*)
{
    m_dissolveBtn->setVisible(false);
}

bool FormulaGroupHeader::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_nameLabel && event->type() == QEvent::MouseButtonDblClick) {
        startRename();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

// ─── Drop target: card dropped on the header joins this group ───

void FormulaGroupHeader::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(FormulaCard::kDragMimeType)) {
        setDropHighlight(true);
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void FormulaGroupHeader::dragLeaveEvent(QDragLeaveEvent*)
{
    setDropHighlight(false);
}

void FormulaGroupHeader::dropEvent(QDropEvent* event)
{
    setDropHighlight(false);
    if (event->mimeData()->hasFormat(FormulaCard::kDragMimeType)) {
        const QUuid formulaId(
            QString::fromLatin1(event->mimeData()->data(FormulaCard::kDragMimeType)));
        if (!formulaId.isNull()) {
            emit formulaDropped(formulaId, m_groupId);
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}
