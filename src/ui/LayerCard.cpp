#include "ui/LayerCard.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QEnterEvent>

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "ui/Theme.h"
#include "ui/IconHelper.h"
#include "ui/TooltipFormatter.h"

namespace cad::ui {

// ---------------------------------------------------------------------------
// EyeToggle
// ---------------------------------------------------------------------------

EyeToggle::EyeToggle(QWidget* parent, int size)
    : QToolButton(parent)
    , m_size(size)
{
    setFixedSize(m_size, m_size);
    setCursor(Qt::PointingHandCursor);
    setCheckable(true);
    setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 2px; padding: 0; }"
        "QToolButton:hover { background: %1; }")
        .arg(cad::ui::Theme::tokens().surface2.name()));
    connect(this, &QToolButton::toggled, this, &EyeToggle::syncIcon);
    syncIcon();
}

void EyeToggle::applyTheme()
{
    setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 2px; padding: 0; }"
        "QToolButton:hover { background: %1; }")
        .arg(cad::ui::Theme::tokens().surface2.name()));
    syncIcon();
}

void EyeToggle::syncIcon()
{
    const bool on = isChecked();
    const auto& tk = cad::ui::Theme::tokens();
    const int iconPix = m_size >= 20 ? 14 : 12;
    setIcon(cad::ui::IconHelper::iconByName(
        on ? QStringLiteral("eye") : QStringLiteral("eye-closed"),
        on ? tk.text1 : tk.text3));
    setIconSize(QSize(iconPix, iconPix));
    setToolTip(cad::ui::TooltipFormatter::action(
        on ? QStringLiteral("图层已显示") : QStringLiteral("图层已隐藏"),
        on ? QStringLiteral("点击隐藏该图层全部线段") : QStringLiteral("点击显示该图层全部线段")));
}

// ---------------------------------------------------------------------------
// SegmentRow
// ---------------------------------------------------------------------------

SegmentRow::SegmentRow(const QUuid& blockId, const cad::param::Segment& seg,
                       bool layerHidden, QWidget* parent)
    : QWidget(parent)
    , m_blockId(blockId)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName(QStringLiteral("SegmentRow"));
    setContextMenuPolicy(Qt::CustomContextMenu);
    setFixedHeight(26);

    const auto& tk = cad::ui::Theme::tokens();
    setStyleSheet(QStringLiteral(
        "QWidget#SegmentRow { background: transparent; border-radius: 3px; }"
        "QWidget#SegmentRow:hover { background: %1; }")
        .arg(tk.surface2.name()));

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 1, 6, 1);
    lay->setSpacing(6);

    m_eye = new EyeToggle(this, 18);
    m_eye->setChecked(seg.visible);
    lay->addWidget(m_eye);

    m_nameLbl = new ElaText(QString(), 12, this);
    lay->addWidget(m_nameLbl, 1);

    m_tagLbl = new ElaText(cad::param::Serial::tag(seg.serial), 10, this);
    m_tagLbl->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("线段编号"), seg.serial, false));
    m_tagLbl->setStyleSheet(QStringLiteral(
        "font-family: 'Consolas','Courier New',monospace; font-size: 10px;"
        "color: %1; background: %2; border: 1px solid %3; border-radius: 2px; padding: 1px 4px;")
        .arg(tk.text2.name(), tk.surface2.name(), tk.border.name()));
    lay->addWidget(m_tagLbl);

    m_moreBtn = new QToolButton(this);
    m_moreBtn->setText(QStringLiteral("···"));
    m_moreBtn->setFixedSize(18, 18);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("线段操作"),
        QStringLiteral("重命名、修改线型属性或删除线段")));
    m_moreBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 2px;"
        "  color: %1; font-weight: bold; font-size: 11px; padding: 0; }"
        "QToolButton:hover { background: %2; color: %3; }")
        .arg(tk.text3.name(), tk.surface3.name(), tk.text1.name()));
    m_moreBtn->setVisible(false);
    lay->addWidget(m_moreBtn);

    connect(m_moreBtn, &QToolButton::clicked, this, [this] {
        emit customContextMenuRequested(mapFromGlobal(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height()))));
    });

    updateNameVisual(seg.name, seg.visible);
    (void)layerHidden;
}

void SegmentRow::setRowInfo(const QString& name, bool visible)
{
    m_eye->blockSignals(true);
    m_eye->setChecked(visible);
    m_eye->blockSignals(false);
    updateNameVisual(name, visible);
}

