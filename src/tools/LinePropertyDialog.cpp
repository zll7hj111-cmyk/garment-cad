#include "LinePropertyDialog.h"

#include <algorithm>
#include <cmath>

#include "ElaTabWidget.h"
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include "ElaLineEdit.h"
#include <QDebug>
#include "ElaComboBox.h"
#include "ElaCheckBox.h"
#include "ElaText.h"
#include "ElaDoubleSpinBox.h"
#include <QGroupBox>
#include "ElaScrollPageArea.h"
#include "ElaScrollArea.h"
#include <QScreen>
#include "ElaPushButton.h"
#include <QPushButton>
#include <QFrame>
#include "ui/ElaDialogButtons.h"
#include <QMouseEvent>
#include "ElaColorDialog.h"
#include <QListWidget>
#include <QApplication>
#include <QClipboard>
#include <QSignalBlocker>

#include "parametric/ParamDocument.h"
#include "parametric/Block.h"
#include "parametric/Serial.h"
#include "parametric/ConditionEngine.h"
#include "parametric/AttachmentGraph.h"
#include "parametric/LinkedVariable.h"
#include "canvas/CanvasScene.h"
#include "canvas/BlockItem.h"
#include "geometry/Units.h"
#include "geometry/CurveMath.h"
#include "geometry/Angle.h"
#include "parametric/FollowerAngle.h"
#include "document/commands/VariableCommands.h"
#include "PointRefEdit.h"
#include "SegmentAnchorTab.h"
#include "SegmentAimCard.h"
#include "SegmentConnectionCard.h"
#include "SegmentAuxTab.h"

namespace cad::tools {

namespace {

/// Weight presets: 细 / 中 / 粗 / 自定义
constexpr double kWeightThin   = 0.8;
constexpr double kWeightMedium = 1.2;
constexpr double kWeightThick  = 2.0;

/// Format an angle in degrees for display: integers render without a trailing
/// ".0" (e.g. 22 -> "22", 22.5 -> "22.5").
QString formatAngleDeg(double deg)
{
    QString s = QString::number(deg, 'f', 1);
    if (s.endsWith(QLatin1String(".0")))
        s.chop(2);
    return s;
}


/// Shared stylesheet for card group boxes. Now empty: the global theme QSS
/// already provides the section-card look (mode-aware), and an empty widget
/// stylesheet lets it cascade through.
const char* kCardStyle = "";

/// Card group box used throughout the dialog. ElaScrollPageArea's constructor
/// hard-codes setFixedHeight(75) (it is designed as a fixed-size page slot),
/// which would crush every card's content — lift the constraint so the cards
/// size themselves from their layouts.
ElaScrollPageArea* makeCard(QWidget* parent)
{
    auto* card = new ElaScrollPageArea(parent);
    card->setMinimumHeight(0);
    card->setMaximumHeight(QWIDGETSIZE_MAX);
    return card;
}

/// One connection at an endpoint of the current segment.
} // namespace

LinePropertyDialog::LinePropertyDialog(const QUuid& blockId, const QUuid& segmentId,
                                       cad::param::ParamDocument* paramDoc,
                                       CanvasScene* scene,
                                       QWidget* parent,
                                       bool isCreation)
    : ElaDialog(parent)
    , m_blockId(blockId)
    , m_segmentId(segmentId)
    , m_paramDoc(paramDoc)
    , m_isCreation(isCreation)
    , m_scene(scene)
{
    setWindowTitle(QString::fromUtf8("\u7ebf\u6761\u5c5e\u6027"));  // 线条属性
    setMinimumWidth(480);

    auto* mainLayout = new QVBoxLayout(this);

    m_tabs = new ElaTabWidget(this);
    // ElaTabBar defaults to tabs-closable/movable/accept-drops: the × button
    // triggers ElaTabWidgetPrivate::onTabCloseRequested which deleteLater()s
    // the page (our 锚点 tab IS this dialog's SegmentAnchorTab instance —
    // clicking × would destroy it and crash the next populateFromModel).
    // Disable close/move/drag so tab order and pages are stable.
    if (auto* tabBar = m_tabs->tabBar())
    {
        tabBar->setTabsClosable(false);
        tabBar->setMovable(false);
        tabBar->setAcceptDrops(false);
    }
    m_tabs->setAcceptDrops(false);
    buildPage1(m_tabs);
    // Sub-tabs: 锚点 / 辅助点 / 点连接 (extracted widgets; they read/write
    // the model directly and request scene refresh through callbacks).
    m_anchorTab = new SegmentAnchorTab(m_paramDoc,
                                       [this]() { refreshScene(); }, this);
    m_anchorTab->build(m_tabs);
    m_auxTab = new SegmentAuxTab(m_paramDoc, m_scene,
                                 [this]() { refreshScene(); },
                                 [this]() { if (m_debounce) m_debounce->start(); },
                                 this);
    m_auxTab->build(m_tabs);
    // m_auxTab is a bare QWidget container: its pages were reparented into the
    // tab widget, but the container itself stays an orphan at (0,0,100,30) —
    // and it ends up VISIBLE, covering the 属性/锚点 tab-bar buttons (the
    // reported "属性 tab 点击判定区域极小，只能点靠下" bug). Hide it so clicks
    // reach the QTabBar. (SegmentAnchorTab is registered via addTab(this) and
    // lives inside the QStackedWidget, so it does not need this.)
    m_auxTab->hide();
    connect(m_auxTab, &SegmentAuxTab::jumpRequested,
            this, &LinePropertyDialog::setTarget);
    mainLayout->addWidget(m_tabs);

    const auto btns = cad::ui::makeDialogButtons(
        this, QString::fromUtf8("关闭"), QString::fromUtf8("撤销全部"));
    QObject::disconnect(btns.ok, nullptr, this, nullptr);
    QObject::disconnect(btns.cancel, nullptr, this, nullptr);
    mainLayout->addWidget(btns.row);

    // ElaAppBar's default-close path (isDefaultClosed=true) runs
    // window->close() → processEvents() → window->windowHandle(). With
    // WA_DeleteOnClose the dialog is already deleted by the processEvents()
    // pass, so the handle read is a use-after-free (crash on X). Route the
    // close button through our own reject() instead: the emit branch performs
    // no processEvents/close, so hide-only + explicit deleteLater is safe.
    setIsDefaultClosed(false);
    connect(this, &ElaDialog::closeButtonClicked, this, &LinePropertyDialog::onRejected);

    connect(btns.ok,     &ElaPushButton::clicked, this, &LinePropertyDialog::onAccepted);
    connect(btns.cancel, &ElaPushButton::clicked, this, &LinePropertyDialog::onRejected);

    // Global debounce timer: 200ms after last keystroke → auto-apply
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);
    connect(m_debounce, &QTimer::timeout, this, &LinePropertyDialog::onDebounceTimeout);

