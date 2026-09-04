#include "LayerPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QUndoStack>

#include "ElaScrollArea.h"
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaMenu.h"
#include "ElaMsgBox.h"
#include "Theme.h"
#include "TooltipFormatter.h"
#include "IconHelper.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "document/commands/LayerCommands.h"
#include "document/commands/BlockCommands.h"
#include "ui/DeleteImpactConfirm.h"

namespace {

/// Build a full Props snapshot from a segment (for SetSegmentPropertyCommand).
cad::cmd::SetSegmentPropertyCommand::Props propsFrom(const cad::param::Segment& s)
{
    cad::cmd::SetSegmentPropertyCommand::Props p;
    p.name = s.name;
    p.role = s.role;
    p.lineStyle = s.lineStyle;
    p.color = s.color;
    p.weight = s.weight;
    p.visible = s.visible;
    p.showName = s.showName;
    p.showLength = s.showLength;
    p.lengthFormula = s.lengthFormula;
    return p;
}

} // namespace

#include "ui/LayerCard.h"

using cad::ui::EyeToggle;
using cad::ui::SegmentRow;
using cad::ui::LayerCard;

// ---------------------------------------------------------------------------
// LayerPanel
// ---------------------------------------------------------------------------

LayerPanel::LayerPanel(cad::param::ParamDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    setupUi();

    connect(m_doc, &cad::param::ParamDocument::layersChanged,
            this, &LayerPanel::refresh, Qt::QueuedConnection);
    connect(m_doc, &cad::param::ParamDocument::activeLayerChanged,
            this, [this](const QUuid&) { refresh(); }, Qt::QueuedConnection);
    // Blocks being added/removed changes per-layer membership counts.
    connect(m_doc, &cad::param::ParamDocument::structureChanged,
            this, &LayerPanel::refresh, Qt::QueuedConnection);
    // Segment renames / property edits should reflect immediately — in-place
    // sync, NOT a full rebuild (documentChanged fires after EVERY resolve).
    connect(m_doc, &cad::param::ParamDocument::documentChanged,
            this, &LayerPanel::syncFromDoc, Qt::QueuedConnection);

    refresh();
}

