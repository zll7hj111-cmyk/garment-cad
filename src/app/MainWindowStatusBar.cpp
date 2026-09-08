#include "MainWindow.h"

#include <QFontMetrics>
#include <QMenu>
#include <QPoint>
#include <QSizePolicy>
#include <QTimer>
#include <QToolButton>

#include "ElaStatusBar.h"
#include "ElaText.h"
#include "parametric/ParamDocument.h"
#include "tools/ToolRegistry.h"
#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"

void MainWindow::setupStatusBar()
{
    auto* sb = new ElaStatusBar(this);
    setStatusBar(sb);

    m_toolHintLabel = new ElaText(QString(), 13, this);
    m_toolHintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_toolHintLabel->installEventFilter(this);
    sb->addWidget(m_toolHintLabel, 1);
    setToolHint(cad::tools::toolHintText(cad::tools::ToolType::Select));

    m_flashLabel = new ElaText(QString(), 12, this);
    m_flashLabel->setObjectName(QStringLiteral("coordLabel"));
    m_flashLabel->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);
    m_flashLabel->hide();

    m_diagBadge = new QToolButton(this);
    m_diagBadge->setText(QStringLiteral("⚠"));
    m_diagBadge->setCursor(Qt::PointingHandCursor);
    m_diagBadge->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("连接拓扑诊断"), QStringLiteral("存在连接问题，点击查看明细"), true));
    m_diagBadge->hide();
    connect(m_diagBadge, &QToolButton::clicked, this, [this]() {
        using cad::param::ResolveDiagnostic;
        const auto& diags = m_paramDoc->diagnostics();
        if (diags.empty()) return;
        QMenu menu(this);
        for (const auto& d : diags) {
            QString text;
            switch (d.kind) {
            case ResolveDiagnostic::Kind::DanglingBlock:
                text = QString::fromUtf8("连接引用了不存在的线段"); break;
            case ResolveDiagnostic::Kind::DanglingPoint:
                text = QString::fromUtf8("连接引用了不存在的端点"); break;
            case ResolveDiagnostic::Kind::NotConverged:
                text = QString::fromUtf8("连接存在冲突或循环，无法稳定求解"); break;
            }
            auto* act = menu.addAction(text);
            act->setDisabled(true);
        }
        menu.exec(m_diagBadge->mapToGlobal(
            QPoint(0, m_diagBadge->height())));
    });

    m_coordLabel = new ElaText("X: 0.000  Y: 0.000", 12, this);
    m_coordLabel->setObjectName(QStringLiteral("coordLabel"));
    m_coordLabel->setMinimumWidth(220);
    m_coordLabel->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);

    m_zoomLabel = new ElaText(QString::fromUtf8("缩放: 100%"), 12, this);
    m_zoomLabel->setObjectName(QStringLiteral("zoomLabel"));
    m_zoomLabel->setMinimumWidth(100);
    m_zoomLabel->setStyleSheet(cad::ui::ThemeTokens::kMonospaceFamily);

    m_flashTimer = new QTimer(this);
    m_flashTimer->setSingleShot(true);
    connect(m_flashTimer, &QTimer::timeout, this,
            [this]() { m_flashLabel->hide(); });

    sb->addPermanentWidget(m_flashLabel);
    sb->addPermanentWidget(m_diagBadge);
    sb->addPermanentWidget(m_coordLabel);
    sb->addPermanentWidget(m_zoomLabel);

    refreshStatusBarChrome();
}

void MainWindow::refreshStatusBarChrome()
{
    if (!m_diagBadge)
        return;
    const auto& tk = cad::ui::Theme::tokens();
    m_diagBadge->setStyleSheet(QStringLiteral(
        "QToolButton { background: %1; color: #FFFFFF; border: none;"  // color-allow: danger 实心徽章白字——danger 底已两模式可读，白字跨模式固定（token 无 white）
        "  border-radius: 2px; padding: 1px 8px; font-size: 11px; font-weight: 600; }"
        "QToolButton:hover { background: %2; }")
        .arg(tk.danger.name(), tk.danger.darker(110).name()));
}

void MainWindow::onDocumentChanged()
{
    const auto& diags = m_paramDoc->diagnostics();
    if (diags.empty()) {
        m_diagBadge->setText(QStringLiteral("⚠"));
        m_diagBadge->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("连接拓扑诊断"), QStringLiteral("全部连接正常，无拓扑问题"), false));
        m_diagBadge->hide();
        return;
    }

    using cad::param::ResolveDiagnostic;
    QString first;
    switch (diags.front().kind) {
    case ResolveDiagnostic::Kind::DanglingBlock:
        first = QString::fromUtf8("连接引用了不存在的线段");
        break;
    case ResolveDiagnostic::Kind::DanglingPoint:
        first = QString::fromUtf8("连接引用了不存在的端点");
        break;
    case ResolveDiagnostic::Kind::NotConverged:
        first = QString::fromUtf8("连接存在冲突或循环，无法稳定求解");
        break;
    }

    m_diagBadge->setText(QStringLiteral("⚠ %1").arg(diags.size()));
    m_diagBadge->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("连接拓扑异常 (共 %1 处)").arg(diags.size()),
        QStringLiteral("首项：%1（点击查看全部明细）").arg(first),
        true));
    m_diagBadge->show();
}

void MainWindow::onToolHintOverride(const QString& hint)
{
    if (!hint.isEmpty())
        setToolHint(hint);
}

void MainWindow::setToolHint(const QString& text)
{
    m_toolHintFull = text;
    if (!m_toolHintLabel) return;
    m_toolHintLabel->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("工具操作指引"), text, false));
    applyToolHintElide();
}

void MainWindow::applyToolHintElide()
{
    if (!m_toolHintLabel || m_toolHintFull.isEmpty()) return;
    const int avail = m_toolHintLabel->width() - 8;
    if (avail <= 0) return;
    const QFontMetrics fm(m_toolHintLabel->fontMetrics());
    const QString elided = fm.elidedText(m_toolHintFull, Qt::ElideRight, avail);
    if (elided == m_toolHintLabel->text()) return;
    m_toolHintLabel->setText(elided);
}

void MainWindow::flashStatus(const QString& text, const QColor& color, int ms)
{
    if (!m_flashLabel || !m_flashTimer)
        return;
    m_flashLabel->setText(text);
    m_flashLabel->setStyleSheet(
        QStringLiteral("%1 font-weight: 600; color: %2; background: transparent;")
            .arg(cad::ui::ThemeTokens::kMonospaceFamily, color.name()));
    m_flashLabel->show();
    m_flashTimer->start(ms);
}
