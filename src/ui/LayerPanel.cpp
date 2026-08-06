#include "LayerPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QUndoStack>
#include <QFrame>
#include <QEvent>

#include "IconHelper.h"
#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Segment.h"
#include "parametric/Serial.h"
#include "document/commands/LayerCommands.h"
#include "document/commands/BlockCommands.h"
#include "document/DeleteImpactConfirm.h"

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

// ---------------------------------------------------------------------------
// Eye toggle button (visibility switch with icon states)
// ---------------------------------------------------------------------------
class EyeToggle : public QToolButton
{
public:
    EyeToggle(QWidget* parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(22, 22);
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(
            "QToolButton { background: transparent; border: none; border-radius: 11px; }"
            "QToolButton:hover { background: #EBF5FB; }");
        setCheckable(true);
        connect(this, &QToolButton::toggled, this, &EyeToggle::syncIcon);
        syncIcon();
    }

private:
    void syncIcon()
    {
        const bool on = isChecked();
        setIcon(cad::ui::IconHelper::iconByName(
            on ? QStringLiteral("eye") : QStringLiteral("eye-closed"),
            on ? QColor(0x2E, 0x86, 0xC1) : QColor(0xB0, 0xBE, 0xC5)));
        setIconSize(QSize(14, 14));
        setToolTip(on ? QString::fromUtf8("\u70b9\u51fb\u9690\u85cf")      // 点击隐藏
                      : QString::fromUtf8("\u70b9\u51fb\u663e\u793a"));    // 点击显示
    }
};

// ---------------------------------------------------------------------------
// Segment row widget inside a layer card
// ---------------------------------------------------------------------------
class SegmentRow : public QWidget
{
public:
    SegmentRow(const QUuid& blockId, const cad::param::Segment& seg,
               bool layerHidden, QWidget* parent)
        : QWidget(parent)
        , m_blockId(blockId)
    {
        setAttribute(Qt::WA_StyledBackground, true);
        setObjectName(QStringLiteral("SegmentRow"));
        // Pure selector rules only: mixing bare declarations with a
        // "QWidget:hover" rule made Qt's stylesheet parser reject the whole
        // string ("Could not parse stylesheet"), which flooded the console and
        // burned CPU on every LayerPanel rebuild (i.e. every resolve).
        setStyleSheet(
            "QWidget#SegmentRow { background: transparent; border-radius: 4px; }"
            "QWidget#SegmentRow:hover { background: #F4F6F7; }");
        setContextMenuPolicy(Qt::CustomContextMenu);

        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(4, 1, 6, 1);
        lay->setSpacing(6);

        m_eye = new EyeToggle(this);
        m_eye->setChecked(seg.visible);
        lay->addWidget(m_eye);

        // Name (or muted dash when unnamed).
        auto* nameLbl = new QLabel(this);
        m_nameLbl = nameLbl;
        if (seg.name.isEmpty()) {
            nameLbl->setText(QStringLiteral("\u2014"));  // —
            nameLbl->setStyleSheet("font-size: 12px; color: #BDC3C7; background: transparent;");
        } else {
            nameLbl->setText(seg.name);
            nameLbl->setStyleSheet("font-size: 12px; color: #34495E; background: transparent;");
        }
        lay->addWidget(nameLbl, 1);

        // Serial tag badge (monospace, blue-gray chip).
        auto* tagLbl = new QLabel(cad::param::Serial::tag(seg.serial), this);
        tagLbl->setToolTip(seg.serial);
        tagLbl->setStyleSheet(
            "font-family: 'Consolas','Courier New',monospace;"
            "font-size: 10px; color: #5D6D7E; background: #ECEFF1;"
            "border-radius: 3px; padding: 1px 5px;");
        lay->addWidget(tagLbl);

        // Dim the row when the segment is individually hidden.
        if (!seg.visible)
            nameLbl->setStyleSheet("font-size: 12px; color: #B0BEC5; background: transparent;");
        (void)layerHidden;
    }