    // Sub-tabs need the target before populateFromModel() fills them; the
    // constructor assigns m_blockId/m_segmentId directly, so mirror setTarget
    // here (aux/anchor lists would otherwise stay empty on first open).
    if (m_anchorTab) m_anchorTab->setTarget(m_blockId, m_segmentId);
    if (m_auxTab)    m_auxTab->setTarget(m_blockId, m_segmentId);
    populateFromModel();

    // Give the dialog a sane initial size, clamped to the target screen's
    // available area (the 属性 page scrolls, so a short monitor still works —
    // without clamping, a fixed 900px height overflows 768px laptops and the
    // window's bottom edge lands off-screen, which made it look unresizable).
    QScreen* scr = parent ? parent->screen() : screen();
    const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1280, 800);
    resize(qMin(620, avail.width() - 60), qMin(880, avail.height() - 80));
    connectLiveSignals();
    applyCanvasHighlight();
}

LinePropertyDialog::~LinePropertyDialog()
{
    clearCanvasHighlight();
}

void LinePropertyDialog::applyCanvasHighlight()
{
    if (m_scene && !m_blockId.isNull())
        m_scene->selectBlock(m_blockId);
}

void LinePropertyDialog::clearCanvasHighlight()
{
    if (!m_scene) return;
    if (auto* item = m_scene->findBlockItem(m_blockId))
        item->setSelected(false);
}

void LinePropertyDialog::connectLiveSignals()
{
    // Live update for non-text-input fields (immediate)
    connect(m_editName,      &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_cmbRole,       &QComboBox::currentIndexChanged, this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowName,   &QCheckBox::toggled,            this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowLength, &QCheckBox::toggled,            this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkVisible,    &QCheckBox::toggled,            this, &LinePropertyDialog::onLiveUpdate);
    connect(m_editStartName, &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowStartName, &QCheckBox::toggled,         this, &LinePropertyDialog::onLiveUpdate);
    connect(m_editEndName,   &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowEndName, &QCheckBox::toggled,           this, &LinePropertyDialog::onLiveUpdate);
    connect(m_cmbStyle,      &QComboBox::currentIndexChanged, this, &LinePropertyDialog::onLiveUpdate);
    connect(m_spinWeight,    &QDoubleSpinBox::valueChanged,   this, &LinePropertyDialog::onLiveUpdate);
    connect(m_btnColor,      &QPushButton::clicked,           this, &LinePropertyDialog::onColorPick);
    connect(m_cmbWeight,     &QComboBox::currentIndexChanged, this, &LinePropertyDialog::onWeightPresetChanged);

    // Length: textChanged restarts debounce; editingFinished (Enter/focus-loss) applies immediately
    connect(m_editLength, &QLineEdit::textChanged,     this, &LinePropertyDialog::onLengthDirty);
    connect(m_editLength, &QLineEdit::editingFinished,  this, &LinePropertyDialog::onLengthApply);

    // Tension (curve only): apply on Enter/focus-loss
    connect(m_editTension, &QLineEdit::editingFinished, this, [this] {
        if (!m_paramDoc) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        auto* seg = block->findSegment(m_segmentId);
        if (!seg || !seg->isCurve()) return;
        bool ok = false;
        double val = m_editTension->text().toDouble(&ok);
        if (ok && std::abs(val - seg->tension) > 1e-9) {
            seg->tension = val;
            m_paramDoc->resolveAll();
            refreshScene();
            populateFromModel();
        }
    });

    // ── 跟随角度·连接 card (extracted): debounce + scene/aux re-sync ──
    connect(m_connCard, &SegmentConnectionCard::changed,
            this, &LinePropertyDialog::onConnCardChanged);
    connect(m_connCard, &SegmentConnectionCard::angleEdited,
            this, [this] { if (m_debounce) m_debounce->start(); });

    // ── 终点指向 card (extracted): scene/angle/follower re-sync on mutation ──
    connect(m_aimCard, &SegmentAimCard::changed,
            this, &LinePropertyDialog::onAimCardChanged);
}