void SegmentRow::updateNameVisual(const QString& name, bool visible)
{
    const auto& tk = cad::ui::Theme::tokens();
    if (name.isEmpty()) {
        m_nameLbl->setText(QString::fromUtf8("未命名线段"));
        m_nameLbl->setStyleSheet(QStringLiteral(
            "font-size: 11px; color: %1; background: transparent;")
            .arg(tk.text3.name()));
    } else {
        m_nameLbl->setText(name);
        m_nameLbl->setStyleSheet(QStringLiteral(
            "font-size: 12px; color: %1; background: transparent;")
            .arg(visible ? tk.text1.name() : tk.text3.name()));
    }
}

void SegmentRow::enterEvent(QEnterEvent*)
{
    m_moreBtn->setVisible(true);
}

void SegmentRow::leaveEvent(QEvent*)
{
    m_moreBtn->setVisible(false);
}

// ---------------------------------------------------------------------------
// LayerCard
// ---------------------------------------------------------------------------

LayerCard::LayerCard(int index, const QUuid& layerId, const QString& name, bool visible,
                     bool isActive, int segCount, bool isAux, QWidget* parent)
    : QFrame(parent)
    , m_index(index)
    , m_layerId(layerId)
    , m_isActive(isActive)
    , m_isAux(isAux)
    , m_visible(visible)
{
    setObjectName(QStringLiteral("LayerCard"));
    setAttribute(Qt::WA_StyledBackground, true);
    applyCardStyle();

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // --- Header widget ---
    m_headerWidget = new QWidget(this);
    m_headerWidget->setObjectName(QStringLiteral("cardHeader"));
    auto* hLay = new QHBoxLayout(m_headerWidget);
    hLay->setContentsMargins(8, 6, 8, 6);
    hLay->setSpacing(6);

    const auto& tk = cad::ui::Theme::tokens();

    // Caret button (expand/collapse)
    m_collapseBtn = new QToolButton(m_headerWidget);
    m_collapseBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("caret-down"), tk.text3));
    m_collapseBtn->setIconSize(QSize(10, 10));
    m_collapseBtn->setFixedSize(20, 20);
    m_collapseBtn->setCursor(Qt::PointingHandCursor);
    m_collapseBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 2px; padding: 0; }"
        "QToolButton:hover { background: %1; }")
        .arg(tk.surface2.name()));
    m_collapseBtn->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("展开/收起"),
        QStringLiteral("展开或收起当前图层下的线段列表")));
    hLay->addWidget(m_collapseBtn);

    // Eye toggle (layer visibility)
    m_eye = new EyeToggle(m_headerWidget, 20);
    m_eye->setChecked(visible);
    hLay->addWidget(m_eye);

    // Name label
    m_nameLabel = new ElaText(name, 12, m_headerWidget);
    m_nameLabel->setCursor(Qt::PointingHandCursor);
    m_nameLabel->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("图层名称"),
        QStringLiteral("单击切换为活动图层 · 双击重命名")));
    updateNameStyle();
    hLay->addWidget(m_nameLabel);

    // Inline name editor (hidden by default)
    m_nameEdit = new ElaLineEdit(m_headerWidget);
    m_nameEdit->setFixedHeight(22);
    m_nameEdit->setVisible(false);
    hLay->addWidget(m_nameEdit);

    // Aux layer badge
    if (isAux) {
        m_auxBadge = new ElaText(QString::fromUtf8("辅助计算"), 11, m_headerWidget);
        m_auxBadge->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("辅助计算层"),
            QStringLiteral("用于构造几何求值并发布测量参数供工作层使用（系统内置不可删除）"),
            false));
        m_auxBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tk.warning));
        hLay->addWidget(m_auxBadge);
    }

    // Active layer badge
    if (isActive) {
        m_activeBadge = new ElaText(QString::fromUtf8("活动"), 11, m_headerWidget);
        m_activeBadge->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("当前活动图层"),
            QStringLiteral("所有新绘制的线段与图元均保存于此图层"),
            false));
        m_activeBadge->setStyleSheet(QStringLiteral(
            "background-color: %1; color: %2; border-radius: 3px; padding: 1px 6px;"
            "font-size: 10px; font-weight: bold;")
            .arg(tk.accent.name(), tk.onAccent.name()));
        hLay->addWidget(m_activeBadge);
    }

    hLay->addStretch(1);

    // Segment count pill
    m_countPill = new ElaText(QString::number(segCount), 11, m_headerWidget);
    m_countPill->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("线段统计"),
        QStringLiteral("当前图层包含 %1 条线段").arg(segCount),
        false));
    m_countPill->setStyleSheet(QStringLiteral(
        "color: %1; background-color: %2; border: 1px solid %3; border-radius: 3px;"
        "padding: 1px 6px; font-size: 10px; font-family: 'Consolas','Courier New',monospace; font-weight: 500;")
        .arg(tk.text2.name(), tk.surface2.name(), tk.border.name()));
    hLay->addWidget(m_countPill);

    // Context menu button (···)
    m_menuBtn = new QToolButton(m_headerWidget);
    m_menuBtn->setText(QStringLiteral("···"));
    m_menuBtn->setFixedSize(20, 20);
    m_menuBtn->setCursor(Qt::PointingHandCursor);
    m_menuBtn->setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("图层操作"),
        QStringLiteral("移动图层层级、重命名或删除")));
    m_menuBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 2px;"
        "  color: %1; font-weight: bold; font-size: 12px; padding: 0; }"
        "QToolButton:hover { background: %2; color: %3; }")
        .arg(tk.text3.name(), tk.surface2.name(), tk.text1.name()));
    m_menuBtn->setVisible(false);
    hLay->addWidget(m_menuBtn);

    mainLay->addWidget(m_headerWidget);

    // --- Inner separator between header and segment list ---
    m_innerSep = new QFrame(this);
    m_innerSep->setFrameShape(QFrame::HLine);
    m_innerSep->setFixedHeight(1);
    m_innerSep->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(tk.border.name()));
    mainLay->addWidget(m_innerSep);

    // --- Segment list container ---
    m_segList = new QWidget(this);
    m_segListLayout = new QVBoxLayout(m_segList);
    m_segListLayout->setContentsMargins(16, 4, 8, 6);
    m_segListLayout->setSpacing(2);

    // Empty placeholder row
    m_emptyRow = new ElaText(QString::fromUtf8("该图层暂无线段（使用智能笔绘制）"), 11, m_segList);
    m_emptyRow->setAlignment(Qt::AlignCenter);
    m_emptyRow->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: %1; background: transparent; padding: 4px 0;")
        .arg(tk.text3.name()));
    m_emptyRow->setVisible(segCount == 0);
    m_segListLayout->addWidget(m_emptyRow);

    mainLay->addWidget(m_segList);

    // Inline rename finished
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this] {
        if (!m_nameEdit->isVisible()) return;
        const QString newName = m_nameEdit->text().trimmed();
        m_nameEdit->setVisible(false);
        m_nameLabel->setVisible(true);
        if (!newName.isEmpty() && newName != m_nameLabel->text()) {
            if (m_onRenameCommitted) {
                m_onRenameCommitted(m_index, newName);
            }
        }
    });

    connect(m_menuBtn, &QToolButton::clicked, this, [this] {
        if (m_onMenuRequested) {
            m_onMenuRequested(m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height())));
        }
    });
}