    [[nodiscard]] QUuid blockId() const { return m_blockId; }
    [[nodiscard]] EyeToggle* eye() const { return m_eye; }

    /// In-place refresh (增量同步): update name + visibility without
    /// rebuilding the row widget. blockSignals guards against a signal loop
    /// (setChecked → toggled → undo command → documentChanged → sync again).
    void setRowInfo(const QString& name, bool visible)
    {
        m_eye->blockSignals(true);
        m_eye->setChecked(visible);
        m_eye->blockSignals(false);
        m_nameLbl->setText(name.isEmpty() ? QStringLiteral("—") : name);
        m_nameLbl->setStyleSheet(QStringLiteral(
            "font-size: 12px; color: %1; background: transparent;")
            .arg(visible ? (name.isEmpty() ? QStringLiteral("#BDC3C7")
                                           : QStringLiteral("#34495E"))
                         : QStringLiteral("#B0BEC5")));
    }

private:
    QUuid m_blockId;
    EyeToggle* m_eye = nullptr;
    QLabel* m_nameLbl = nullptr;
};

// ---------------------------------------------------------------------------
// Layer card: accent bar + header + collapsible segment list
// ---------------------------------------------------------------------------
class LayerCard : public QFrame
{
public:
    struct SegmentInfo {
        QUuid blockId;
        QString name;
        QString serial;
        bool visible;
    };

