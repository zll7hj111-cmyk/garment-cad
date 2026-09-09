#include "MainWindow.h"
#include "ToolDockStyle.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

#include "ElaMenu.h"
#include "ElaScrollPageArea.h"
#include "ElaToolButton.h"
#include "parametric/ParamDocument.h"
#include "tools/ToolRegistry.h"
#include "ui/IconHelper.h"
#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"
#include "ui/UiStrings.h"

namespace {

[[nodiscard]] ElaIconType::IconName toolDockIcon(cad::tools::ToolType type)
{
    static const QHash<cad::tools::ToolType, ElaIconType::IconName> kIcons = {
        { cad::tools::ToolType::Select,       ElaIconType::ArrowPointer },
        { cad::tools::ToolType::SmartPen,     ElaIconType::PenNib },
        { cad::tools::ToolType::CurveEdit,    ElaIconType::BezierCurve },
        { cad::tools::ToolType::Rotate,       ElaIconType::Rotate },
        { cad::tools::ToolType::Break,        ElaIconType::Scissors },
        { cad::tools::ToolType::Intersection, ElaIconType::Intersection },
        { cad::tools::ToolType::Measure,      ElaIconType::RulerCombined },
        { cad::tools::ToolType::AngleMeasure, ElaIconType::Angle },
        { cad::tools::ToolType::PlacePoint,   ElaIconType::Diamond },
        { cad::tools::ToolType::Circle,       ElaIconType::Compass },
    };
    if (auto it = kIcons.constFind(type); it != kIcons.constEnd())
        return *it;

    Q_ASSERT_X(false, "toolDockIcon",
               "tool dock icon missing for a registered ToolType");
    qWarning() << "MainWindow: no dock icon registered for ToolType"
               << static_cast<int>(type) << "- falling back to ArrowPointer.";
    return ElaIconType::ArrowPointer;
}

} // namespace

void MainWindow::setupToolBar()
{
    auto* dockLay = new QHBoxLayout(m_toolDock);
    dockLay->setContentsMargins(10, 5, 10, 5);
    dockLay->setSpacing(0);

    m_toolPill = new ElaScrollPageArea(m_toolDock);
    m_toolPill->setObjectName(QStringLiteral("toolPill"));
    m_toolPill->setFixedHeight(46);
    m_toolPill->setStyleSheet(QStringLiteral(
        "QFrame#toolPill {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 2px;"
        "}"
    ).arg(cad::ui::Theme::tokens().surface.name(), cad::ui::Theme::tokens().borderStrong.name()));
    auto* pillLay = new QHBoxLayout(m_toolPill);
    pillLay->setContentsMargins(4, 4, 4, 4);
    pillLay->setSpacing(2);

    for (cad::tools::ToolType type : m_toolOrder) {
        auto* btn = new ElaToolButton(m_toolPill);
        btn->setDefaultAction(m_toolActions.value(type));
        btn->setElaIcon(toolDockIcon(type));
        btn->setIsTransparent(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyle(new cad::app::ToolDockStyle(btn));
        m_toolButtons.append(btn);
        pillLay->addWidget(btn);
    }

    m_layerChipSeparator = new QFrame(m_toolPill);
    m_layerChipSeparator->setFrameShape(QFrame::VLine);
    m_layerChipSeparator->setFixedWidth(1);
    pillLay->addSpacing(6);
    pillLay->addWidget(m_layerChipSeparator);
    pillLay->addSpacing(2);

    m_layerChip = new ElaToolButton(m_toolPill);
    m_layerChip->setIsTransparent(true);
    m_layerChip->setCursor(Qt::PointingHandCursor);
    m_layerChip->setPopupMode(QToolButton::InstantPopup);
    m_layerChip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pillLay->addWidget(m_layerChip);

    dockLay->addStretch(1);
    dockLay->addWidget(m_toolPill, 0, Qt::AlignHCenter);
    dockLay->addStretch(1);

    refreshLayerChip();
}

void MainWindow::refreshLayerChip()
{
    if (!m_layerChip || !m_paramDoc) return;

    const auto& tk = cad::ui::Theme::tokens();
    if (m_layerChipSeparator)
        m_layerChipSeparator->setStyleSheet(
            QStringLiteral("background: %1; border: none;").arg(tk.border.name()));

    auto dotIcon = [](const QColor& c) {
        QPixmap pm(12, 12);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QRectF(1.5, 1.5, 9.0, 9.0));
        return QIcon(pm);
    };

    const QUuid cur = m_paramDoc->layersView().activeLayer();
    const auto* layer = m_paramDoc->layersView().byId(cur);
    if (!layer) return;

    const bool aux = layer->type == cad::param::LayerType::Auxiliary;
    const QColor dot = aux ? tk.success : tk.text1;
    m_layerChip->setIcon(dotIcon(dot));
    m_layerChip->setText(aux ? QStringLiteral("辅助：%1").arg(layer->name)
                             : QStringLiteral("图层：%1").arg(layer->name));
    m_layerChip->setToolTip(cad::ui::TooltipFormatter::actionWithShortcut(
        cad::ui::str::kActiveLayer,
        QStringLiteral("点击快速切换"),
        QStringLiteral("当前图层：%1。所有新创建的图元均归属此图层。").arg(layer->name)));

    if (m_layerChipMenu) {
        m_layerChip->setMenu(nullptr);
        delete m_layerChipMenu;
        m_layerChipMenu = nullptr;
    }
    auto* menu = new ElaMenu(m_layerChip);
    for (const auto& l : m_paramDoc->layers()) {
        const bool lAux = l.type == cad::param::LayerType::Auxiliary;
        QString label = l.name;
        if (lAux) label += QStringLiteral("（辅助计算层）");
        auto* act = menu->addAction(dotIcon(lAux ? tk.success : tk.text1), label);
        act->setCheckable(true);
        act->setChecked(l.id == cur);
        connect(act, &QAction::triggered, this,
                [this, id = l.id]() { m_paramDoc->setActiveLayer(id); });
    }
    m_layerChipMenu = menu;
    m_layerChip->setMenu(menu);
}

void MainWindow::refreshToolIcons()
{
    const auto& t = cad::ui::Theme::tokens();
    const QColor normal = t.text2;
    const QColor active = t.text1;
    auto& reg = cad::tools::ToolRegistry::instance();
    for (cad::tools::ToolType type : m_toolOrder) {
        if (const auto* d = reg.descriptor(type)) {
            if (auto* act = m_toolActions.value(type)) {
                act->setIcon(cad::ui::IconHelper::icon2State(
                    d->iconName, normal, active));
            }
        }
    }
}
