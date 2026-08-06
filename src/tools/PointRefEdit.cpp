#include "PointRefEdit.h"

#include <algorithm>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"

namespace cad::tools {

namespace {

/// Find the segment that owns a point (start/end/aux) — for display context.
QString segmentLabelFor(const cad::param::Block& block, const QUuid& pointId)
{
    for (const auto& seg : block.segments) {
        const bool owns = seg.startPointId == pointId || seg.endPointId == pointId
            || std::find(seg.auxPointIds.begin(), seg.auxPointIds.end(), pointId)
                   != seg.auxPointIds.end();
        if (!owns) continue;
        QString label = cad::param::Serial::tag(seg.serial);
        if (!seg.name.isEmpty())
            label += QStringLiteral("\u00b7") + seg.name;
        return label;
    }
    return QString();
}

} // namespace

PointRefEdit::PointRefEdit(cad::param::ParamDocument* doc, QWidget* parent)
    : QLineEdit(parent)
    , m_doc(doc)
{
    setPlaceholderText(QString::fromUtf8("\u8f93\u5165P\u7f16\u53f7\u2026"));  // 输入P编号…
    setToolTip(QString::fromUtf8(
        "\u8f93\u5165\u70b9\u7f16\u53f7\uff08\u5982 P12\uff09\u6216\u5b8c\u6574\u5e8f\u5217\u53f7\uff0c\u56de\u8f66\u786e\u8ba4\u3002"
        "\u540c\u540d\u70b9\u4f1a\u5f39\u7a97\u9009\u62e9\uff0cEsc \u53d6\u6d88"));
    // 输入点编号（如 P12）或完整序列号，回车确认。同名点会弹窗选择，Esc 取消
    setMinimumWidth(90);
    setStyleSheet(
        "QLineEdit { color: #1565C0; border: 1px solid #BBDEFB; border-radius: 3px;"
        "  padding: 2px 5px; background: #F8FBFF; }"
        "QLineEdit:focus { border-color: #1565C0; background: #FFFFFF; }");
}

void PointRefEdit::setPoint(const QUuid& blockId, const QUuid& pointId)
{
    m_blockId = blockId;
    m_pointId = pointId;
    m_displayText = displayTextFor(blockId, pointId);
    setText(m_displayText);
    setCursorPosition(0);
}

void PointRefEdit::clearPoint()
{
    m_blockId = QUuid();
    m_pointId = QUuid();
    m_displayText.clear();
    clear();
}

QString PointRefEdit::displayTextFor(const QUuid& blockId, const QUuid& pointId) const
{
    if (!m_doc || pointId.isNull()) return QString();
    const auto* block = m_doc->findBlock(blockId);
    const auto* pt = block ? block->findPoint(pointId) : nullptr;
    if (!pt) return QString::fromUtf8("\u5df2\u5220\u9664");  // 已删除
    QString label = cad::param::Serial::tag(pt->serial);
    if (!pt->name.isEmpty())
        label += QStringLiteral("\u00b7") + pt->name;
    return label;
}

std::vector<PointRefEdit::Match> PointRefEdit::findMatches(const QString& text) const
{
    std::vector<Match> results;
    if (!m_doc || text.isEmpty()) return results;

    const QString input = text.trimmed();

    for (const auto& block : m_doc->blocks()) {
        if (block.id == m_excludeBlockId) continue;
        for (const auto& pt : block.points) {
            bool hit = false;
            // 1. Full serial exact match (case-sensitive).
            if (pt.serial == input)
                hit = true;
            // 2. Tag match (case-insensitive): "P12" matches "...P12".
            else if (cad::param::Serial::tag(pt.serial).compare(
                         input, Qt::CaseInsensitive) == 0)
                hit = true;

            if (hit) {
                Match m;
                m.blockId = block.id;
                m.pointId = pt.id;
                m.serial = pt.serial;
                m.pointName = pt.name;
                m.segLabel = segmentLabelFor(block, pt.id);
                results.push_back(std::move(m));
            }
        }
    }
    return results;
}

void PointRefEdit::commitInput()
{
    const QString input = text().trimmed();

    // Unchanged → nothing to do.
    if (input == m_displayText) { revertDisplay(); return; }
    if (input.isEmpty()) { revertDisplay(); return; }

    auto matches = findMatches(input);

    if (matches.empty()) {
        flashError();
        return;
    }

    if (matches.size() == 1) {
        applyMatch(matches.front());
        return;
    }

    // --- Disambiguation dialog: show full serials ---
    QDialog dlg(this);
    dlg.setWindowTitle(QString::fromUtf8("\u627e\u5230\u591a\u4e2a\u540c\u540d\u70b9"));  // 找到多个同名点
    dlg.setMinimumWidth(360);
    auto* lay = new QVBoxLayout(&dlg);
    auto* hint = new QLabel(QString::fromUtf8(
        "\u591a\u4e2a\u70b9\u5171\u4eab\u7f16\u53f7\u201c%1\u201d\uff0c\u8bf7\u9009\u62e9\uff1a").arg(input), &dlg);
    // 多个点共享编号"xxx"，请选择：
    hint->setStyleSheet("color:#555; font-size:12px;");
    lay->addWidget(hint);

    auto* list = new QListWidget(&dlg);
    for (const auto& m : matches) {
        QString label = m.serial;  // Full ID always shown.
        if (!m.pointName.isEmpty())
            label += QStringLiteral(" \u00b7 ") + m.pointName;
        if (!m.segLabel.isEmpty())
            label += QStringLiteral("  \uff08%1\uff09").arg(m.segLabel);
        auto* item = new QListWidgetItem(label, list);
        item->setData(Qt::UserRole, m.blockId);
        item->setData(Qt::UserRole + 1, m.pointId);
    }
    list->setCurrentRow(0);
    lay->addWidget(list, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    lay->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) {
        revertDisplay();
        return;
    }
    auto* chosen = list->currentItem();
    if (!chosen) { revertDisplay(); return; }

    for (const auto& m : matches) {
        if (m.blockId == chosen->data(Qt::UserRole).toUuid()
            && m.pointId == chosen->data(Qt::UserRole + 1).toUuid()) {
            applyMatch(m);
            return;
        }
    }
    revertDisplay();
}

void PointRefEdit::applyMatch(const Match& m)
{
    m_blockId = m.blockId;
    m_pointId = m.pointId;
    m_displayText = displayTextFor(m.blockId, m.pointId);
    setText(m_displayText);
    setCursorPosition(0);
    emit pointResolved(m.blockId, m.pointId);
}

void PointRefEdit::revertDisplay()
{
    setText(m_displayText);
    setCursorPosition(0);
}

void PointRefEdit::flashError()
{
    const QString saved = styleSheet();
    setStyleSheet(
        "QLineEdit { color: #C62828; border: 1px solid #EF9A9A; border-radius: 3px;"
        "  padding: 2px 5px; background: #FFEBEE; }");
    setToolTip(QString::fromUtf8("\u672a\u627e\u5230\u8be5\u70b9\uff08\u6392\u9664\u672c\u7ebf\u6bb5\u6240\u5c5e Block\uff09"));
    // 未找到该点（排除本线段所属 Block）
    QTimer::singleShot(900, this, [this, saved] {
        setStyleSheet(saved);
        revertDisplay();
    });
}

void PointRefEdit::focusInEvent(QFocusEvent* e)
{
    QLineEdit::focusInEvent(e);
    selectAll();
}

void PointRefEdit::focusOutEvent(QFocusEvent* e)
{
    // Revert on focus loss: only explicit Enter commits (prevents accidental
    // changes and avoids opening a modal dialog from a focus event).
    revertDisplay();
    QLineEdit::focusOutEvent(e);
}

void PointRefEdit::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        revertDisplay();
        clearFocus();
        return;
    }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        commitInput();
        clearFocus();
        return;
    }
    QLineEdit::keyPressEvent(e);
}

} // namespace cad::tools