    LayerCard(int index, const QString& name, bool visible, bool isActive,
              int segCount, bool isAux, QWidget* parent)
        : QFrame(parent)
        , m_index(index)
        , m_isActive(isActive)
    {
        setObjectName(QStringLiteral("LayerCard"));
        setStyleSheet(QStringLiteral(
            "QFrame#LayerCard {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 7px;"
            "}"
            "QFrame#LayerCard:hover { border: 1px solid %3; }")
            .arg(isActive ? QStringLiteral("#F6FBFF") : QStringLiteral("#FFFFFF"))
            .arg(isActive ? QStringLiteral("#AED6F1") : QStringLiteral("#E0E4E8"))
            .arg(isActive ? QStringLiteral("#5DADE2") : QStringLiteral("#B0C4DE")));

        auto* outer = new QHBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        // Left accent bar (auxiliary layer uses a distinct amber tone).
        auto* accent = new QWidget(this);
        accent->setFixedWidth(4);
        accent->setStyleSheet(QStringLiteral(
            "background: %1; border-top-left-radius: 7px; border-bottom-left-radius: 7px;")
            .arg(isAux ? QStringLiteral("#F39C12")
                       : (isActive ? QStringLiteral("#2E86C1")
                                   : (visible ? QStringLiteral("#B0BEC5") : QStringLiteral("#E0E0E0")))));
        outer->addWidget(accent);

        // Content column.
        auto* content = new QVBoxLayout();
        content->setContentsMargins(6, 5, 8, 5);
        content->setSpacing(2);
        outer->addLayout(content, 1);

        // --- Header row ---
        auto* header = new QHBoxLayout();
        header->setSpacing(4);

        m_collapseBtn = new QToolButton(this);
        m_collapseBtn->setIcon(cad::ui::IconHelper::iconByName(
            QStringLiteral("caret-down"), QColor(0x7F, 0x8C, 0x8D)));
        m_collapseBtn->setIconSize(QSize(10, 10));
        m_collapseBtn->setFixedSize(18, 18);
        m_collapseBtn->setCursor(Qt::PointingHandCursor);
        m_collapseBtn->setStyleSheet(
            "QToolButton { background: transparent; border: none; border-radius: 9px; }"
            "QToolButton:hover { background: #EAECEE; }");
        header->addWidget(m_collapseBtn);

        m_eye = new EyeToggle(this);
        m_eye->setChecked(visible);
        header->addWidget(m_eye);

        m_nameLabel = new QLabel(name, this);
        m_nameLabel->setCursor(Qt::PointingHandCursor);
        m_nameLabel->setToolTip(QString::fromUtf8(
            "\u5355\u51fb\u8bbe\u4e3a\u6d3b\u52a8\u5c42 \u00b7 \u53cc\u51fb\u91cd\u547d\u540d"));
        // 单击设为活动层 · 双击重命名
        m_nameLabel->setStyleSheet(QStringLiteral(
            "font-size: 12px; font-weight: %1; color: %2; background: transparent; padding: 2px 0;")
            .arg(isActive ? QStringLiteral("bold") : QStringLiteral("normal"))
            .arg(isActive ? QStringLiteral("#1B4F72") : QStringLiteral("#34495E")));
        header->addWidget(m_nameLabel, 1);

        // Auxiliary-layer badge (always shown, amber chip).
        if (isAux) {
            auto* auxBadge = new QLabel(QString::fromUtf8("\u8f85"), this);  // 辅
            auxBadge->setToolTip(QString::fromUtf8(
                "\u8f85\u52a9\u8ba1\u7b97\u5c42\uff1a\u753b\u6784\u9020\u51e0\u4f55\u6c42\u503c\uff0c"
                "\u53d1\u5e03\u6d4b\u91cf\u53c2\u6570\u4f9b\u5de5\u4f5c\u5c42\u4f7f\u7528"));
            // 辅助计算层：画构造几何求值，发布测量参数供工作层使用
            auxBadge->setStyleSheet(
                "font-size: 10px; font-weight: bold; color: #935E09;"
                "background: #FDEBD0; border-radius: 4px; padding: 1px 6px;");
            header->addWidget(auxBadge);
        }

        // Active badge.
        if (isActive) {
            auto* badge = new QLabel(QString::fromUtf8("\u6d3b\u52a8"), this);  // 活动
            badge->setStyleSheet(
                "font-size: 10px; font-weight: bold; color: #2E86C1;"
                "background: #D6EAF8; border-radius: 4px; padding: 1px 6px;");
            header->addWidget(badge);
        }

        // Count pill.
        auto* countPill = new QLabel(QStringLiteral("%1").arg(segCount), this);
        m_countPill = countPill;
        countPill->setStyleSheet(
            "font-size: 10px; color: #85929E; background: #EAECEE;"
            "border-radius: 8px; padding: 1px 7px;");
        countPill->setToolTip(QString::fromUtf8("%1 \u6761\u7ebf\u6bb5"));  // %1 条线段
        header->addWidget(countPill);

        content->addLayout(header);

        // --- Segment list container ---
        m_segList = new QWidget(this);
        m_segListLayout = new QVBoxLayout(m_segList);
        m_segListLayout->setContentsMargins(24, 2, 0, 2);
        m_segListLayout->setSpacing(1);
        content->addWidget(m_segList);

        // Dim everything when the layer is hidden.
        if (!visible)
            setOpacityDimmed(true);
    }

    void addSegmentRow(SegmentRow* row) { m_segListLayout->addWidget(row); }

    /// In-place refresh (增量同步): name / visibility / count / segment rows
    /// update without rebuilding the card.
    void setLayerName(const QString& name) { m_nameLabel->setText(name); }
    void setLayerVisible(bool visible)
    {
        // blockSignals: setChecked would fire toggled → undo command →
        // layersChanged → full refresh (signal loop from the in-place sync).
        m_eye->blockSignals(true);
        m_eye->setChecked(visible);
        m_eye->blockSignals(false);
        setOpacityDimmed(!visible);
    }
    void setSegCount(int n) { m_countPill->setText(QString::number(n)); }