void LinePropertyDialog::buildPage1(ElaTabWidget* tabs)
{
    auto* page = new QWidget(this);
    // The 属性 page is taller than most screens (measured ~1040px minimum
    // when every card is laid out at once). Without a scroll area the dialog's
    // minimum height becomes the page's minimum height — the window cannot be
    // resized smaller than the content (reported "弹窗好大，没有办法缩放") and
    // overflows short monitors. Wrap the card column in a scroll area so the
    // window's minimum size stays small and the content scrolls instead.
    auto* scroll = new ElaScrollArea(page);
    scroll->setWidgetResizable(true);
    auto* inner = new QWidget(scroll);
    auto* layout = new QVBoxLayout(inner);
    layout->setSpacing(6);

    // ─── Card: 基本信息 ───
    auto* grpIdentity = makeCard(page);
    auto* idVbox = new QVBoxLayout(grpIdentity);
    idVbox->setContentsMargins(10, 8, 10, 10);
    idVbox->setSpacing(6);
    auto* idTitle = new ElaText(QString::fromUtf8("基本信息"), 13, grpIdentity);
    idTitle->setStyleSheet("font-weight:600;");
    idVbox->addWidget(idTitle);
    auto* idLayout = new QHBoxLayout();
    idVbox->addLayout(idLayout);

    idLayout->addWidget(new ElaText(QString::fromUtf8("\u540d\u79f0:"), 13, grpIdentity));  // 名称:
    m_editName = new ElaLineEdit(grpIdentity);
    m_editName->setPlaceholderText(QString::fromUtf8("\u5982\u201c\u80a9\u7ebf\u201d\u201c\u4fa7\u7f1d\u201d"));  // 如"肩线""侧缝"
    idLayout->addWidget(m_editName, 1);

    idLayout->addWidget(new ElaText(QString::fromUtf8("\u7c7b\u578b:"), 13, grpIdentity));  // 类型:
    m_cmbRole = new ElaComboBox(grpIdentity);
    m_cmbRole->addItem(QString::fromUtf8("\u8f6e\u5ed3\u7ebf"));  // 轮廓线
    m_cmbRole->addItem(QString::fromUtf8("\u5185\u90e8\u7ebf"));  // 内部线
    m_cmbRole->addItem(QString::fromUtf8("\u8f85\u52a9\u7ebf"));  // 辅助线
    idLayout->addWidget(m_cmbRole);

    idLayout->addWidget(new ElaText(QString::fromUtf8("\u7f16\u53f7:"), 13, grpIdentity));  // 编号:
    m_lblSegId = new ElaText(QString(), 13, grpIdentity);
    m_lblSegId->setTextFormat(Qt::RichText);
    m_lblSegId->setStyleSheet("font-size:11px;");
    idLayout->addWidget(m_lblSegId);

    layout->addWidget(grpIdentity);

    // ─── Card: 几何 ───
    auto* grpGeom = makeCard(page);
    auto* geomVbox = new QVBoxLayout(grpGeom);
    geomVbox->setContentsMargins(10, 8, 10, 10);
    geomVbox->setSpacing(6);
    auto* geomTitle = new ElaText(QString::fromUtf8("几何"), 13, grpGeom);  // 几何
    geomTitle->setStyleSheet("font-weight:600;");
    geomVbox->addWidget(geomTitle);
    auto* geomGrid = new QGridLayout();
    geomVbox->addLayout(geomGrid);
    geomGrid->setHorizontalSpacing(8);
    geomGrid->setVerticalSpacing(6);

    // Row 0 — 长度: [fx] [input stretches] [填入]
    geomGrid->addWidget(new ElaText(QString::fromUtf8("\u957f\u5ea6(cm):"), 13, grpGeom), 0, 0);  // 长度(cm):
    m_lblFx = new ElaText(QStringLiteral("<i style='color:#2F6FED;'>fx</i>"), 13, grpGeom);
    m_lblFx->setVisible(false);
    m_lblFx->setFixedWidth(18);
    geomGrid->addWidget(m_lblFx, 0, 1);
    m_editLength = new ElaLineEdit(grpGeom);
    m_editLength->setMinimumWidth(140);
    m_editLength->setPlaceholderText(
        QString::fromUtf8("\u6570\u503c(cm)\u6216\u516c\u5f0f"));  // 数值(cm)或公式
    geomGrid->addWidget(m_editLength, 0, 2);
    geomGrid->setColumnStretch(2, 1);
    auto* btnPasteLen = new ElaPushButton(QStringLiteral("填入"), grpGeom);
    btnPasteLen->setToolTip(QStringLiteral("清空输入框并粘贴剪切板内容"));
    connect(btnPasteLen, &QPushButton::clicked, this, [this] {
        const QString clean = QString(QApplication::clipboard()->text())
                                  .remove(QLatin1Char('\r'))
                                  .remove(QLatin1Char('\n'))
                                  .trimmed();
        if (!clean.isEmpty())
            m_editLength->setText(clean);
    });
    geomGrid->addWidget(btnPasteLen, 0, 3, Qt::AlignLeft | Qt::AlignVCenter);

    // Row 1 — [x] 显示长度标注  实际长度(只读) …… [发布长度参数]
    auto* geomActionRow = new QHBoxLayout();
    m_chkShowLength = new ElaCheckBox(QString::fromUtf8("显示长度标注"), grpGeom);
    geomActionRow->addWidget(m_chkShowLength);
    // Read-only resolved length: when the length field holds a formula /
    // reference name (e.g. M_xxx) the actual value is not visible at a glance.
    m_lblActualLength = new ElaText(QString(), 13, grpGeom);
    m_lblActualLength->setStyleSheet(
        "color:#9CA3AF; font-size:11px; background:transparent;");
    m_lblActualLength->setToolTip(QString::fromUtf8("当前实际长度（只读）"));
    geomActionRow->addWidget(m_lblActualLength);
    geomActionRow->addStretch();
    m_btnPublishLen = new ElaPushButton(QString::fromUtf8("发布长度参数"), grpGeom);
    m_btnPublishLen->setToolTip(QString::fromUtf8(
        "将此线段的长度发布为关联参数，其他公式可引用"));
    m_btnPublishLen->setCursor(Qt::PointingHandCursor);
    geomActionRow->addWidget(m_btnPublishLen);
    connect(m_btnPublishLen, &QPushButton::clicked,
            this, &LinePropertyDialog::onPublishLength);
    geomGrid->addLayout(geomActionRow, 1, 0, 1, 4);

    // Row 2 — 弧长 (curve only, read-only)
    m_arcRow = new QWidget(grpGeom);
    auto* arcLayout = new QHBoxLayout(m_arcRow);
    arcLayout->setContentsMargins(0, 0, 0, 0);
    arcLayout->addWidget(new ElaText(QString::fromUtf8("弧长(cm):"), 13, m_arcRow));
    m_lblArcLength = new ElaText(QStringLiteral("—"), 13, m_arcRow);
    m_lblArcLength->setStyleSheet("font-weight:bold;");
    arcLayout->addWidget(m_lblArcLength, 1);
    m_arcRow->setVisible(false);
    geomGrid->addWidget(m_arcRow, 2, 0, 1, 4);

    // Row 3 — 张力 (curve only)
    m_tensionRow = new QWidget(grpGeom);
    auto* tensionLayout = new QHBoxLayout(m_tensionRow);
    tensionLayout->setContentsMargins(0, 0, 0, 0);
    m_lblTension = new ElaText(QString::fromUtf8("张力:"), 13, m_tensionRow);
    tensionLayout->addWidget(m_lblTension);
    m_editTension = new ElaLineEdit(m_tensionRow);
    m_editTension->setMaximumWidth(80);
    m_editTension->setPlaceholderText(QStringLiteral("0"));
    m_editTension->setToolTip(QString::fromUtf8("0=平滑(Catmull-Rom)  >0更紧  <0更松"));
    tensionLayout->addWidget(m_editTension);
    tensionLayout->addStretch();
    m_tensionRow->setVisible(false);
    geomGrid->addWidget(m_tensionRow, 3, 0, 1, 4);

    // Row 4 — 转换按钮
    m_btnConvert = new ElaPushButton(grpGeom);
    m_btnConvert->setCursor(Qt::PointingHandCursor);
    geomGrid->addWidget(m_btnConvert, 4, 0, 1, 2);
    connect(m_btnConvert, &QPushButton::clicked, this, [this] {
        if (!m_paramDoc) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        auto* seg = block->findSegment(m_segmentId);
        // 仅支持 曲线 → 直线（一键移除全部曲线点）。直线 → 曲线请直接用
        // 智能笔点击线身添加曲线点——旧的"转为曲线"会插入一个 Interpolated
        // 过点（旧曲线笔模型遗留），与新范式的 CurveAnchor 冲突，已移除。
        if (!seg || !seg->isCurve()) return;

        for (const auto& ppId : seg->passPointIds) {
            auto& pts = block->points;
            pts.erase(std::remove_if(pts.begin(), pts.end(),
                [&](const cad::param::ParamPoint& p) { return p.id == ppId; }),
                pts.end());
        }
        block->rebuildPointIndex();
        seg->passPointIds.clear();
        seg->type = cad::param::SegmentType::Line;

        m_paramDoc->resolveAll();
        refreshScene();
        populateFromModel();
    });

    layout->addWidget(grpGeom);

    // ─── Card: 跟随角度 · 连接 (extracted: SegmentConnectionCard) ───
    m_connCard = new SegmentConnectionCard(m_paramDoc, m_scene, page);
    m_connCard->setTarget(m_blockId, m_segmentId);
    layout->addWidget(m_connCard);

    // ─── Card: 终点指向 (extracted: SegmentAimCard) ───
    m_aimCard = new SegmentAimCard(m_paramDoc, page);
    m_aimCard->setTarget(m_blockId);
    layout->addWidget(m_aimCard);

    // ─── Card: 外观 ───
    auto* grpAppear = makeCard(page);
    auto* appearVbox = new QVBoxLayout(grpAppear);
    appearVbox->setContentsMargins(10, 8, 10, 10);
    appearVbox->setSpacing(6);
    auto* appearTitle = new ElaText(QString::fromUtf8("外观"), 13, grpAppear);  // 外观
    appearTitle->setStyleSheet("font-weight:600;");
    appearVbox->addWidget(appearTitle);
    auto* appearLayout = new QFormLayout();
    appearVbox->addLayout(appearLayout);

    m_cmbStyle = new ElaComboBox(grpAppear);
    m_cmbStyle->addItem(QString::fromUtf8("\u5b9e\u7ebf"));   // 实线
    m_cmbStyle->addItem(QString::fromUtf8("\u865a\u7ebf"));   // 虚线
    m_cmbStyle->addItem(QString::fromUtf8("\u70b9\u7ebf"));   // 点线
    appearLayout->addRow(QString::fromUtf8("\u7ebf\u578b:"), m_cmbStyle);  // 线型:

    auto* weightRow = new QHBoxLayout();
    m_cmbWeight = new ElaComboBox(grpAppear);
    m_cmbWeight->addItem(QString::fromUtf8("\u7ec6 (0.8)"), kWeightThin);     // 细
    m_cmbWeight->addItem(QString::fromUtf8("\u4e2d (1.2)"), kWeightMedium);   // 中
    m_cmbWeight->addItem(QString::fromUtf8("\u7c97 (2.0)"), kWeightThick);    // 粗
    m_cmbWeight->addItem(QString::fromUtf8("\u81ea\u5b9a\u4e49"));            // 自定义
    weightRow->addWidget(m_cmbWeight);

    m_spinWeight = new ElaDoubleSpinBox(grpAppear);
    m_spinWeight->setRange(0.5, 10.0);
    m_spinWeight->setSingleStep(0.2);
    m_spinWeight->setDecimals(1);
    m_spinWeight->setVisible(false);
    weightRow->addWidget(m_spinWeight);
    appearLayout->addRow(QString::fromUtf8("\u7c97\u7ec6:"), weightRow);  // 粗细:

    // Plain QPushButton (not ElaPushButton): the swatch color is applied via
    // stylesheet background — Ela's custom painting ignores QSS backgrounds
    // and always shows the theme's default face (white in light mode).
    m_btnColor = new QPushButton(grpAppear);
    m_btnColor->setFixedSize(60, 22);
    m_btnColor->setCursor(Qt::PointingHandCursor);
    appearLayout->addRow(QString::fromUtf8("\u989c\u8272:"), m_btnColor);  // 颜色:

    m_chkVisible = new ElaCheckBox(QString::fromUtf8("\u53ef\u89c1"), grpAppear);  // 可见
    m_chkVisible->setChecked(true);
    appearLayout->addRow(QString(), m_chkVisible);

    m_chkShowName = new ElaCheckBox(QString::fromUtf8("\u5728\u753b\u5e03\u4e0a\u663e\u793a\u540d\u79f0"), grpAppear);  // 在画布上显示名称
    appearLayout->addRow(QString(), m_chkShowName);

    layout->addWidget(grpAppear);

    // ─── 端点 section: 起点 | 终点 side by side ───
    auto* ptRow = new QHBoxLayout();

    // Start point card
    auto* grpStart = makeCard(page);
    auto* startVbox = new QVBoxLayout(grpStart);
    startVbox->setContentsMargins(10, 8, 10, 10);
    startVbox->setSpacing(6);
    auto* startTitle = new ElaText(QString::fromUtf8("起点"), 13, grpStart);  // 起点
    startTitle->setStyleSheet("font-weight:600;");
    startVbox->addWidget(startTitle);
    auto* startLayout = new QFormLayout();
    startVbox->addLayout(startLayout);
    m_lblStartPtId = new ElaText(QString(), 13, grpStart);
    m_lblStartPtId->setTextFormat(Qt::RichText);
    m_lblStartPtId->setStyleSheet("font-size:11px;");
    startLayout->addRow(QString::fromUtf8("\u7f16\u53f7:"), m_lblStartPtId);  // 编号:
    m_editStartName = new ElaLineEdit(grpStart);
    m_editStartName->setPlaceholderText(QString::fromUtf8("\u5982\u201c\u80a9\u70b9\u201d"));  // 如"肩点"
    startLayout->addRow(QString::fromUtf8("\u540d\u79f0:"), m_editStartName);  // 名称:
    m_chkShowStartName = new ElaCheckBox(QString::fromUtf8("\u663e\u793a\u540d\u79f0"), grpStart);  // 显示名称
    startLayout->addRow(QString(), m_chkShowStartName);
    m_editStartAnno = new ElaLineEdit(grpStart);
    startLayout->addRow(QString::fromUtf8("\u5907\u6ce8:"), m_editStartAnno);  // 备注:
    ptRow->addWidget(grpStart);

    // End point card
    auto* grpEnd = makeCard(page);
    auto* endVbox = new QVBoxLayout(grpEnd);
    endVbox->setContentsMargins(10, 8, 10, 10);
    endVbox->setSpacing(6);
    auto* endTitle = new ElaText(QString::fromUtf8("终点"), 13, grpEnd);  // 终点
    endTitle->setStyleSheet("font-weight:600;");
    endVbox->addWidget(endTitle);
    auto* endLayout = new QFormLayout();
    endVbox->addLayout(endLayout);
    m_lblEndPtId = new ElaText(QString(), 13, grpEnd);
    m_lblEndPtId->setTextFormat(Qt::RichText);
    m_lblEndPtId->setStyleSheet("font-size:11px;");
    endLayout->addRow(QString::fromUtf8("\u7f16\u53f7:"), m_lblEndPtId);  // 编号:
    m_editEndName = new ElaLineEdit(grpEnd);
    m_editEndName->setPlaceholderText(QString::fromUtf8("\u5982\u201c\u9888\u70b9\u201d"));  // 如"颈点"
    endLayout->addRow(QString::fromUtf8("\u540d\u79f0:"), m_editEndName);  // 名称:
    m_chkShowEndName = new ElaCheckBox(QString::fromUtf8("\u663e\u793a\u540d\u79f0"), grpEnd);  // 显示名称
    endLayout->addRow(QString(), m_chkShowEndName);
    m_editEndAnno = new ElaLineEdit(grpEnd);
    endLayout->addRow(QString::fromUtf8("\u5907\u6ce8:"), m_editEndAnno);  // 备注:
    ptRow->addWidget(grpEnd);

    layout->addLayout(ptRow);
    layout->addStretch();

    scroll->setWidget(inner);
    auto* pageLay = new QVBoxLayout(page);
    pageLay->setContentsMargins(0, 0, 0, 0);
    pageLay->addWidget(scroll, 1);

    tabs->addTab(page, QString::fromUtf8("属性"));  // 属性
}