void LayerPanel::setupUi()
{
    const auto& tk = cad::ui::Theme::tokens();
    setStyleSheet(QStringLiteral("background: %1;").arg(tk.surface.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ===== Header: total layer count badge + new layer button =====
    auto* header = new QWidget(this);
    m_header = header;
    header->setStyleSheet(QStringLiteral("background: %1;").arg(tk.surface.name()));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(8);

    m_countLabel = new ElaText(QString(), 11, header);
    m_countLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: %1; background: %2; border: 1px solid %3; border-radius: 3px; padding: 2px 8px; font-weight: 500;")
        .arg(tk.text2.name(), tk.surface2.name(), tk.border.name()));
    headerLayout->addWidget(m_countLabel);

    headerLayout->addStretch();

    m_addBtn = new QPushButton(QString::fromUtf8("+ 新建图层"), header);
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setFixedHeight(24);
    m_addBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1; color: %2; border: 1px solid %3; border-radius: 3px;"
        "  padding: 0 10px; font-size: 11px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: %4; border: 1px solid %5; color: %6; }"
        "QPushButton:pressed { background: %7; }")
        .arg(tk.surface.name(), tk.text1.name(), tk.border.name(),
             tk.surface2.name(), tk.borderStrong.name(), tk.text1.name(),
             tk.surface3.name()));
    connect(m_addBtn, &QPushButton::clicked, this, &LayerPanel::onAddLayerClicked);
    headerLayout->addWidget(m_addBtn);

    layout->addWidget(header);

    // ===== Separator =====
    auto* sep = new QFrame(this);
    m_sep = sep;
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(tk.border.name()));
    layout->addWidget(sep);

    // ===== Scrollable card list =====
    m_scroll = new ElaScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("cardListArea"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea#cardListArea { background: %1; border: none; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: %2; border-radius: 2px; }"
        "QScrollBar::handle:vertical:hover { background: %3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
        .arg(tk.canvasBg.name(), tk.borderStrong.name(), tk.text3.name()));

    m_container = new QWidget(m_scroll);
    m_container->setObjectName(QStringLiteral("cardListContainer"));
    m_container->setStyleSheet(QStringLiteral("background: %1;").arg(tk.canvasBg.name()));
    m_listLayout = new QVBoxLayout(m_container);
    m_listLayout->setContentsMargins(8, 8, 8, 8);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch();

    m_scroll->setWidget(m_container);
    layout->addWidget(m_scroll, 1);

    // ===== Empty hint container =====
    m_emptyHint = new QWidget(m_container);
    auto* emptyLay = new QVBoxLayout(m_emptyHint);
    emptyLay->setContentsMargins(16, 48, 16, 48);
    emptyLay->setSpacing(8);
    emptyLay->setAlignment(Qt::AlignCenter);

    auto* emptyIcon = new QLabel(m_emptyHint);
    emptyIcon->setPixmap(cad::ui::IconHelper::iconByName(
        QStringLiteral("layers"), tk.text3).pixmap(36, 36));
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLay->addWidget(emptyIcon);

    auto* emptyTitle = new ElaText(QString::fromUtf8("暂无图层构件"), 15, m_emptyHint);
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyTitle->setStyleSheet(QStringLiteral(
        "font-size: 14px; font-weight: 600; color: %1; background: transparent;")
        .arg(tk.text2.name()));
    emptyLay->addWidget(emptyTitle);

    auto* emptySub = new ElaText(QString::fromUtf8("使用智能笔绘制线条后\n将在此按图层归类展示构件"), 12, m_emptyHint);
    emptySub->setAlignment(Qt::AlignCenter);
    emptySub->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: %1; background: transparent;")
        .arg(tk.text3.name()));
    emptyLay->addWidget(emptySub);

    m_emptyHint->setVisible(false);
    m_listLayout->insertWidget(0, m_emptyHint);
}

void LayerPanel::applyTheme()
{
    const auto& tk = cad::ui::Theme::tokens();
    setStyleSheet(QStringLiteral("background: %1;").arg(tk.surface.name()));
    if (m_header)
        m_header->setStyleSheet(QStringLiteral("background: %1;").arg(tk.surface.name()));
    if (m_countLabel) {
        m_countLabel->setStyleSheet(QStringLiteral(
            "font-size: 11px; color: %1; background: %2; border: 1px solid %3; border-radius: 3px; padding: 2px 8px; font-weight: 500;")
            .arg(tk.text2.name(), tk.surface2.name(), tk.border.name()));
    }
    if (m_addBtn) {
        m_addBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: %1; color: %2; border: 1px solid %3; border-radius: 3px;"
            "  padding: 0 10px; font-size: 11px; font-weight: 600;"
            "}"
            "QPushButton:hover { background: %4; border: 1px solid %5; color: %6; }"
            "QPushButton:pressed { background: %7; }")
            .arg(tk.surface.name(), tk.text1.name(), tk.border.name(),
                 tk.surface2.name(), tk.borderStrong.name(), tk.text1.name(),
                 tk.surface3.name()));
    }
    if (m_sep)
        m_sep->setStyleSheet(QStringLiteral("background: %1; border: none;").arg(tk.border.name()));
    if (m_scroll) {
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#cardListArea { background: %1; border: none; }"
            "QScrollBar:vertical { width: 6px; background: transparent; }"
            "QScrollBar::handle:vertical { background: %2; border-radius: 2px; }"
            "QScrollBar::handle:vertical:hover { background: %3; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(tk.canvasBg.name(), tk.borderStrong.name(), tk.text3.name()));
    }
    if (m_container)
        m_container->setStyleSheet(QStringLiteral("background: %1;").arg(tk.canvasBg.name()));

    // Rebuild cards to bake new theme tokens.
    refresh();
}