void LayerCard::startRename()
{
    m_nameLabel->setVisible(false);
    m_nameEdit->setText(m_nameLabel->text());
    m_nameEdit->setVisible(true);
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void LayerCard::addSegmentRow(SegmentRow* row)
{
    m_emptyRow->setVisible(false);
    m_segListLayout->addWidget(row);
}

void LayerCard::setLayerName(const QString& name)
{
    m_nameLabel->setText(name);
}

void LayerCard::setLayerVisible(bool visible)
{
    m_visible = visible;
    m_eye->blockSignals(true);
    m_eye->setChecked(visible);
    m_eye->blockSignals(false);
    updateNameStyle();
}

void LayerCard::setSegCount(int n)
{
    m_countPill->setText(QString::number(n));
    m_countPill->setToolTip(cad::ui::TooltipFormatter::status(
        QStringLiteral("线段统计"),
        QStringLiteral("当前图层包含 %1 条线段").arg(n),
        false));
    updateEmptyRowVisibility();
}

SegmentRow* LayerCard::findSegmentRow(const QUuid& blockId) const
{
    for (int i = 0; i < m_segListLayout->count(); ++i) {
        if (auto* row = dynamic_cast<SegmentRow*>(m_segListLayout->itemAt(i)->widget()))
            if (row->blockId() == blockId)
                return row;
    }
    return nullptr;
}

QList<SegmentRow*> LayerCard::segmentRows() const
{
    QList<SegmentRow*> rows;
    for (int i = 0; i < m_segListLayout->count(); ++i) {
        if (auto* row = dynamic_cast<SegmentRow*>(m_segListLayout->itemAt(i)->widget()))
            rows.append(row);
    }
    return rows;
}

void LayerCard::removeSegmentRow(const QUuid& blockId)
{
    for (int i = 0; i < m_segListLayout->count(); ++i) {
        auto* it = m_segListLayout->itemAt(i);
        if (auto* row = dynamic_cast<SegmentRow*>(it->widget())) {
            if (row->blockId() == blockId) {
                m_segListLayout->removeItem(it);
                row->deleteLater();
                delete it;
                break;
            }
        }
    }
    updateEmptyRowVisibility();
}

void LayerCard::setCollapsed(bool collapsed)
{
    m_segList->setVisible(!collapsed);
    m_innerSep->setVisible(!collapsed);
    const auto& tk = cad::ui::Theme::tokens();
    m_collapseBtn->setIcon(cad::ui::IconHelper::iconByName(
        collapsed ? QStringLiteral("caret-right") : QStringLiteral("caret-down"),
        tk.text3));
}

void LayerCard::applyTheme()
{
    applyCardStyle();
    updateNameStyle();
    m_eye->applyTheme();
    const auto& tk = cad::ui::Theme::tokens();
    m_innerSep->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(tk.border.name()));
    m_collapseBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; border: none; border-radius: 2px; padding: 0; }"
        "QToolButton:hover { background: %1; }")
        .arg(tk.surface2.name()));
    m_collapseBtn->setIcon(cad::ui::IconHelper::iconByName(
        m_segList->isVisible() ? QStringLiteral("caret-down") : QStringLiteral("caret-right"),
        tk.text3));
    m_countPill->setStyleSheet(QStringLiteral(
        "color: %1; background-color: %2; border: 1px solid %3; border-radius: 3px;"
        "padding: 1px 6px; font-size: 10px; font-family: 'Consolas','Courier New',monospace; font-weight: 500;")
        .arg(tk.text2.name(), tk.surface2.name(), tk.border.name()));
    if (m_auxBadge)
        m_auxBadge->setStyleSheet(cad::ui::Theme::badgeStyle(tk.warning));
    if (m_activeBadge)
        m_activeBadge->setStyleSheet(QStringLiteral(
            "background-color: %1; color: %2; border-radius: 3px; padding: 1px 6px;"
            "font-size: 10px; font-weight: bold;")
            .arg(tk.accent.name(), tk.onAccent.name()));
    update();
}