void LinePropertyDialog::populateFromModel()
{
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // 基本信息
    m_lblSegId->setText(cad::param::Serial::toHtml(seg->serial));
    m_editName->setText(seg->name);
    m_cmbRole->setCurrentIndex(static_cast<int>(seg->role));

    // 几何
    m_chkShowLength->setChecked(seg->showLength);
    if (!seg->lengthFormula.isEmpty()) {
        m_editLength->setText(seg->lengthFormula);
        m_lblFx->setVisible(true);
    } else {
        m_lblFx->setVisible(false);
        const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
        const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
        if (sp && ep && sp->resolved && ep->resolved) {
            double lenMm = sp->resolvedPos.distanceTo(ep->resolvedPos);
            double lenCm = cad::geo::Units::mmToCm(lenMm);
            m_editLength->setText(QString::number(lenCm, 'f', 2));
        }
    }
    refreshActualLengthLabel();

    // 曲线专有字段
    const bool isCurve = seg->isCurve();
    m_arcRow->setVisible(isCurve);
    m_tensionRow->setVisible(isCurve);
    // 转换按钮仅对曲线显示（一键转回直线）。直线→曲线请用智能笔添加曲线点。
    m_btnConvert->setVisible(isCurve);
    m_btnConvert->setText(QString::fromUtf8("转为直线"));
    if (isCurve) {
        // Arc length (read-only)
        double arcLen = block->segmentLengthAtPoint(seg->startPointId);
        m_lblArcLength->setText(QString::number(cad::geo::Units::mmToCm(arcLen), 'f', 2));
        // Tension
        m_editTension->setText(QString::number(seg->tension, 'f', 1));
        // Length label becomes "弦长"
    }

    // 锚点 Tab
    if (m_anchorTab && m_tabs) {
        int tabIdx = -1;
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (m_tabs->widget(i) == m_anchorTab)
                { tabIdx = i; break; }
        }
        if (tabIdx >= 0)
            m_tabs->setTabEnabled(tabIdx, isCurve);
        if (isCurve)
            m_anchorTab->populateList(block, seg);
    }

    // 跟随角度·连接 card + 终点指向 card (extracted): sync targets.
    if (m_connCard) m_connCard->setTarget(m_blockId, m_segmentId);
    if (m_aimCard)  m_aimCard->setTarget(m_blockId);

    // 外观
    m_cmbStyle->setCurrentIndex(static_cast<int>(seg->lineStyle));
    m_chkVisible->setChecked(seg->visible);
    m_chkShowName->setChecked(seg->showName);
    m_currentColor = seg->color;
    m_btnColor->setStyleSheet(QStringLiteral(
        "background-color: %1; border:1px solid #9CA3AF; border-radius:3px;")
        .arg(m_currentColor.name()));

    // Weight: match to preset or show custom
    updateWeightCombo();

    // 端点
    if (auto* sp = block->findPoint(seg->startPointId)) {
        m_lblStartPtId->setText(cad::param::Serial::toHtml(sp->serial));
        m_editStartName->setText(sp->name);
        m_chkShowStartName->setChecked(sp->showName);
        m_editStartAnno->setText(sp->annotation);
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_lblEndPtId->setText(cad::param::Serial::toHtml(ep->serial));
        m_editEndName->setText(ep->name);
        m_chkShowEndName->setChecked(ep->showName);
        m_editEndAnno->setText(ep->annotation);
    }

    // Follower-state snapshot for 撤销全部: taken ONCE at open time so the
    // revert always restores the pre-dialog state (retargets / connects made
    // inside this dialog are undone too).
    m_snapshot.followerAtt.reset();
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId != m_blockId) continue;
        m_snapshot.followerAtt = att;
        break;
    }

    // Aux connections tab
    if (m_auxTab) m_auxTab->refreshConnections();

    // Aux points tab: refresh direction labels, save snapshots, refresh list
    if (m_auxTab) {
        m_auxTab->refreshDirLabels();
        m_auxTab->saveSnapshots(seg);
        m_auxTab->refreshList();
    }

    // Endpoint-aim card.
    if (m_aimCard)
        m_aimCard->setTarget(m_blockId);

    // Save snapshot for cancel-revert
    m_snapshot.segName       = seg->name;
    m_snapshot.showName      = seg->showName;
    m_snapshot.showLength    = seg->showLength;
    m_snapshot.visible       = seg->visible;
    m_snapshot.role          = static_cast<int>(seg->role);
    m_snapshot.lengthFormula = seg->lengthFormula;
    m_snapshot.color         = seg->color;
    m_snapshot.tension       = seg->tension;
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_snapshot.distance = ep->distance;
        m_snapshot.distanceFormula = ep->distanceFormula;
        m_snapshot.angle = ep->angle;
        m_snapshot.angleFormula = ep->angleFormula;
        m_snapshot.constraint = static_cast<int>(ep->constraint);
        m_snapshot.refPointId = ep->refPointId;
    }
    if (auto* sp = block->findPoint(seg->startPointId)) {
        m_snapshot.startName = sp->name;
        m_snapshot.startShowName = sp->showName;
        m_snapshot.startAnno = sp->annotation;
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_snapshot.endName = ep->name;
        m_snapshot.endShowName = ep->showName;
        m_snapshot.endAnno = ep->annotation;
    }
    m_snapshot.lineStyle = static_cast<int>(seg->lineStyle);
    m_snapshot.weight    = seg->weight;
    m_snapshot.endTargetBlockId = block->endTargetBlockId;
    m_snapshot.endTargetPointId = block->endTargetPointId;
    m_snapshot.endTargetOffset  = block->endTargetOffset;
    m_snapshot.endTargetOffsetFormula = block->endTargetOffsetFormula;

    // Sync publish button state: disable if already published.
    if (m_btnPublishLen) {
        const bool published =
            m_paramDoc->findLinkedBySource(m_blockId, m_segmentId) != nullptr;
        m_btnPublishLen->setEnabled(!published);
        m_btnPublishLen->setText(published
            ? QString::fromUtf8("已发布")
            : QString::fromUtf8("发布长度参数"));
    }

    // Bridge lines: length/angle are passive measurements — lock the editors.
    applyBridgeReadOnly();
}