void LayerPanel::refresh()
{
    const int scrollPos = m_scroll->verticalScrollBar()->value();
    m_listLayout->setEnabled(false);

    // Remove all cards (keep trailing stretch + empty hint).
    while (m_listLayout->count() > 2) {
        QLayoutItem* it = m_listLayout->takeAt(1);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    m_cards.clear();

    const auto& layers = m_doc->layersView().all();
    const QUuid active = m_doc->layersView().activeLayer();

    // Prune stale collapse state (layer removal drops ids).
    for (auto it = m_collapsed.begin(); it != m_collapsed.end();)
        if (std::none_of(layers.begin(), layers.end(),
                         [&](const auto& l) { return l.id == *it; }))
            it = m_collapsed.erase(it);
        else ++it;

    m_countLabel->setText(QString::fromUtf8("%1 个图层").arg(layers.size()));

    // Detect whether any non-shadow blocks exist
    bool anyBlocks = false;
    for (const auto& b : m_doc->blocks())
        if (!b.segments.empty() && !b.isShadow) { anyBlocks = true; break; }
    m_emptyHint->setVisible(!anyBlocks);

    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        const auto& layer = layers[i];

        // Gather member segments
        std::vector<LayerCard::SegmentInfo> segs;
        for (const auto& b : m_doc->blocks()) {
            if (b.isShadow) continue;  // 影子不进图层面板 (R4)
            if (b.layer != layer.id || b.segments.empty())
                continue;
            const auto& s = b.segments.front();
            segs.push_back({b.id, s.name, s.serial, s.visible});
        }

        auto* card = new LayerCard(i, layer.id, layer.name, layer.visible,
                                   layer.id == active, static_cast<int>(segs.size()),
                                   m_doc->layersView().isAuxLayer(layer.id), m_container);
        m_cards.append(card);

        // Segment rows
        for (const auto& info : segs) {
            cad::param::Segment tmpSeg;
            tmpSeg.name = info.name;
            tmpSeg.serial = info.serial;
            tmpSeg.visible = info.visible;
            auto* row = new SegmentRow(info.blockId, tmpSeg, !layer.visible, card);
            card->addSegmentRow(row);

            // Eye toggle → undoable segment visibility
            connect(row->eye(), &EyeToggle::toggled, this, [this, id = info.blockId](bool on) {
                setSegmentVisible(id, on);
            });
            // Right-click → segment context menu
            connect(row, &QWidget::customContextMenuRequested, this,
                    [this, row, id = info.blockId](const QPoint& pos) {
                        showSegmentMenu(row->mapToGlobal(pos), id);
                    });
        }

        // Header interactions
        connect(card->eye(), &EyeToggle::toggled, this, [this, id = layer.id](bool on) {
            m_doc->setLayerVisible(id, on);
        });
        card->setCollapsed(m_collapsed.contains(layer.id));
        connect(card->collapseBtn(), &QToolButton::clicked, this, [this, card, id = layer.id] {
            const bool nowCollapsed = card->segList()->isVisible();
            card->setCollapsed(nowCollapsed);
            if (nowCollapsed)
                m_collapsed.insert(id);
            else
                m_collapsed.remove(id);
        });

        // Click header / name → activate layer; double-click → rename
        card->headerWidget()->installEventFilter(this);
        card->headerWidget()->setProperty("layerIndex", i);
        card->nameLabel()->installEventFilter(this);
        card->nameLabel()->setProperty("layerIndex", i);

        // Context menu
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QWidget::customContextMenuRequested, this,
                [this, card](const QPoint& pos) {
                    showLayerMenu(card->mapToGlobal(pos), card->layerIndex());
                });

        card->setOnMenuRequested([this, card](const QPoint& pos) {
            showLayerMenu(pos, card->layerIndex());
        });

        card->setOnRenameCommitted([this](int idx, const QString& newName) {
            m_doc->undoStack()->push(new cad::cmd::RenameLayerCommand(m_doc, idx, newName));
        });

        m_listLayout->insertWidget(m_listLayout->count() - 1, card);
    }

    m_listLayout->setEnabled(true);
    m_scroll->verticalScrollBar()->setValue(scrollPos);
}

