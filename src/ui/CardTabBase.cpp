#include "CardTabBase.h"

#include <QVBoxLayout>
#include <QPushButton>
#include "ElaScrollArea.h"
#include "ElaText.h"
#include "VirtualCardList.h"
#include "Theme.h"

namespace cad::ui {

CardTabBase::CardTabBase(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    setAttribute(Qt::WA_StyledBackground, true);
}

void CardTabBase::setUndoStack(QUndoStack* stack)
{
    m_undoStack = stack;
}

void CardTabBase::setEmptyHintVisible(bool visible)
{
    if (m_emptyBox)
        m_emptyBox->setVisible(visible);
}

void CardTabBase::setupListPage(const QString& emptyTitle, const QString& emptyGuide,
                                const QString& ghostAddText,
                                const std::function<void()>& onGhostAdd)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_scroll = new ElaScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: %1; border: none; }")
        .arg(cad::ui::Theme::tokens().canvasBg.name()));
    m_scroll->setObjectName(QStringLiteral("cardListArea"));

    m_container = new QWidget();
    m_container->setStyleSheet(QStringLiteral(
        "background: %1;").arg(cad::ui::Theme::tokens().canvasBg.name()));
    m_container->setObjectName(QStringLiteral("cardListContainer"));
    auto* layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Empty state: 18px Semibold title + 11px guide + optional ghost button
    m_emptyBox = new QWidget(m_container);
    auto* emptyLay = new QVBoxLayout(m_emptyBox);
    emptyLay->setContentsMargins(0, 36, 0, 36);
    emptyLay->setSpacing(8);
    const auto& tk = cad::ui::Theme::tokens();

    m_emptyTitle = new ElaText(emptyTitle, cad::ui::ThemeTokens::FontXl, m_emptyBox);
    m_emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setStyleSheet(QStringLiteral(
        "font-size: %1px; font-weight: 600; color: %2; background: transparent;")
        .arg(QString::number(cad::ui::ThemeTokens::FontXl), tk.text1.name()));
    emptyLay->addWidget(m_emptyTitle);

    m_emptyGuide = new ElaText(emptyGuide, 11, m_emptyBox);
    m_emptyGuide->setObjectName(QStringLiteral("emptyGuide"));
    m_emptyGuide->setAlignment(Qt::AlignCenter);
    m_emptyGuide->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: %1; background: transparent;")
        .arg(tk.text2.name()));
    emptyLay->addWidget(m_emptyGuide);

    if (!ghostAddText.isEmpty()) {
        m_emptyGhost = new QPushButton(ghostAddText, m_emptyBox);
        m_emptyGhost->setObjectName(QStringLiteral("emptyGhost"));
        m_emptyGhost->setCursor(Qt::PointingHandCursor);
        m_emptyGhost->setFixedHeight(28);
        m_emptyGhost->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: 1px dashed %1;"
            "  border-radius: 4px; padding: 0 14px; font-size: 11px; font-weight: 500; color: %2; }"
            "QPushButton:hover { background: %3; border: 1px solid %2; color: %2; }")
            .arg(tk.borderStrong.name(), tk.accent.name(), tk.accentTint.name()));
        if (onGhostAdd)
            connect(m_emptyGhost, &QPushButton::clicked, m_emptyBox, onGhostAdd);
        emptyLay->addWidget(m_emptyGhost, 0, Qt::AlignHCenter);
    }
    layout->addWidget(m_emptyBox);

    // Virtualized card host
    m_host = new VirtualCardList(m_container);
    layout->addWidget(m_host);
    m_host->init(m_scroll);

    layout->addStretch();

    m_scroll->setWidget(m_container);
    outerLayout->addWidget(m_scroll);
}

void CardTabBase::applyTheme()
{
    const auto& tk = cad::ui::Theme::tokens();
    setStyleSheet(QStringLiteral("background: %1;").arg(tk.surface.name()));

    if (m_scroll) {
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea { background: %1; border: none; }")
            .arg(tk.canvasBg.name()));
    }
    if (m_container) {
        m_container->setStyleSheet(QStringLiteral("background: %1;")
            .arg(tk.canvasBg.name()));
    }
    if (m_emptyTitle) {
        m_emptyTitle->setStyleSheet(QStringLiteral(
            "font-size: %1px; font-weight: 600; color: %2; background: transparent;")
            .arg(QString::number(cad::ui::ThemeTokens::FontXl), tk.text1.name()));
    }
    if (m_emptyGuide) {
        m_emptyGuide->setStyleSheet(QStringLiteral(
            "font-size: 11px; color: %1; background: transparent;")
            .arg(tk.text2.name()));
    }
    if (m_emptyGhost) {
        m_emptyGhost->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: 1px dashed %1;"
            "  border-radius: 4px; padding: 0 14px; font-size: 11px; font-weight: 500; color: %2; }"
            "QPushButton:hover { background: %3; border: 1px solid %2; color: %2; }")
            .arg(tk.borderStrong.name(), tk.accent.name(), tk.accentTint.name()));
    }

    if (m_host)
        m_host->rebuildAll();

    update();
}

} // namespace cad::ui