void LinePropertyDialog::applyHoldOverride(bool forceName, bool forceLength)
{
    if (!m_paramDoc) return;
    const cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // Repaint the toggles as (model value OR held force) WITHOUT writing the
    // model — signals are blocked so no onLiveUpdate round-trip happens.
    const QSignalBlocker b1(m_chkShowName);
    m_chkShowName->setChecked(seg->showName || forceName);
    const QSignalBlocker b2(m_chkShowLength);
    m_chkShowLength->setChecked(seg->showLength || forceLength);
    if (const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId)) {
        const QSignalBlocker b3(m_chkShowStartName);
        m_chkShowStartName->setChecked(sp->showName || forceName);
    }
    if (const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId)) {
        const QSignalBlocker b4(m_chkShowEndName);
        m_chkShowEndName->setChecked(ep->showName || forceName);
    }
}

void LinePropertyDialog::applyBridgeReadOnly()
{
    if (!m_paramDoc) return;
    const cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block || !block->isBridge) return;
    const cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    // Show the measured world length / angle.
    const cad::param::ParamPoint* sp = block->findPoint(seg->startPointId);
    const cad::param::ParamPoint* ep = block->findPoint(seg->endPointId);
    if (sp && ep && sp->resolved && ep->resolved) {
        const cad::geo::Vec2 w1 = block->transform.toWorld(sp->resolvedPos);
        const cad::geo::Vec2 w2 = block->transform.toWorld(ep->resolvedPos);
        const double lenCm = cad::geo::Units::mmToCm(w1.distanceTo(w2));
        m_editLength->setText(QString::number(lenCm, 'f', 2));
    }

    m_editLength->setEnabled(false);
    m_lblFx->setVisible(false);

    // Angle side of the bridge read-only treatment lives in the card.
    if (m_connCard)
        m_connCard->setBridgeReadOnly(true);
}

