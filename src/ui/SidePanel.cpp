#include "SidePanel.h"

#include <QVBoxLayout>
#include <QTabBar>
#include <QStackedWidget>

#include "VariablePanel.h"
#include "LayerPanel.h"
#include "GroupPanel.h"

SidePanel::SidePanel(cad::param::ParamDocument* paramDoc, CanvasScene* scene,
                     QWidget* parent)
    : QDockWidget(QString::fromUtf8("面板"), parent)
{
    setObjectName(QStringLiteral("SidePanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ===== Top-level tab bar: 变量 / 图层 / 组 =====
    m_tabBar = new QTabBar(central);
    m_tabBar->addTab(QString::fromUtf8("变量"));
    m_tabBar->addTab(QString::fromUtf8("图层"));
    m_tabBar->addTab(QString::fromUtf8("组"));
    m_tabBar->setExpanding(true);
    m_tabBar->setDrawBase(false);
    m_tabBar->setCursor(Qt::PointingHandCursor);
    m_tabBar->setStyleSheet(
        "QTabBar::tab {"
        "  font-size: 13px; color: #7F8C8D; background: #F4F6F7;"
        "  border: none; padding: 9px 12px;"
        "}"
        "QTabBar::tab:selected {"
        "  font-weight: bold; color: #2E86C1; background: #FFFFFF;"
        "  border-bottom: 2px solid #2E86C1;"
        "}"
        "QTabBar::tab:hover:!selected { color: #34495E; background: #EAECEE; }");
    layout->addWidget(m_tabBar);

    // ===== Stacked pages =====
    m_stack = new QStackedWidget(central);
    m_variablePanel = new VariablePanel(paramDoc, m_stack);
    m_layerPanel    = new LayerPanel(paramDoc, m_stack);
    m_groupPanel    = new GroupPanel(paramDoc, m_stack);
    m_stack->addWidget(m_variablePanel);  // index 0
    m_stack->addWidget(m_layerPanel);     // index 1
    m_stack->addWidget(m_groupPanel);     // index 2
    layout->addWidget(m_stack, 1);

    setWidget(central);

    connect(m_tabBar, &QTabBar::currentChanged,
            m_stack, &QStackedWidget::setCurrentIndex);
}