    [[nodiscard]] SegmentRow* findSegmentRow(const QUuid& blockId) const
    {
        for (int i = 0; i < m_segListLayout->count(); ++i) {
            // The list holds ONLY SegmentRow widgets (no Q_OBJECT — plain
            // static_cast is safe).
            if (auto* row = static_cast<SegmentRow*>(
                    m_segListLayout->itemAt(i)->widget()))
                if (row->blockId() == blockId)
                    return row;
        }
        return nullptr;
    }
    [[nodiscard]] QList<SegmentRow*> segmentRows() const
    {
        QList<SegmentRow*> rows;
        for (int i = 0; i < m_segListLayout->count(); ++i) {
            if (auto* row = static_cast<SegmentRow*>(
                    m_segListLayout->itemAt(i)->widget()))
                rows.append(row);
        }
        return rows;
    }
    void removeSegmentRow(const QUuid& blockId)
    {
        for (int i = 0; i < m_segListLayout->count(); ++i) {
            auto* it = m_segListLayout->itemAt(i);
            if (auto* row = static_cast<SegmentRow*>(it->widget())) {
                if (row->blockId() == blockId) {
                    m_segListLayout->removeItem(it);
                    row->deleteLater();
                    delete it;
                    return;
                }
            }
        }
    }

    [[nodiscard]] int layerIndex() const { return m_index; }
    [[nodiscard]] EyeToggle* eye() const { return m_eye; }
    [[nodiscard]] QLabel* nameLabel() const { return m_nameLabel; }
    [[nodiscard]] QToolButton* collapseBtn() const { return m_collapseBtn; }
    [[nodiscard]] QWidget* segList() const { return m_segList; }

    void setCollapsed(bool collapsed)
    {
        m_segList->setVisible(!collapsed);
        m_collapseBtn->setIcon(cad::ui::IconHelper::iconByName(
            collapsed ? QStringLiteral("caret-right") : QStringLiteral("caret-down"),
            QColor(0x7F, 0x8C, 0x8D)));
    }

    void setOpacityDimmed(bool dimmed)
    {
        // Simulate opacity by overriding the name label color (cheap, no
        // QGraphicsOpacityEffect which breaks child hover styles).
        m_nameLabel->setStyleSheet(QStringLiteral(
            "font-size: 12px; font-weight: %1; color: %2; background: transparent; padding: 2px 0;")
            .arg(m_isActive ? QStringLiteral("bold") : QStringLiteral("normal"))
            .arg(dimmed ? QStringLiteral("#B0BEC5")
                        : (m_isActive ? QStringLiteral("#1B4F72") : QStringLiteral("#34495E"))));
    }

private:
    int m_index;
    bool m_isActive;
    QToolButton* m_collapseBtn = nullptr;
    EyeToggle* m_eye = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_countPill = nullptr;
    QWidget* m_segList = nullptr;
    QVBoxLayout* m_segListLayout = nullptr;
};

} // namespace

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
            this, [this](int) { refresh(); }, Qt::QueuedConnection);
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
    setStyleSheet("background: #FFFFFF;");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ===== Header: title + count pill + add button =====
    auto* header = new QWidget(this);
    header->setStyleSheet("background: #FFFFFF;");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 6, 10, 4);
    headerLayout->setSpacing(8);

    auto* title = new QLabel(QString::fromUtf8("\u56fe\u5c42"), header);  // 图层
    title->setStyleSheet("font-size: 13px; font-weight: bold; color: #2C3E50;");
    headerLayout->addWidget(title);

    m_countLabel = new QLabel(header);
    m_countLabel->setStyleSheet(
        "font-size: 11px; color: #85929E; background: #EAECEE;"
        "border-radius: 8px; padding: 1px 8px;");
    headerLayout->addWidget(m_countLabel);

    headerLayout->addStretch();

    auto* addBtn = new QToolButton(header);
    addBtn->setIcon(cad::ui::IconHelper::iconByName(
        QStringLiteral("plus"), QColor(0x2E, 0x86, 0xC1)));
    addBtn->setIconSize(QSize(13, 13));
    addBtn->setToolTip(QString::fromUtf8("\u65b0\u5efa\u56fe\u5c42"));  // 新建图层
    addBtn->setFixedSize(26, 26);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setStyleSheet(
        "QToolButton { background: transparent; border: 1px solid #AED6F1;"
        "  border-radius: 5px; }"
        "QToolButton:hover { background: #EBF5FB; border: 1px solid #2E86C1; }");
    connect(addBtn, &QToolButton::clicked, this, &LayerPanel::onAddLayerClicked);
    headerLayout->addWidget(addBtn);

    layout->addWidget(header);

    // ===== Separator =====
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    sep->setStyleSheet("background: #E5E8E8; border: none;");
    layout->addWidget(sep);

    // ===== Scrollable card list =====
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(
        "QScrollArea { background: #FAFBFC; border: none; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #D5DBDB; border-radius: 3px; }"
        "QScrollBar::handle:vertical:hover { background: #AEB6BF; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    m_container = new QWidget(m_scroll);
    m_container->setStyleSheet("background: #FAFBFC;");
    m_listLayout = new QVBoxLayout(m_container);
    m_listLayout->setContentsMargins(8, 8, 8, 8);
    m_listLayout->setSpacing(8);
    m_listLayout->addStretch();

    m_scroll->setWidget(m_container);
    layout->addWidget(m_scroll, 1);

    // ===== Empty hint (overlay label) =====
    m_emptyHint = new QLabel(QString::fromUtf8(
        "\u4f7f\u7528\u667a\u80fd\u7b14\u7ed8\u5236\u7ebf\u6761\u540e\n\u5c06\u5728\u6b64\u663e\u793a\u56fe\u5c42\u5185\u5bb9"), m_container);
    // 使用智能笔绘制线条后\n将在此显示图层内容
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet("color: #B0BEC5; font-size: 12px; background: transparent;");
    m_emptyHint->setVisible(false);
    m_listLayout->insertWidget(0, m_emptyHint);
}

