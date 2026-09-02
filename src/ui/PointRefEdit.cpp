#include "ui/PointRefEdit.h"

#include <algorithm>

#include "ElaDialog.h"
#include <QFocusEvent>
#include <QKeyEvent>
#include "ElaText.h"
#include <QListWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "ui/ElaDialogButtons.h"
#include "ui/Theme.h"
#include "parametric/Serial.h"

namespace cad::ui {

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
    setPlaceholderText(QString::fromUtf8("P#/L#/名称…"));  // §6.6 名称复用
    setToolTip(QString::fromUtf8(
        "输入点编号（如 P12）、线段编号（如 L1）或名称，回车确认。"
        "同名会弹窗选择，Esc 取消"));
    // 输入点编号（如 P12）或完整序列号，回车确认。同名点会弹窗选择，Esc 取消
    setMinimumWidth(90);
    // 行内统一高度 (线段属性对话框: ElaLineEdit/ElaComboBox 原生 35px,
    // 未设时此控件缩成 ~24px → "输入框大小不一")。
    setFixedHeight(30);
    const auto& tk = cad::ui::Theme::tokens();
    // [autoEcho="true"] = 自动态灰显回显 (§6.4): 值来自模型自动跟随而非手填。
    setStyleSheet(QStringLiteral(
        "QLineEdit { color: %1; border: 1px solid %2; border-radius: 3px;"
        "  padding: 2px 5px; background: %3; font-size: 11px; }"
        "QLineEdit:focus { border-color: %1; background: %4; }"
        "QLineEdit[autoEcho=\"true\"] { color: %5; }")
        .arg(tk.text1.name(), tk.borderStrong.name(), tk.surface2.name(),
             tk.surface.name(), tk.text3.name()));
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

void PointRefEdit::setAutoEcho(bool on)
{
    if (m_autoEcho == on) return;
    m_autoEcho = on;
    setProperty("autoEcho", on);
    // 动态属性翻转后必须 unpolish/polish, QSS 规则才会重新求值。
    style()->unpolish(this);
    style()->polish(this);
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

    if (!m_restrictBlockId.isNull()) {
        // 对齐点模式 (2026-09 设计修正): 只在本块点集内匹配, 不匹配线段。
        const auto* blk = m_doc->findBlock(m_restrictBlockId);
        if (!blk) return results;
        for (const auto& pt : blk->points) {
            bool hit = pt.serial == input
                || cad::param::Serial::tag(pt.serial).compare(
                       input, Qt::CaseInsensitive) == 0
                || (!pt.name.isEmpty()
                    && pt.name.compare(input, Qt::CaseInsensitive) == 0);
            if (!hit) continue;
            Match m;
            m.blockId = blk->id;
            m.pointId = pt.id;
            m.serial = pt.serial;
            m.pointName = pt.name;
            m.segLabel = segmentLabelFor(*blk, pt.id);
            results.push_back(std::move(m));
        }
        return results;
    }

    for (const auto& block : m_doc->blocks()) {
        if (block.id == m_excludeBlockId) continue;
        if (block.isShadow) continue;  // 影子不可交互/不可被引用 (R4, 拆开影子基准)
        for (const auto& pt : block.points) {
            bool hit = false;
            // 1. Full serial exact match (case-sensitive).
            if (pt.serial == input)
                hit = true;
            // 2. Tag match (case-insensitive): "P12" matches "...P12".
            else if (cad::param::Serial::tag(pt.serial).compare(
                         input, Qt::CaseInsensitive) == 0)
                hit = true;
            // 3. Name match (case-insensitive, §6.6 名称复用): "肩点" matches.
            else if (!pt.name.isEmpty()
                     && pt.name.compare(input, Qt::CaseInsensitive) == 0)
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

    // 线段匹配 (§6.6 名称复用): "L#" / 完整编号 / 线段名称 → 该线段的起点
    // (起点的出口方向 = 线段方向, 与旧「基准线」语义一致)。点匹配优先。
    for (const auto& block : m_doc->blocks()) {
        if (block.id == m_excludeBlockId) continue;
        if (block.isShadow) continue;  // 影子不可交互 (R4, 拆开影子基准)
        for (const auto& seg : block.segments) {
            bool hit = seg.serial == input
                || cad::param::Serial::tag(seg.serial).compare(
                       input, Qt::CaseInsensitive) == 0
                || (!seg.name.isEmpty()
                    && seg.name.compare(input, Qt::CaseInsensitive) == 0);
            if (!hit) continue;
            Match m;
            m.blockId = block.id;
            m.pointId = seg.startPointId;
            m.serial = seg.serial;
            m.pointName = seg.name;
            m.segLabel = QString::fromUtf8("线段");
            results.push_back(std::move(m));
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
    ElaDialog dlg(this);
    dlg.setWindowTitle(QString::fromUtf8("\u627e\u5230\u591a\u4e2a\u540c\u540d\u70b9"));  // 找到多个同名点
    dlg.setMinimumWidth(360);
    auto* lay = new QVBoxLayout(&dlg);
    auto* hint = new ElaText(QString::fromUtf8(
        "\u591a\u4e2a\u70b9\u5171\u4eab\u7f16\u53f7\u201c%1\u201d\uff0c\u8bf7\u9009\u62e9\uff1a").arg(input), 13, &dlg);
    // 多个点共享编号"xxx"，请选择：
    hint->setStyleSheet("font-size:12px;");
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

    lay->addWidget(cad::ui::makeDialogButtons(&dlg).row);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);

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
    setAutoEcho(false);   // 成功解析 = 用户意图 (2026-09 自动回填守卫).
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
    const auto& tk = cad::ui::Theme::tokens();
    setStyleSheet(QStringLiteral(
        "QLineEdit { color: %1; border: 1px solid %1; border-radius: 3px;"
        "  padding: 2px 5px; background: rgba(220,38,38,32); }")
        .arg(tk.danger.name()));
    setToolTip(QString::fromUtf8("\u672a\u627e\u5230\u8be5\u70b9\uff08\u6392\u9664\u672c\u7ebf\u6bb5\u6240\u5c5e Block\uff09"));
    // 未找到该点（排除本线段所属 Block）
    QTimer::singleShot(900, this, [this, saved] {
        setStyleSheet(saved);
        revertDisplay();
    });
}

void PointRefEdit::focusInEvent(QFocusEvent* e)
{
    // 用户点进来自带编辑意图 (焦点全选即替换): 灰显自动解除。
    setAutoEcho(false);
    QLineEdit::focusInEvent(e);
    selectAll();
}

void PointRefEdit::focusOutEvent(QFocusEvent* e)
{
    // 用户编辑后未按回车就离开: **合法单解自动提交** (与 Enter 同语义) ——
    // 原实现一律 revertDisplay, 把"点进输入框→输入→点走"的真实操作无声
    // 清空: 属性面板角度基准点2 因此"永远填不进" (2026-09 E:\4.gcad L2
    // 报告; 合法内容同样被清, 用户在点2 输入后点走或 Tab 即丢失)。
    // 多解不弹选择框 (失焦事件里弹模态框有焦点死锁风险, 还原让用户重来);
    // 未改动/非法输入照旧还原。
    const QString input = text().trimmed();
    if (!input.isEmpty() && input != m_displayText) {
        const auto matches = findMatches(input);
        if (matches.size() == 1) {
            applyMatch(matches.front());
            QLineEdit::focusOutEvent(e);
            return;
        }
    }
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

} // namespace cad::ui