void LayerPanel::syncFromDoc()
{
    if (m_cards.size() != static_cast<int>(m_doc->layersView().all().size())) {
        refresh();
        return;
    }

    bool anyBlocks = false;
    for (const auto& b : m_doc->blocks())
        if (!b.segments.empty() && !b.isShadow) { anyBlocks = true; break; }
    m_emptyHint->setVisible(!anyBlocks);
    m_countLabel->setText(QString::fromUtf8("%1 个图层").arg(m_doc->layersView().all().size()));

    const auto& layers = m_doc->layersView().all();
    for (int i = 0; i < m_cards.size(); ++i) {
        auto* card = static_cast<LayerCard*>(m_cards[i]);
        const auto& layer = layers[i];
        card->setLayerName(layer.name);
        card->setLayerVisible(layer.visible);

        QSet<QUuid> want;
        int count = 0;
        for (const auto& b : m_doc->blocks()) {
            if (b.isShadow || b.layer != layer.id || b.segments.empty()) continue;
            const auto& s = b.segments.front();
            want.insert(b.id);
            ++count;
            if (auto* row = card->findSegmentRow(b.id)) {
                row->setRowInfo(s.name, s.visible);
            } else {
                auto* newRow = new SegmentRow(b.id, s, !layer.visible, card);
                card->addSegmentRow(newRow);
                connect(newRow->eye(), &EyeToggle::toggled, this,
                        [this, id = b.id](bool on) { setSegmentVisible(id, on); });
                connect(newRow, &QWidget::customContextMenuRequested, this,
                        [this, newRow, id = b.id](const QPoint& pos) {
                            showSegmentMenu(newRow->mapToGlobal(pos), id);
                        });
            }
        }
        for (auto* row : card->segmentRows())
            if (!want.contains(row->blockId()))
                card->removeSegmentRow(row->blockId());
        card->setSegCount(count);
    }
}

bool LayerPanel::eventFilter(QObject* watched, QEvent* event)
{
    const QVariant vIdx = watched->property("layerIndex");
    if (vIdx.isValid()) {
        const int idx = vIdx.toInt();
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                if (idx >= 0 && idx < m_doc->layersView().layerCount()) {
                    m_doc->setActiveLayer(m_doc->layersView().all()[idx].id);
                }
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                startRename(idx);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LayerPanel::startRename(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_cards.size())
        return;
    if (auto* card = dynamic_cast<LayerCard*>(m_cards[layerIndex])) {
        card->startRename();
    }
}

// ---------------------------------------------------------------------------
// Context menus
// ---------------------------------------------------------------------------

void LayerPanel::showLayerMenu(const QPoint& globalPos, int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_doc->layersView().layerCount())
        return;

    const auto& layer = m_doc->layersView().all()[layerIndex];
    const bool isAux = m_doc->layersView().isAuxLayer(layer.id);
    const bool isActive = (layer.id == m_doc->layersView().activeLayer());

    ElaMenu menu(this);

    if (!isActive) {
        auto* actActive = menu.addAction(QString::fromUtf8("设为活动图层"));
        connect(actActive, &QAction::triggered, this, [this, id = layer.id] {
            m_doc->setActiveLayer(id);
        });
        menu.addSeparator();
    }

    auto* rename = menu.addAction(QString::fromUtf8("重命名"));
    connect(rename, &QAction::triggered, this, [this, layerIndex] {
        startRename(layerIndex);
    });

    menu.addSeparator();
    auto* del = menu.addAction(QString::fromUtf8("删除图层"));
    del->setEnabled(!isAux && m_doc->layersView().layerCount() > 2);
    connect(del, &QAction::triggered, this, [this, layerIndex] {
        deleteLayer(layerIndex);
    });

    menu.exec(globalPos);
}

