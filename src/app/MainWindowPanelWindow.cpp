#include "MainWindow.h"
#include "ContextStrip.h"

#include <QBoxLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "ElaNavigationBar.h"
#include "ElaTabBar.h"
#include "canvas/BlockItem.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "parametric/ParamDocument.h"
#include "ui/ComponentTab.h"
#include "ui/IconHelper.h"
#include "ui/LayerPanel.h"
#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"
#include "ui/VariablePanel.h"
#include "ui/UiStrings.h"

namespace {

QIcon panelActiveDot(const QColor& color)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(2.5, 2.5, 9, 9));
    return QIcon(pm);
}

} // namespace

void MainWindow::setupPages()
{
    setStackSwitchMode(ElaWindowType::StackSwitchMode::None);

    m_panelWindow = new QWidget(this, Qt::Tool);
    m_panelWindow->setAttribute(Qt::WA_StyledBackground, true);
    m_panelWindow->setObjectName(QStringLiteral("panelFloatingWindow"));
    m_panelWindow->setWindowTitle(QString::fromUtf8("面板"));
    m_panelWindow->setMinimumSize(300, 360);
    m_panelWindow->setMaximumWidth(480);
    m_panelWindow->installEventFilter(this);
    auto* winLay = new QVBoxLayout(m_panelWindow);
    winLay->setContentsMargins(8, 8, 8, 8);
    winLay->setSpacing(6);

    m_panelHeader = new QWidget(m_panelWindow);
    m_panelHeader->setAttribute(Qt::WA_StyledBackground, true);
    auto* headerLay = new QHBoxLayout(m_panelHeader);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(2);

    m_panelBigBar = new ElaTabBar(m_panelHeader);
    m_panelBigBar->addTab(cad::ui::str::kVariable);
    m_panelBigBar->addTab(cad::ui::str::kLayer);
    m_panelBigBar->addTab(QStringLiteral("组件"));
    m_panelBigBar->setTabSize(QSize(76, 32));
    m_panelBigBar->setExpanding(true);
    m_panelBigBar->setUsesScrollButtons(false);
    m_panelBigBar->setElideMode(Qt::ElideNone);
    m_panelBigBar->setDrawBase(false);
    m_panelBigBar->setTabsClosable(false);
    m_panelBigBar->setMovable(false);
    m_panelBigBar->setAcceptDrops(false);
    m_panelBigBar->setCursor(Qt::PointingHandCursor);
    m_panelBigBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    headerLay->addWidget(m_panelBigBar, 1);

    const auto& tk = cad::ui::Theme::tokens();
    const QString ghostBtnQss = QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent;"
        "  border-radius: 2px; padding: 1px; }"
        "QToolButton:hover { background: %1; border: 1px solid %2; }")
        .arg(tk.surface2.name(), tk.border.name());
    m_hidePanelBtn = new QToolButton(m_panelHeader);
    m_hidePanelBtn->setCursor(Qt::PointingHandCursor);
    m_hidePanelBtn->setFixedSize(24, 24);
    m_hidePanelBtn->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("折叠面板"),
        QStringLiteral("收起右侧属性与图层面板，扩展视口绘图区")));
    m_hidePanelBtn->setStyleSheet(ghostBtnQss);
    m_hidePanelBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("x"), tk.text2));
    connect(m_hidePanelBtn, &QToolButton::clicked, this, [this]() {
        m_panelWindow->hide();
    });
    headerLay->addWidget(m_hidePanelBtn);
    winLay->addWidget(m_panelHeader);

    m_panelStack = new QStackedWidget(m_panelWindow);
    m_panelStack->setAttribute(Qt::WA_StyledBackground, true);
    m_variablePanel = new cad::ui::VariablePanel(m_paramDoc, m_panelStack);
    m_variablePanel->setUndoStack(m_undoStack);
    m_panelStack->addWidget(m_variablePanel);

    m_layerPanel = new LayerPanel(m_paramDoc, m_panelStack);
    m_panelStack->addWidget(m_layerPanel);

    m_componentTab = new cad::ui::ComponentTab(m_paramDoc, m_panelStack);
    m_componentTab->setUndoStack(m_undoStack);
    m_panelStack->addWidget(m_componentTab);

    winLay->addWidget(m_panelStack, 1);
    refreshPanelChrome();

    connect(m_panelBigBar, &QTabBar::currentChanged, this, [this](int category) {
        if (m_panelStack && m_panelStack->currentIndex() != category)
            m_panelStack->setCurrentIndex(category);
        if (!m_tabSyncGuard && panelVisible())
            syncPanelTabs();
    });

    m_pageTabs = new ElaTabBar(this);
    m_pageTabs->addTab(QStringLiteral("画布"));
    m_pageTabs->addTab(cad::ui::str::kVariable);
    m_pageTabs->addTab(cad::ui::str::kLayer);
    m_pageTabs->setTabSize(QSize(88, 30));
    m_pageTabs->setIconSize(QSize(14, 14));
    m_pageTabs->setTabToolTip(1,
        cad::ui::TooltipFormatter::actionWithShortcut(
            QStringLiteral("变量面板"),
            QStringLiteral("P"),
            QStringLiteral("显示或折叠右侧参数化变量、公式与测量面板")));
    m_pageTabs->setTabToolTip(2,
        cad::ui::TooltipFormatter::actionWithShortcut(
            QStringLiteral("图层面板"),
            QStringLiteral("L"),
            QStringLiteral("显示或折叠右侧设计图层与线段管理面板")));
    m_pageTabs->setTabsClosable(false);
    m_pageTabs->setMovable(false);
    m_pageTabs->setAcceptDrops(false);

    m_pageStack = new QStackedWidget(this);
    m_pageStack->addWidget(m_centerContainer);
    connect(m_pageTabs, &QTabBar::currentChanged,
            this, &MainWindow::onPageTabChanged);

    m_editBand = new QWidget(this);
    m_editBand->setObjectName(QStringLiteral("stripBand"));
    m_editBand->setAttribute(Qt::WA_StyledBackground, true);
    m_editBand->setStyleSheet(QStringLiteral(
        "#stripBand { background: %1; border: 1px solid %2; border-radius: 4px; }")
        .arg(tk.surface.name(), tk.border.name()));
    auto* bandLay = new QHBoxLayout(m_editBand);
    bandLay->setContentsMargins(6, 4, 10, 4);
    bandLay->setSpacing(8);

    m_contextStrip = new cad::app::ContextStrip(m_paramDoc, m_editBand);
    m_contextStrip->setUndoStack(m_undoStack);
    m_contextStrip->setCanvasView(m_canvasView);
    m_contextStrip->hide();
    bandLay->addWidget(m_contextStrip, 1);
    m_contextStrip->installEventFilter(this);
    m_editBand->hide();

    auto* pageHost = new QWidget(this);
    auto* pageLay = new QVBoxLayout(pageHost);
    pageLay->setContentsMargins(8, 6, 8, 0);
    pageLay->setSpacing(6);
    pageLay->addWidget(m_pageTabs);
    pageLay->addWidget(m_pageStack, 1);
    pageLay->addWidget(m_editBand);
    setCentralCustomWidget(pageHost);

    if (QWidget* innerCentral = pageHost->parentWidget()) {
        if (auto* box = qobject_cast<QBoxLayout*>(innerCentral->layout())) {
            box->setStretch(0, 1);
            box->setStretch(1, 0);
            if (auto* emptyContainer = innerCentral->findChild<QStackedWidget*>())
                emptyContainer->hide();
        }
    }

    if (auto* navBar = findChild<ElaNavigationBar*>())
        navBar->hide();

    for (int i = 0; i < m_pageTabs->count(); ++i) {
        auto* sc = new QShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)), this);
        sc->setContext(Qt::ApplicationShortcut);
        const int idx = i;
        connect(sc, &QShortcut::activated, this, [this, idx] {
            if (m_pageTabs)
                m_pageTabs->setCurrentIndex(idx);
        });
    }

    {
        QSettings settings;
        settings.remove(QStringLiteral("panel/docked"));
        const QByteArray geo =
            settings.value(QStringLiteral("panel/geo")).toByteArray();
        if (!geo.isEmpty()) {
            m_panelWindow->restoreGeometry(geo);
            m_panelWindowPositioned = true;
        }
    }

    connect(m_variablePanel, &cad::ui::VariablePanel::highlightBlockRequested,
            this, [this](const QUuid& blockId) {
        auto* bi = m_canvasScene->findBlockItem(blockId);
        if (!bi) return;
        bi->setToolLocked(true);
        m_canvasScene->refreshAllBlockItems();
        QTimer::singleShot(1500, this, [this, blockId]() {
            auto* item = m_canvasScene->findBlockItem(blockId);
            if (item) {
                item->setToolLocked(false);
                m_canvasScene->refreshAllBlockItems();
            }
        });
    });

    connect(m_variablePanel, &cad::ui::VariablePanel::highlightMeasureRequested,
            this, [this](const QUuid& measureId) {
        const auto* mv = m_paramDoc->findMeasure(measureId);
        if (!mv) return;

        if (!mv->ownerBlockId.isNull()) {
            m_canvasScene->highlightMeasureBlock(measureId, mv->ownerBlockId);
        } else if (!m_canvasScene->flashMeasure(mv->blockA, mv->pointA,
                                                 mv->blockB, mv->pointB,
                                                 mv->kind, measureId)) {
            m_canvasScene->highlightMeasureBlock(measureId, mv->blockA);
        }
    });

    connect(m_variablePanel, &cad::ui::VariablePanel::highlightAngleMeasureRequested,
            this, [this](const QUuid& angleMeasureId) {
        const auto* am = m_paramDoc->findAngleMeasure(angleMeasureId);
        if (!am) return;
        if (!m_canvasScene->flashAngleMeasure(am->blockA, am->segmentA,
                                              am->blockB, am->segmentB,
                                              am->flipA, am->flipB,
                                              angleMeasureId)) {
            m_canvasScene->highlightMeasureBlock(angleMeasureId, am->blockA);
        }
    });

    connect(m_variablePanel, &cad::ui::VariablePanel::clearMeasureHighlightRequested,
            this, [this](const QUuid& measureId) {
        m_canvasScene->clearMeasureHighlight(measureId);
    });
}