void LinePropertyDialog::applyToModel()
{
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    seg->name = m_editName->text().trimmed();
    // Keep the owned measure variable's name in sync (测量对象名称 → 测量变量
    // 名称): renaming a bridge/measure line renames its measurement variable.
    m_paramDoc->setOwnerMeasureName(m_blockId, seg->name);
    seg->role = static_cast<cad::param::SegmentRole>(m_cmbRole->currentIndex());
    seg->showName = m_chkShowName->isChecked();
    seg->showLength = m_chkShowLength->isChecked();
    seg->visible = m_chkVisible->isChecked();
    seg->color = m_currentColor;

    // Length: parse as cm number or treat as formula (formula result is in cm)
    QString lenText = m_editLength->text().trimmed();
    bool isNumber = false;
    double numCm = lenText.toDouble(&isNumber);
    if (isNumber) {
        double numMm = cad::geo::Units::cmToMm(numCm);
        seg->lengthFormula.clear();
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula.clear();
            ep->distance = numMm;
        }
    } else if (!lenText.isEmpty()) {
        seg->lengthFormula = lenText;
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula = lenText;
        }
    }

    // Point names/annotations
    if (auto* sp = block->findPoint(seg->startPointId)) {
        sp->name = m_editStartName->text().trimmed();
        sp->showName = m_chkShowStartName->isChecked();
        sp->annotation = m_editStartAnno->text().trimmed();
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        ep->name = m_editEndName->text().trimmed();
        ep->showName = m_chkShowEndName->isChecked();
        ep->annotation = m_editEndAnno->text().trimmed();
    }

    seg->lineStyle = static_cast<cad::param::LineStyle>(m_cmbStyle->currentIndex());
    seg->weight = m_spinWeight->value();

    // NOTE: The follower angle (跟随角度) is intentionally NOT written here.
    // It is owned by the follower's attachment and is applied exclusively by
    // SegmentConnectionCard::applyAngle().
}