void LayerCard::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    if (!m_isActive && !m_isAux)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const auto& tk = cad::ui::Theme::tokens();
    const QColor barColor = m_isAux ? tk.warning : tk.accent;

    QPainterPath clip;
    clip.addRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 4.0, 4.0);
    p.setClipPath(clip);

    p.fillRect(QRectF(0, 0, 3.5, height()), barColor);
}

void LayerCard::enterEvent(QEnterEvent*)
{
    m_menuBtn->setVisible(true);
}

void LayerCard::leaveEvent(QEvent*)
{
    m_menuBtn->setVisible(false);
}

void LayerCard::applyCardStyle()
{
    const auto& tk = cad::ui::Theme::tokens();
    setStyleSheet(QStringLiteral(
        "QFrame#LayerCard {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 4px;"
        "}"
        "QFrame#LayerCard:hover { border: 1px solid %3; }")
        .arg(tk.surface.name(),
             m_isActive ? tk.borderStrong.name() : tk.border.name(),
             tk.borderStrong.name()));
}

void LayerCard::updateNameStyle()
{
    const auto& tk = cad::ui::Theme::tokens();
    m_nameLabel->setStyleSheet(QStringLiteral(
        "font-size: 12px; font-weight: %1; color: %2; background: transparent; padding: 2px 0;")
        .arg(m_isActive ? QStringLiteral("bold") : QStringLiteral("normal"),
             m_visible ? tk.text1.name() : tk.text3.name()));
}

void LayerCard::updateEmptyRowVisibility()
{
    int segRowCount = 0;
    for (int i = 0; i < m_segListLayout->count(); ++i) {
        if (dynamic_cast<SegmentRow*>(m_segListLayout->itemAt(i)->widget()))
            ++segRowCount;
    }
    m_emptyRow->setVisible(segRowCount == 0);
}

} // namespace cad::ui