void MainWindow::onPageTabChanged(int index)
{
    if (m_tabSyncGuard || !m_pageTabs || !m_pageStack)
        return;

    if (index == 0) {
        if (panelVisible()) {
            const int cat = m_panelBigBar->currentIndex();
            m_tabSyncGuard = true;
            m_pageTabs->setCurrentIndex(cat < 2 ? 1 + cat : 0);
            m_tabSyncGuard = false;
        }
        return;
    }

    const int category = index - 1;
    if (panelVisible() && m_panelBigBar->currentIndex() == category) {
        m_panelWindow->hide();
        return;
    }

    m_panelBigBar->setCurrentIndex(category);
    ensurePanelWindowPosition();
    m_panelWindow->show();
    m_panelWindow->raise();
    m_panelWindow->activateWindow();
    syncPanelTabs();
}

void MainWindow::syncPanelTabs()
{
    if (!m_pageTabs || !m_panelWindow || !m_panelBigBar)
        return;

    const bool open = panelVisible();
    const int cat = m_panelBigBar->currentIndex();
    const int target = (open && cat < 2) ? 1 + cat : 0;
    if (m_pageTabs->currentIndex() != target) {
        m_tabSyncGuard = true;
        m_pageTabs->setCurrentIndex(target);
        m_tabSyncGuard = false;
    }

    for (int i = 1; i < m_pageTabs->count(); ++i) {
        const bool active = open && cat < 2 && (i - 1) == cat;
        m_pageTabs->setTabIcon(i,
            active ? panelActiveDot(cad::ui::Theme::tokens().text1) : QIcon());
    }
}