void LinePropertyDialog::refreshScene()
{
    // Deferred + coalesced refresh. resolveAll() + refreshAllBlockItems() are
    // heavy on real patterns, and this dialog triggers them from focus-loss
    // (editingFinished) chains: e.g. switching back to the 属性 tab while the
    // aux-point form held focus ran the full re-resolve SYNCHRONOUSLY inside
    // the tab-bar mouse-press handler. Each click then blocked the UI for the
    // whole re-resolve + rebuild + paint, so rapid clicks looked dead and only
    // the final queued press "landed" (the reported 狂点才生效). Deferring to
    // the next event-loop pass keeps clicks responsive; multiple requests
    // within one pass coalesce into a single re-resolve.
    if (m_refreshScheduled) return;
    m_refreshScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_refreshScheduled = false;
        if (m_paramDoc) m_paramDoc->resolveAll();
        if (m_scene) m_scene->refreshAllBlockItems();
        refreshActualLengthLabel();
    });
}

void LinePropertyDialog::refreshActualLengthLabel()
{
    if (!m_lblActualLength || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;
    const auto* sp = block->findPoint(seg->startPointId);
    const auto* ep = block->findPoint(seg->endPointId);
    if (sp && ep && sp->resolved && ep->resolved) {
        const double lenMm = sp->resolvedPos.distanceTo(ep->resolvedPos);
        m_lblActualLength->setText(cad::geo::Units::formatLength(lenMm));
    } else {
        m_lblActualLength->setText(QStringLiteral("—"));
    }
}

void LinePropertyDialog::onLiveUpdate()
{
    applyToModel();
    refreshScene();
}

void LinePropertyDialog::onLengthDirty()
{
    m_editLength->setStyleSheet(QString());

    QString text = m_editLength->text().trimmed();
    bool isNumber = false;
    text.toDouble(&isNumber);
    m_lblFx->setVisible(!isNumber && !text.isEmpty());

    if (m_debounce) m_debounce->start();
}

void LinePropertyDialog::onLengthApply()
{
    applyToModel();
    refreshScene();
    m_editLength->setStyleSheet(QString());
}

void LinePropertyDialog::onColorPick()
{
    // ElaColorDialog 的确定按钮只发射 colorSelected 并 close()（不 accept），
    // 所以用信号取色，而不是依赖 exec() 的返回值。
    ElaColorDialog dlg(this);
    dlg.setCurrentColor(m_currentColor);
    QColor chosen;
    connect(&dlg, &ElaColorDialog::colorSelected, this,
            [&chosen](const QColor& c) { chosen = c; });
    dlg.exec();
    if (!chosen.isValid()) return;

    m_currentColor = chosen;
    m_btnColor->setStyleSheet(QStringLiteral(
        "background-color: %1; border:1px solid #9CA3AF; border-radius:3px;")
        .arg(chosen.name()));

    onLiveUpdate();
}

void LinePropertyDialog::onWeightPresetChanged(int index)
{
    const bool isCustom = (index == m_cmbWeight->count() - 1);
    m_spinWeight->setVisible(isCustom);

    if (!isCustom) {
        double val = m_cmbWeight->currentData().toDouble();
        m_spinWeight->setValue(val);
    }
}

void LinePropertyDialog::updateWeightCombo()
{
    if (!m_paramDoc) return;
    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    double w = seg->weight;

    const bool oldComboState = m_cmbWeight->blockSignals(true);
    const bool oldSpinState  = m_spinWeight->blockSignals(true);

    m_spinWeight->setValue(w);

    if (std::abs(w - kWeightThin) < 0.05) {
        m_cmbWeight->setCurrentIndex(0);
        m_spinWeight->setVisible(false);
    } else if (std::abs(w - kWeightMedium) < 0.05) {
        m_cmbWeight->setCurrentIndex(1);
        m_spinWeight->setVisible(false);
    } else if (std::abs(w - kWeightThick) < 0.05) {
        m_cmbWeight->setCurrentIndex(2);
        m_spinWeight->setVisible(false);
    } else {
        m_cmbWeight->setCurrentIndex(3);  // 自定义
        m_spinWeight->setVisible(true);
    }

    m_cmbWeight->blockSignals(oldComboState);
    m_spinWeight->blockSignals(oldSpinState);
}

void LinePropertyDialog::reject()
{
    // Esc / window-X close must behave like the "撤销全部" button: revert the
    // live-applied edits (QDialog's default reject() only hides the dialog).
    onRejected();
}

void LinePropertyDialog::onAccepted()
{
    applyToModel();
    refreshScene();
    if (m_scene) m_scene->notifyGroupInfoChanged();
    m_confirmed = true;
    accept();
    deleteLater();
}

void LinePropertyDialog::onRejected()
{
    // Creation state (smart pen just drew the line): 撤销全部 = 取消线段创建
    // — delete the line entirely (creation itself never entered the undo
    // stack, so the symmetric removal is a plain removeBlock).
    if (m_isCreation && m_paramDoc && m_paramDoc->findBlock(m_blockId)) {
        m_paramDoc->removeBlock(m_blockId);
        if (m_scene) m_scene->notifyGroupInfoChanged();
        m_confirmed = false;
        QDialog::reject();
        deleteLater();
        return;
    }

    // Edit state: revert to snapshot (取消本次改动).
    if (m_paramDoc) {
        cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
        if (block) {
            cad::param::Segment* const seg = block->findSegment(m_segmentId);
            if (seg) {
                seg->name = m_snapshot.segName;
                seg->showName = m_snapshot.showName;
                seg->showLength = m_snapshot.showLength;
                seg->visible = m_snapshot.visible;
                seg->role = static_cast<cad::param::SegmentRole>(m_snapshot.role);
                seg->lengthFormula = m_snapshot.lengthFormula;
                seg->lineStyle = static_cast<cad::param::LineStyle>(m_snapshot.lineStyle);
                seg->weight = m_snapshot.weight;
                seg->color = m_snapshot.color;
                seg->tension = m_snapshot.tension;
            }
            if (seg) {
                if (auto* ep = block->findPoint(seg->endPointId)) {
                    ep->distance = m_snapshot.distance;
                    ep->distanceFormula = m_snapshot.distanceFormula;
                    ep->angle = m_snapshot.angle;
                    ep->angleFormula = m_snapshot.angleFormula;
                    ep->constraint = static_cast<cad::param::PointConstraint>(m_snapshot.constraint);
                    ep->refPointId = m_snapshot.refPointId;
                    ep->name = m_snapshot.endName;
                    ep->showName = m_snapshot.endShowName;
                    ep->annotation = m_snapshot.endAnno;
                }
                if (auto* sp = block->findPoint(seg->startPointId)) {
                    sp->name = m_snapshot.startName;
                    sp->showName = m_snapshot.startShowName;
                    sp->annotation = m_snapshot.startAnno;
                }
            }

            // Revert endpoint-aim state.
            block->endTargetBlockId = m_snapshot.endTargetBlockId;
            block->endTargetPointId = m_snapshot.endTargetPointId;
            block->endTargetOffset  = m_snapshot.endTargetOffset;
            block->endTargetOffsetFormula = m_snapshot.endTargetOffsetFormula;
        }
        // Revert the follower attachment: remove whatever exists now (it may
        // have been created, retargeted, or replaced during this session) and
        // restore the open-time snapshot (if the line was connected then).
        m_paramDoc->restoreFollowerAttachment(m_blockId, m_snapshot.followerAtt);

        // Revert aux points: remove added ones, restore snapshots.
        if (m_auxTab)
            m_auxTab->restoreSnapshots();

        refreshScene();
    }

    if (m_scene) m_scene->notifyGroupInfoChanged();
    m_confirmed = false;
    QDialog::reject();
    deleteLater();
}

void LinePropertyDialog::setTarget(const QUuid& blockId, const QUuid& segmentId)
{
    m_blockId = blockId;
    m_segmentId = segmentId;
    if (m_anchorTab) m_anchorTab->setTarget(blockId, segmentId);
    if (m_auxTab)    m_auxTab->setTarget(blockId, segmentId);
    populateFromModel();
    applyCanvasHighlight();

    if (m_paramDoc) {
        if (auto* b = m_paramDoc->findBlock(m_blockId)) {
            if (const auto* s = b->findSegment(m_segmentId)) {
                setWindowTitle(QString::fromUtf8("\u7ebf\u6761\u5c5e\u6027 - %1")  // 线条属性 - %1
                    .arg(s->name.isEmpty()
                             ? cad::param::Serial::tag(s->serial)
                             : s->name));
            }
        }
    }
}

void LinePropertyDialog::onDebounceTimeout()
{
    onLengthApply();
    if (m_connCard) m_connCard->applyAngle();
    if (m_auxTab)
        m_auxTab->onLiveUpdate();
}

void LinePropertyDialog::onPublishLength()
{
    if (!m_paramDoc) return;

    if (m_paramDoc->findLinkedBySource(m_blockId, m_segmentId)) return;

    const auto* blk = m_paramDoc->findBlock(m_blockId);
    if (!blk) return;
    const auto* seg = blk->findSegment(m_segmentId);
    if (!seg) return;

    cad::param::LinkedVariable lv = cad::param::LinkedVariable::fromSegment(*blk, *seg);

    auto* stack = m_paramDoc->undoStack();
    if (stack)
        stack->push(new cad::cmd::AddLinkedCommand(m_paramDoc, lv));
    else
        m_paramDoc->addLinked(lv);

    m_btnPublishLen->setEnabled(false);
    m_btnPublishLen->setText(QString::fromUtf8("已发布"));
}

// ---------------------------------------------------------------------------
// 跟随角度·连接 card actions
// ---------------------------------------------------------------------------

const cad::param::MeasureVariable* LinePropertyDialog::findBridgeMeasure() const
{
    if (!m_paramDoc) return nullptr;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!seg) return nullptr;

    const QString formula = seg->lengthFormula.trimmed();
    if (formula.isEmpty()) return nullptr;
    // Case-insensitive match: reference names are uppercase by convention
    // (CopyChip force-uppercases them when the user types/copies a ref name),
    // while MeasureVariable refNames are generated lowercase ("M_" + random
    // prefix). The formula evaluator resolves variables case-insensitively, so
    // a lengthFormula of "M_KTIEY" still evaluates against measure "M_ktiey" —
    // the measure-line detection here must match that same behaviour, or the
    // follow-host / aim-host controls silently disappear for such segments.
    for (const auto& mv : m_paramDoc->measureVars())
        if (mv.refName.compare(formula, Qt::CaseInsensitive) == 0) return &mv;
    return nullptr;
}

void LinePropertyDialog::onAimCardChanged(SegmentAimCard::ChangeKind kind)
{
    // Mirror of the pre-extraction slot effects: every aim mutation needs a
    // scene refresh; offset-only edits leave the angle card alone; the host
    // toggle may back-solve the follower angle → follower/angle re-sync.
    refreshScene();
    if (kind != SegmentAimCard::ChangeKind::OffsetApplied)
        m_connCard->refresh();
}

void LinePropertyDialog::onConnCardChanged(SegmentConnectionCard::ChangeKind kind)
{
    // Mirror of the pre-extraction slot effects: every connection/angle
    // mutation needs a scene refresh; only topology changes (connect /
    // disconnect / re-target) also refresh the aux tab's connection list.
    refreshScene();
    switch (kind) {
    case SegmentConnectionCard::ChangeKind::Connected:
    case SegmentConnectionCard::ChangeKind::Disconnected:
    case SegmentConnectionCard::ChangeKind::Retargeted:
        if (m_auxTab) m_auxTab->refreshConnections();
        break;
    default:
        break;  // Lock / Mode / Angle: no topology change.
    }
}

} // namespace cad::tools
