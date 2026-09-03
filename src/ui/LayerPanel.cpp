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

// ---------------------------------------------------------------------------
// Eye toggle button (visibility switch with theme-aware icons)
// ---------------------------------------------------------------------------
class EyeToggle : public QToolButton
{
public:
    explicit EyeToggle(QWidget* parent = nullptr, int size = 20)
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

    void applyTheme()
    {
        setStyleSheet(QStringLiteral(
            "QToolButton { background: transparent; border: none; border-radius: 2px; padding: 0; }"
            "QToolButton:hover { background: %1; }")
            .arg(cad::ui::Theme::tokens().surface2.name()));
        syncIcon();
    }

private:
    void syncIcon()
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

    int m_size = 20;
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

    [[nodiscard]] QUuid blockId() const { return m_blockId; }
    [[nodiscard]] EyeToggle* eye() const { return m_eye; }
    [[nodiscard]] QToolButton* moreBtn() const { return m_moreBtn; }

    void setRowInfo(const QString& name, bool visible)
    {
        m_eye->blockSignals(true);
        m_eye->setChecked(visible);
        m_eye->blockSignals(false);
        updateNameVisual(name, visible);
    }

    void updateNameVisual(const QString& name, bool visible)
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

protected:
    void enterEvent(QEnterEvent*) override { m_moreBtn->setVisible(true); }
    void leaveEvent(QEvent*) override { m_moreBtn->setVisible(false); }

private:
    QUuid m_blockId;
    EyeToggle* m_eye = nullptr;
    ElaText* m_nameLbl = nullptr;
    ElaText* m_tagLbl = nullptr;
    QToolButton* m_moreBtn = nullptr;
};

// ---------------------------------------------------------------------------
// Layer card: technical border + left accent indicator + header + collapsible segment list
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

    LayerCard(int index, const QUuid& layerId, const QString& name, bool visible,
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

    void setOnRenameCommitted(std::function<void(int, const QString&)> cb) { m_onRenameCommitted = std::move(cb); }
    void setOnMenuRequested(std::function<void(const QPoint&)> cb) { m_onMenuRequested = std::move(cb); }

    void startRename()
    {
        m_nameLabel->setVisible(false);
        m_nameEdit->setText(m_nameLabel->text());
        m_nameEdit->setVisible(true);
        m_nameEdit->setFocus();
        m_nameEdit->selectAll();
    }

    void addSegmentRow(SegmentRow* row)
    {
        m_emptyRow->setVisible(false);
        m_segListLayout->addWidget(row);
    }

    void setLayerName(const QString& name)
    {
        m_nameLabel->setText(name);
    }

    void setLayerVisible(bool visible)
    {
        m_visible = visible;
        m_eye->blockSignals(true);
        m_eye->setChecked(visible);
        m_eye->blockSignals(false);
        updateNameStyle();
    }

    void setSegCount(int n)
    {
        m_countPill->setText(QString::number(n));
        m_countPill->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("线段统计"),
            QStringLiteral("当前图层包含 %1 条线段").arg(n),
            false));
        updateEmptyRowVisibility();
    }

    [[nodiscard]] SegmentRow* findSegmentRow(const QUuid& blockId) const
    {
        for (int i = 0; i < m_segListLayout->count(); ++i) {
            if (auto* row = dynamic_cast<SegmentRow*>(m_segListLayout->itemAt(i)->widget()))
                if (row->blockId() == blockId)
                    return row;
        }
        return nullptr;
    }

    [[nodiscard]] QList<SegmentRow*> segmentRows() const
    {
        QList<SegmentRow*> rows;
        for (int i = 0; i < m_segListLayout->count(); ++i) {
            if (auto* row = dynamic_cast<SegmentRow*>(m_segListLayout->itemAt(i)->widget()))
                rows.append(row);
        }
        return rows;
    }

    void removeSegmentRow(const QUuid& blockId)
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

    [[nodiscard]] int layerIndex() const { return m_index; }
    [[nodiscard]] QUuid layerId() const { return m_layerId; }
    [[nodiscard]] EyeToggle* eye() const { return m_eye; }
    [[nodiscard]] ElaText* nameLabel() const { return m_nameLabel; }
    [[nodiscard]] QWidget* headerWidget() const { return m_headerWidget; }
    [[nodiscard]] QToolButton* collapseBtn() const { return m_collapseBtn; }
    [[nodiscard]] QToolButton* menuBtn() const { return m_menuBtn; }
    [[nodiscard]] QWidget* segList() const { return m_segList; }

    void setCollapsed(bool collapsed)
    {
        m_segList->setVisible(!collapsed);
        m_innerSep->setVisible(!collapsed);
        const auto& tk = cad::ui::Theme::tokens();
        m_collapseBtn->setIcon(cad::ui::IconHelper::iconByName(
            collapsed ? QStringLiteral("caret-right") : QStringLiteral("caret-down"),
            tk.text3));
    }

    void applyTheme()
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

protected:
    void paintEvent(QPaintEvent* event) override
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

    void enterEvent(QEnterEvent*) override
    {
        m_menuBtn->setVisible(true);
    }

    void leaveEvent(QEvent*) override
    {
        m_menuBtn->setVisible(false);
    }

private:
    void applyCardStyle()
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

    void updateNameStyle()
    {
        const auto& tk = cad::ui::Theme::tokens();
        m_nameLabel->setStyleSheet(QStringLiteral(
            "font-size: 12px; font-weight: %1; color: %2; background: transparent; padding: 2px 0;")
            .arg(m_isActive ? QStringLiteral("bold") : QStringLiteral("normal"),
                 m_visible ? tk.text1.name() : tk.text3.name()));
    }

    void updateEmptyRowVisibility()
    {
        int segRowCount = 0;
        for (int i = 0; i < m_segListLayout->count(); ++i) {
            if (dynamic_cast<SegmentRow*>(m_segListLayout->itemAt(i)->widget()))
                ++segRowCount;
        }
        m_emptyRow->setVisible(segRowCount == 0);
    }

    int m_index;
    QUuid m_layerId;
    bool m_isActive;
    bool m_isAux;
    bool m_visible;
    QToolButton* m_collapseBtn = nullptr;
    EyeToggle* m_eye = nullptr;
    ElaText* m_nameLabel = nullptr;
    ElaLineEdit* m_nameEdit = nullptr;
    ElaText* m_auxBadge = nullptr;
    ElaText* m_activeBadge = nullptr;
    ElaText* m_countPill = nullptr;
    QToolButton* m_menuBtn = nullptr;
    QWidget* m_headerWidget = nullptr;
    QFrame* m_innerSep = nullptr;
    QWidget* m_segList = nullptr;
    QVBoxLayout* m_segListLayout = nullptr;
    ElaText* m_emptyRow = nullptr;
    std::function<void(int, const QString&)> m_onRenameCommitted;
    std::function<void(const QPoint&)> m_onMenuRequested;
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