void LayerPanel::showSegmentMenu(const QPoint& globalPos, const QUuid& blockId)
{
    ElaMenu menu(this);

    auto* moveMenu = menu.addMenu(QString::fromUtf8("移动到图层"));
    const auto* blk = m_doc->findBlock(blockId);
    const QUuid curLayer = blk ? blk->layer : QUuid();
    const bool curIsAux = !curLayer.isNull() && m_doc->layersView().isAuxLayer(curLayer);
    QList<QAction*> targets;
    for (int i = 0; i < m_doc->layersView().layerCount(); ++i) {
        if (m_doc->layersView().all()[i].id == curLayer)
            continue;
        auto* act = moveMenu->addAction(m_doc->layersView().all()[i].name);
        act->setData(i);
        targets.append(act);
    }
    if (targets.isEmpty())
        moveMenu->setEnabled(false);

    menu.addSeparator();
    auto* del = menu.addAction(QString::fromUtf8("删除线段"));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == del)
        deleteBlock(blockId);
    else if (targets.contains(chosen))
        moveBlockToLayer(blockId, chosen->data().toInt());
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void LayerPanel::onAddLayerClicked()
{
    int workingCount = 0;
    for (int i = 0; i < m_doc->layersView().layerCount(); ++i)
        if (!m_doc->layersView().isAuxLayer(m_doc->layersView().all()[i].id)) ++workingCount;
    const QString name = QString::fromUtf8("图层 %1").arg(workingCount + 1);
    m_doc->undoStack()->push(new cad::cmd::AddLayerCommand(m_doc, name));

    // New layer becomes active
    m_doc->setActiveLayer(m_doc->layersView().all().back().id);
}

void LayerPanel::deleteLayer(int index)
{
    if (m_doc->layersView().layerCount() <= 2 || m_doc->layersView().isAuxLayer(m_doc->layersView().all()[index].id))
        return;  // Aux layer and the last working layer are undeletable.

    // Confirm if the layer is non-empty.
    int count = 0;
    for (const auto& b : m_doc->blocks())
        if (b.layer == m_doc->layersView().all()[index].id && !b.isShadow)
            ++count;
    if (count > 0) {
        const bool ok = cad::ui::ElaMsgBox::question(this, QString::fromUtf8("删除图层"),
            QString::fromUtf8("该图层有 %1 条线段，将移至下方图层。确定删除？").arg(count));
        if (!ok)
            return;
    }

    m_doc->undoStack()->push(new cad::cmd::RemoveLayerCommand(m_doc, index));
}

void LayerPanel::deleteBlock(const QUuid& blockId)
{
    if (!cad::doc::confirmDeleteImpact(this, m_doc, {blockId}))
        return;

    m_doc->undoStack()->push(new cad::cmd::RemoveBlockCommand(m_doc, blockId));
}

void LayerPanel::setSegmentVisible(const QUuid& blockId, bool visible)
{
    auto* block = m_doc->findBlock(blockId);
    if (!block || block->segments.empty())
        return;
    const auto& seg = block->segments.front();
    if (seg.visible == visible)
        return;

    auto props = propsFrom(seg);
    props.visible = visible;
    m_doc->undoStack()->push(new cad::cmd::SetSegmentPropertyCommand(
        m_doc, blockId, seg.id, props));
}

void LayerPanel::moveBlockToLayer(const QUuid& blockId, int targetLayer)
{
    m_doc->undoStack()->push(new cad::cmd::MoveBlockToLayerCommand(m_doc, blockId, targetLayer));
}