void LayerPanel::refresh()
{
    // Preserve scroll position across the rebuild.
    const int scrollPos = m_scroll->verticalScrollBar()->value();

    // Batch rebuild: freeze the layout so per-card insert does not relayout
    // the whole list (O(N²) with many cards); re-enable activates ONCE.
    m_listLayout->setEnabled(false);

    // Remove all cards (keep trailing stretch + empty hint).
    while (m_listLayout->count() > 2) {
        QLayoutItem* it = m_listLayout->takeAt(1);  // index 0 = empty hint, last = stretch
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    m_cards.clear();

    const auto& layers = m_doc->layers();
    const int active = m_doc->activeLayer();

    // Prune stale collapse state (layer removal shifts indices).
    for (auto it = m_collapsed.begin(); it != m_collapsed.end();)
        if (*it >= static_cast<int>(layers.size())) it = m_collapsed.erase(it);
        else ++it;

    m_countLabel->setText(QStringLiteral("%1").arg(layers.size()));

    // Detect whether any blocks exist at all (for the empty hint).
    bool anyBlocks = false;
    for (const auto& b : m_doc->blocks())
        if (!b.segments.empty()) { anyBlocks = true; break; }
    m_emptyHint->setVisible(!anyBlocks);

    for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
        const auto& layer = layers[i];

        // Gather member segments.
        std::vector<LayerCard::SegmentInfo> segs;
        for (const auto& b : m_doc->blocks()) {
            if (b.layer != i || b.segments.empty())
                continue;
            const auto& s = b.segments.front();
            segs.push_back({b.id, s.name, s.serial, s.visible});
        }

        auto* card = new LayerCard(i, layer.name, layer.visible,
                                   i == active, static_cast<int>(segs.size()),
                                   m_doc->isAuxLayer(i), m_container);
        m_cards.append(card);

        // Segment rows.
        for (const auto& info : segs) {
            cad::param::Segment tmpSeg;
            tmpSeg.name = info.name;
            tmpSeg.serial = info.serial;
            tmpSeg.visible = info.visible;
            auto* row = new SegmentRow(info.blockId, tmpSeg, !layer.visible, card);
            card->addSegmentRow(row);

            // Eye toggle → undoable segment visibility.
            connect(row->eye(), &EyeToggle::toggled, this, [this, id = info.blockId](bool on) {
                setSegmentVisible(id, on);
            });
            // Right-click → segment context menu.
            connect(row, &QWidget::customContextMenuRequested, this,
                    [this, row, id = info.blockId](const QPoint& pos) {
                        showSegmentMenu(row->mapToGlobal(pos), id);
                    });
        }

        // Header interactions.
        connect(card->eye(), &EyeToggle::toggled, this, [this, i](bool on) {
            m_doc->setLayerVisible(i, on);
        });
        card->setCollapsed(m_collapsed.contains(i));
        connect(card->collapseBtn(), &QToolButton::clicked, this, [this, card, i] {
            const bool nowCollapsed = card->segList()->isVisible();
            card->setCollapsed(nowCollapsed);
            if (nowCollapsed)
                m_collapsed.insert(i);
            else
                m_collapsed.remove(i);
        });

        // Click name → activate layer; double-click → rename.
        card->nameLabel()->installEventFilter(this);
        card->nameLabel()->setProperty("layerIndex", i);

        // Right-click card → layer menu.
        card->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(card, &QWidget::customContextMenuRequested, this,
                [this, card](const QPoint& pos) {
                    showLayerMenu(card->mapToGlobal(pos), card->layerIndex());
                });

        m_listLayout->insertWidget(m_listLayout->count() - 1, card);
    }

    m_listLayout->setEnabled(true);  // single layout activation
    m_scroll->verticalScrollBar()->setValue(scrollPos);
}

