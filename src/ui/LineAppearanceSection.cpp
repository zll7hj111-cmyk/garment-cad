#include "ui/LineAppearanceSection.h"

#include <cmath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QButtonGroup>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>

#include "ElaText.h"
#include "ElaDoubleSpinBox.h"
#include "ElaColorDialog.h"

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "ui/Theme.h"
#include "ui/FormScaffold.h"
#include "ui/TooltipFormatter.h"

namespace cad::ui {

namespace {

constexpr double kWeightThin   = 0.8;
constexpr double kWeightMedium = 1.2;
constexpr double kWeightThick  = 2.0;

constexpr int kLabelW = 64;
constexpr int kFieldH = 30;

QIcon lineStyleIcon(Qt::PenStyle ps)
{
    QPixmap pm(48, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(cad::ui::Theme::tokens().text1);
    pen.setWidth(2);
    pen.setStyle(ps);
    p.setPen(pen);
    p.drawLine(3, 7, 45, 7);
    return QIcon(pm);
}

} // namespace

LineAppearanceSection::LineAppearanceSection(cad::param::ParamDocument* paramDoc,
                                             QWidget* parent)
    : QWidget(parent)
    , m_paramDoc(paramDoc)
{
    const QString chips = cad::ui::chipButtonStyle();
    const QString dimMono = cad::ui::Theme::dimValueStyle();

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    // ── 线型分段按钮 ──
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblStyle = new ElaText(QString::fromUtf8("线型"), 11, this);
        lblStyle->setFixedWidth(kLabelW);
        row->addWidget(lblStyle);
        m_styleGroup = new QButtonGroup(this);
        m_styleGroup->setExclusive(true);
        const Qt::PenStyle penStyles[3] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine};
        const char* styleTips[3] = {"实线", "虚线", "点线"};
        for (int i = 0; i < 3; ++i) {
            auto* b = new QPushButton(this);
            b->setCheckable(true);
            b->setIcon(lineStyleIcon(penStyles[i]));
            b->setIconSize(QSize(48, 14));
            b->setFixedSize(56, kFieldH);
            b->setStyleSheet(chips);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(cad::ui::TooltipFormatter::action(
                QString::fromUtf8(styleTips[i]),
                QString::fromUtf8("将线段渲染样式设置为%1").arg(QString::fromUtf8(styleTips[i]))));
            m_styleGroup->addButton(b, i);
            m_styleBtns[i] = b;
            row->addWidget(b);
        }
        connect(m_styleGroup, &QButtonGroup::idClicked, this, [this](int) { emit liveUpdated(); });
        row->addStretch();
        col->addLayout(row);
    }

    // ── 粗细分段 + 数值 spin ──
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblWeight = new ElaText(QString::fromUtf8("粗细"), 11, this);
        lblWeight->setFixedWidth(kLabelW);
        row->addWidget(lblWeight);
        m_weightGroup = new QButtonGroup(this);
        m_weightGroup->setExclusive(true);
        const char* weightTexts[3] = {"细", "中", "粗"};
        for (int i = 0; i < 3; ++i) {
            auto* b = new QPushButton(QString::fromUtf8(weightTexts[i]), this);
            b->setCheckable(true);
            b->setFixedHeight(kFieldH);
            b->setStyleSheet(chips);
            b->setCursor(Qt::PointingHandCursor);
            m_weightGroup->addButton(b, i);
            m_weightBtns[i] = b;
            row->addWidget(b);
        }
        m_spinWeight = new ElaDoubleSpinBox(this);
        m_spinWeight->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_spinWeight->setFixedWidth(110);
        m_spinWeight->setFixedHeight(kFieldH);
        m_spinWeight->setStyleSheet(QStringLiteral("font-size: 11px;"));
        m_spinWeight->setRange(0.5, 10.0);
        m_spinWeight->setSingleStep(0.2);
        m_spinWeight->setDecimals(1);
        m_spinWeight->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("线宽 (px)"),
            QStringLiteral("自定义线段渲染线宽（像素）")));
        row->addWidget(m_spinWeight);

        connect(m_spinWeight, &QDoubleSpinBox::valueChanged, this, &LineAppearanceSection::liveUpdated);
        connect(m_weightGroup, &QButtonGroup::idClicked, this, [this](int id) {
            const double preset[3] = {kWeightThin, kWeightMedium, kWeightThick};
            if (id >= 0 && id < 3 && m_spinWeight) {
                m_spinWeight->setValue(preset[id]);
            }
        });

        row->addStretch();
        col->addLayout(row);
    }

    // ── 颜色: 色板 + hex 读数 ──
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblColor = new ElaText(QString::fromUtf8("颜色"), 11, this);
        lblColor->setFixedWidth(kLabelW);
        row->addWidget(lblColor);
        m_btnColor = new QPushButton(this);
        m_btnColor->setFixedSize(44, kFieldH);
        m_btnColor->setCursor(Qt::PointingHandCursor);
        m_btnColor->setToolTip(cad::ui::TooltipFormatter::action(
            QStringLiteral("线段颜色"),
            QStringLiteral("点击打开调色板选择线段显示颜色")));
        connect(m_btnColor, &QPushButton::clicked, this, &LineAppearanceSection::onColorPick);
        row->addWidget(m_btnColor);
        m_lblColorHex = new ElaText(QString(), 11, this);
        m_lblColorHex->setStyleSheet(dimMono);
        row->addWidget(m_lblColorHex);
        row->addStretch();
        col->addLayout(row);
    }

    // ── 显示: 名称/长度/可见 toggle chips ──
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblShow = new ElaText(QString::fromUtf8("显示"), 11, this);
        lblShow->setFixedWidth(kLabelW);
        row->addWidget(lblShow);
        struct ChipDef { const char* text; QPushButton** slot; const char* tip; };
        const ChipDef defs[3] = {
            {"名称", &m_chkShowName,   "在画布上显示线段名称"},
            {"长度", &m_chkShowLength, "在画布上显示长度标注"},
            {"可见", &m_chkVisible,    "隐藏是纯视觉属性：不渲染，但仍可悬停/选择/捕捉"},
        };
        for (const auto& d : defs) {
            auto* b = new QPushButton(QString::fromUtf8(d.text), this);
            b->setCheckable(true);
            b->setFixedHeight(kFieldH);
            b->setStyleSheet(chips);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(cad::ui::TooltipFormatter::action(
                QString::fromUtf8(d.text),
                QString::fromUtf8(d.tip)));
            connect(b, &QPushButton::toggled, this, &LineAppearanceSection::liveUpdated);
            *d.slot = b;
            row->addWidget(b);
        }
        row->addStretch();
        col->addLayout(row);
    }
}