bool MainWindow::panelVisible() const
{
    return m_panelWindow && m_panelWindow->isVisible();
}

void MainWindow::refreshPanelChrome()
{
    const auto& tk = cad::ui::Theme::tokens();
    if (m_toolPill) {
        m_toolPill->setStyleSheet(QStringLiteral(
            "QFrame#toolPill {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 2px;"
            "}"
        ).arg(tk.surface.name(), tk.borderStrong.name()));
    }
    if (m_panelWindow) {
        m_panelWindow->setStyleSheet(QStringLiteral(
            "QWidget#panelFloatingWindow { background: %1; border: 1px solid %2; border-radius: 4px; }"
        ).arg(tk.surface.name(), tk.border.name()));
    }
    if (m_panelHeader) {
        m_panelHeader->setStyleSheet(QStringLiteral("background: %1;").arg(tk.surface.name()));
    }
    if (m_panelStack) {
        m_panelStack->setStyleSheet(QStringLiteral(
            "QStackedWidget { background: %1; border: none; }"
        ).arg(tk.surface.name()));
    }
    if (!m_hidePanelBtn)
        return;
    const QString ghostBtnQss = QStringLiteral(
        "QToolButton { background: transparent; border: 1px solid transparent;"
        "  border-radius: 2px; padding: 1px; }"
        "QToolButton:hover { background: %1; border: 1px solid %2; }")
        .arg(tk.surface2.name(), tk.border.name());
    m_hidePanelBtn->setStyleSheet(ghostBtnQss);
    m_hidePanelBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("x"), tk.text2));
}

void MainWindow::ensurePanelWindowPosition()
{
    if (!m_panelWindow)
        return;
    if (m_panelWindowPositioned)
        return;
    m_panelWindowPositioned = true;

    QScreen* scr = screen() ? screen() : QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 800);

    const int w = qBound(320, avail.width() / 5, 380);
    const int h = qBound(400, avail.height() * 8 / 10, 660);
    m_panelWindow->resize(w, h);

    const QRect mainGeo = geometry();
    int x = mainGeo.right() - m_panelWindow->width() - 12;
    int y = mainGeo.top() + 96;
    if (x + m_panelWindow->width() > avail.right())
        x = avail.right() - m_panelWindow->width() - 8;
    if (y + m_panelWindow->height() > avail.bottom())
        y = avail.bottom() - m_panelWindow->height() - 8;
    x = qMax(x, avail.left() + 8);
    y = qMax(y, avail.top() + 8);
    m_panelWindow->move(x, y);
}