void LayerPanel::syncFromDoc()
{
    // Layer count changed → full rebuild (structure signal normally handles
    // this; the check guards against ordering between queued signals).
    if (m_cards.size() != static_cast<int>(m_doc->layers().size())) {
        refresh();
        return;
    }

    bool anyBlocks = false;
    for (const auto& b : m_doc->blocks())
        if (!b.segments.empty()) { anyBlocks = true; break; }
    m_emptyHint->setVisible(!anyBlocks);
    m_countLabel->setText(QStringLiteral("%1").arg(m_doc->layers().size()));

    // In-place per-card update: name / visibility / segment rows. This is
    // the hot path (every resolveAll emits documentChanged) — widgets are
    // reused, only texts change; rebuilding widgets per edit was the lag.
    const auto& layers = m_doc->layers();
    for (int i = 0; i < m_cards.size(); ++i) {
        auto* card = static_cast<LayerCard*>(m_cards[i]);
        const auto& layer = layers[i];
        card->setLayerName(layer.name);
        card->setLayerVisible(layer.visible);

        // Desired segment rows for this layer, updated in place.
        QSet<QUuid> want;
        int count = 0;
        for (const auto& b : m_doc->blocks()) {
            if (b.layer != i || b.segments.empty()) continue;
            const auto& s = b.segments.front();
            want.insert(b.id);
            ++count;
            if (auto* row = card->findSegmentRow(b.id)) {
                row->setRowInfo(s.name, s.visible);
            } else {
                auto* newRow = new SegmentRow(b.id, s, !layer.visible, card);
                card->addSegmentRow(newRow);
                // Same interactions as refresh(): eye toggle + context menu.
                connect(newRow->eye(), &EyeToggle::toggled, this,
                        [this, id = b.id](bool on) { setSegmentVisible(id, on); });
                connect(newRow, &QWidget::customContextMenuRequested, this,
                        [this, newRow, id = b.id](const QPoint& pos) {
                            showSegmentMenu(newRow->mapToGlobal(pos), id);
                        });
            }
        }
        // Drop rows whose block moved away / was deleted.
        for (auto* row : card->segmentRows())
            if (!want.contains(row->blockId()))
                card->removeSegmentRow(row->blockId());
        card->setSegCount(count);
    }
}