void LineAppearanceSection::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
}

void LineAppearanceSection::populateFromModel(const cad::param::Block& block,
                                              const cad::param::Segment& seg)
{
    Q_UNUSED(block);
    const QSignalBlocker bStyle(m_styleGroup);
    const QSignalBlocker bWeight(m_spinWeight);
    const QSignalBlocker bShowLen(m_chkShowLength);
    const QSignalBlocker bVis(m_chkVisible);
    const QSignalBlocker bShowName(m_chkShowName);

    // 线型分段按钮
    const int styleId = static_cast<int>(seg.lineStyle);
    if (m_styleGroup) {
        if (auto* btn = m_styleGroup->button(styleId))
            btn->setChecked(true);
    }

    // 粗细
    updateWeightControls();

    // 颜色
    m_currentColor = seg.color;
    if (m_btnColor) {
        m_btnColor->setStyleSheet(QStringLiteral(
            "background-color: %1; border:1px solid %2; border-radius:2px;")
            .arg(seg.color.name(), cad::ui::Theme::tokens().text3.name()));
    }
    if (m_lblColorHex)
        m_lblColorHex->setText(seg.color.name().toUpper());

    // 显示 chips
    if (m_chkShowName)   m_chkShowName->setChecked(seg.showName);
    if (m_chkShowLength) m_chkShowLength->setChecked(seg.showLength);
    if (m_chkVisible)    m_chkVisible->setChecked(seg.visible);
}

void LineAppearanceSection::applyToModel(cad::param::Block* block,
                                         cad::param::Segment* seg)
{
    Q_UNUSED(block);
    if (!seg) return;

    const int styleId = m_styleGroup ? m_styleGroup->checkedId() : -1;
    if (styleId >= 0)
        seg->lineStyle = static_cast<cad::param::LineStyle>(styleId);

    if (m_spinWeight)
        seg->weight = m_spinWeight->value();

    seg->color = m_currentColor;

    if (m_chkShowName)   seg->showName = m_chkShowName->isChecked();
    if (m_chkShowLength) seg->showLength = m_chkShowLength->isChecked();
    if (m_chkVisible)    seg->visible = m_chkVisible->isChecked();
}

void LineAppearanceSection::applyHoldOverride(bool forceName, bool forceLength)
{
    if (!m_paramDoc) return;
    const cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    if (m_chkShowName) {
        const QSignalBlocker b(m_chkShowName);
        m_chkShowName->setChecked(seg->showName || forceName);
    }
    if (m_chkShowLength) {
        const QSignalBlocker b(m_chkShowLength);
        m_chkShowLength->setChecked(seg->showLength || forceLength);
    }
}

void LineAppearanceSection::updateWeightControls()
{
    if (!m_paramDoc || !m_spinWeight || !m_weightGroup) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    const double w = seg->weight;

    const bool oldSpinState = m_spinWeight->blockSignals(true);
    m_spinWeight->setValue(w);
    m_spinWeight->blockSignals(oldSpinState);

    const double presets[3] = {kWeightThin, kWeightMedium, kWeightThick};
    int match = -1;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(w - presets[i]) < 0.05) { match = i; break; }
    }
    for (int i = 0; i < 3; ++i) {
        if (m_weightBtns[i]) m_weightBtns[i]->setChecked(i == match);
    }
}

void LineAppearanceSection::onColorPick()
{
    ElaColorDialog dlg(this);
    dlg.setCurrentColor(m_currentColor);
    QColor chosen;
    connect(&dlg, &ElaColorDialog::colorSelected, this,
            [&chosen](const QColor& c) { chosen = c; });
    dlg.exec();
    if (!chosen.isValid()) return;

    m_currentColor = chosen;
    if (m_btnColor) {
        m_btnColor->setStyleSheet(QStringLiteral(
            "background-color: %1; border:1px solid %2; border-radius:2px;")
            .arg(chosen.name(), cad::ui::Theme::tokens().text3.name()));
    }
    if (m_lblColorHex)
        m_lblColorHex->setText(chosen.name().toUpper());

    emit liveUpdated();
}

} // namespace cad::ui