bool LayerPanel::eventFilter(QObject* watched, QEvent* event)
{
    // Handle click / double-click on layer name labels.
    if (auto* lbl = qobject_cast<QLabel*>(watched)) {
        if (event->type() == QEvent::MouseButtonPress) {
            const int idx = lbl->property("layerIndex").toInt();
            m_doc->setActiveLayer(idx);
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            const int idx = lbl->property("layerIndex").toInt();
            startRename(idx, lbl);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LayerPanel::startRename(int layerIndex, QLabel* nameLabel)
{
    if (layerIndex < 0 || layerIndex >= m_doc->layerCount())
        return;

    // Overlay a QLineEdit exactly on top of the name label (the label lives
    // inside a nested header layout, so geometry overlay is the robust path).
    auto* host = nameLabel->parentWidget();
    if (!host) return;

    const QString oldName = m_doc->layers()[layerIndex].name;

    auto* edit = new QLineEdit(oldName, host);
    edit->setStyleSheet(
        "QLineEdit { font-size: 12px; font-weight: bold; color: #1B4F72;"
        "  border: 1px solid #2E86C1; border-radius: 3px; padding: 1px 4px;"
        "  background: #FFFFFF; }");
    const QRect geo = nameLabel->geometry();
    edit->setGeometry(geo.x() - 2, geo.y() - 2,
                      qMax(geo.width() + 30, 120), geo.height() + 4);
    nameLabel->setVisible(false);
    edit->show();
    edit->setFocus();
    edit->selectAll();

    connect(edit, &QLineEdit::editingFinished, this,
            [this, layerIndex, edit, nameLabel, oldName]() {
        const QString newName = edit->text().trimmed();
        edit->deleteLater();
        nameLabel->setVisible(true);
        if (!newName.isEmpty() && newName != oldName) {
            if (m_undoStack)
                m_undoStack->push(new cad::cmd::RenameLayerCommand(m_doc, layerIndex, newName));
            else
                m_doc->renameLayer(layerIndex, newName);
        }
    });
}

// ---------------------------------------------------------------------------
// Context menus
// ---------------------------------------------------------------------------

void LayerPanel::showLayerMenu(const QPoint& globalPos, int layerIndex)
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { font-size: 12px; border: 1px solid #E0E4E8; border-radius: 6px;"
        "  padding: 4px; background: #FFFFFF; }"
        "QMenu::item { padding: 5px 20px; border-radius: 4px; }"
        "QMenu::item:selected { background: #EBF5FB; color: #1B4F72; }"
        "QMenu::item:disabled { color: #B0BEC5; }");

    auto* rename = menu.addAction(QString::fromUtf8("\u91cd\u547d\u540d"));  // 重命名
    menu.addSeparator();
    auto* del = menu.addAction(QString::fromUtf8("\u5220\u9664\u56fe\u5c42"));  // 删除图层
    // The auxiliary calculation layer cannot be deleted.
    del->setEnabled(!m_doc->isAuxLayer(layerIndex) && m_doc->layerCount() > 2);

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == del) {
        deleteLayer(layerIndex);
    } else if (chosen == rename) {
        // Find the card's name label and start editing.
        for (int i = 0; i < m_listLayout->count(); ++i) {
            auto* card = dynamic_cast<LayerCard*>(m_listLayout->itemAt(i)->widget());
            if (card && card->layerIndex() == layerIndex) {
                startRename(layerIndex, card->nameLabel());
                break;
            }
        }
    }
}

void LayerPanel::showSegmentMenu(const QPoint& globalPos, const QUuid& blockId)
{
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { font-size: 12px; border: 1px solid #E0E4E8; border-radius: 6px;"
        "  padding: 4px; background: #FFFFFF; }"
        "QMenu::item { padding: 5px 20px; border-radius: 4px; }"
        "QMenu::item:selected { background: #EBF5FB; color: #1B4F72; }"
        "QMenu::item:disabled { color: #B0BEC5; }"
        "QMenu::separator { height: 1px; background: #E5E8E8; margin: 3px 8px; }");

    // Move-to-layer submenu. Blocks never cross the aux/working boundary:
    // the auxiliary layer is sealed (its content is created and stays there).
    auto* moveMenu = menu.addMenu(QString::fromUtf8("\u79fb\u52a8\u5230\u56fe\u5c42"));  // 移动到图层
    moveMenu->setStyleSheet(menu.styleSheet());
    const int curLayer = m_doc->findBlock(blockId)
                             ? m_doc->findBlock(blockId)->layer : -1;
    const bool curIsAux = m_doc->isAuxLayer(curLayer);
    QList<QAction*> targets;
    for (int i = 0; i < m_doc->layerCount(); ++i) {
        if (i == curLayer)
            continue;
        if (m_doc->isAuxLayer(i) != curIsAux)
            continue;  // sealed boundary
        auto* act = moveMenu->addAction(m_doc->layers()[i].name);
        act->setData(i);
        targets.append(act);
    }
    if (targets.isEmpty())
        moveMenu->setEnabled(false);

    menu.addSeparator();
    auto* del = menu.addAction(QString::fromUtf8("\u5220\u9664\u7ebf\u6bb5"));  // 删除线段

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
    // Number working layers only (the sealed aux layer is not counted).
    int workingCount = 0;
    for (int i = 0; i < m_doc->layerCount(); ++i)
        if (!m_doc->isAuxLayer(i)) ++workingCount;
    const QString name = QString::fromUtf8("\u56fe\u5c42 %1").arg(workingCount + 1);
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::AddLayerCommand(m_doc, name));
    else
        m_doc->addLayer(name);

    // New layer becomes active (so the user can immediately draw on it).
    m_doc->setActiveLayer(m_doc->layerCount() - 1);
}

void LayerPanel::deleteLayer(int index)
{
    if (m_doc->layerCount() <= 2 || m_doc->isAuxLayer(index))
        return;  // Aux layer and the last working layer are undeletable.

    // Confirm if the layer is non-empty.
    int count = 0;
    for (const auto& b : m_doc->blocks())
        if (b.layer == index)
            ++count;
    if (count > 0) {
        const auto ans = QMessageBox::question(this, QString::fromUtf8("\u5220\u9664\u56fe\u5c42"),
            QString::fromUtf8("\u8be5\u56fe\u5c42\u6709 %1 \u6761\u7ebf\u6bb5\uff0c\u5c06\u79fb\u81f3\u4e0b\u65b9\u56fe\u5c42\u3002\u786e\u5b9a\u5220\u9664\uff1f").arg(count));
        if (ans != QMessageBox::Yes)
            return;
    }

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveLayerCommand(m_doc, index));
    else
        m_doc->removeLayer(index);
}

void LayerPanel::deleteBlock(const QUuid& blockId)
{
    // Show the delete-impact report (连接的桥接线/交点/变量/公式 consequences)
    // before committing — the cascade itself is unchanged.
    if (!cad::doc::confirmDeleteImpact(this, m_doc, {blockId}))
        return;

    if (m_undoStack)
        m_undoStack->push(new cad::cmd::RemoveBlockCommand(m_doc, blockId));
    else
        m_doc->removeBlock(blockId);
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
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::SetSegmentPropertyCommand(
            m_doc, blockId, seg.id, props));
    else {
        block->segments.front().visible = visible;
        m_doc->resolveAll();
    }
}

void LayerPanel::moveBlockToLayer(const QUuid& blockId, int targetLayer)
{
    if (m_undoStack)
        m_undoStack->push(new cad::cmd::MoveBlockToLayerCommand(m_doc, blockId, targetLayer));
    else {
        if (auto* b = m_doc->findBlock(blockId)) {
            b->layer = targetLayer;
            emit m_doc->layersChanged();
        }
    }
}
