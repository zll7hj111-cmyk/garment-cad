#include "ui/LinePropertyDialog.h"

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
#include "ElaText.h"
#include "ElaDoubleSpinBox.h"
#include <QGroupBox>
#include "ElaScrollArea.h"
#include <QScreen>
#include "ElaPushButton.h"
#include <QPushButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QPainter>
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
#include "ui/Theme.h"
#include "ui/FormScaffold.h"
#include "ui/NoteButton.h"
#include "parametric/FollowerAngle.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/BlockCommands.h"  // SetLinePropertiesCommand (P0-3)
#include "document/commands/AttachmentCommands.h"  // SetAttachmentBaselineOffsetCommand
#include "ui/PointRefEdit.h"
#include "ui/SegmentAnchorTab.h"
#include "ui/SegmentConnectionCard.h"
#include "ui/SegmentAuxTab.h"

namespace cad::ui {

namespace {

/// Weight presets: 细 / 中 / 粗 (分段按钮; 无选中 = 自定义, 数值见 spin)。
constexpr double kWeightThin   = 0.8;
constexpr double kWeightMedium = 1.2;
constexpr double kWeightThick  = 2.0;

/// 标签列宽 (短词 11px, 全页统一)。
constexpr int kLabelW = 64;
/// 行内控件统一高度 (2026-xx 紧凑化: 35→30, 与状态栏对齐)。
constexpr int kFieldH = 30;

/// 分区标题: [3px 信号黄竖条][11px 600 标题] … [可选右侧控件/提示]。
/// 竖条/分隔线样式走全局 QSS (QFrame#accentBar / QFrame#divider, 明暗双模)。
QWidget* makeSectionHeader(const QString& title, QWidget* parent,
                           QWidget* right = nullptr)
{
    auto* w = new QWidget(parent);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto* bar = new QFrame(w);
    bar->setObjectName(QStringLiteral("accentBar"));
    bar->setFixedSize(3, 12);
    h->addWidget(bar, 0, Qt::AlignVCenter);
    // 分区标题: text1 墨字 13px Bold。**必须保留 background:transparent** ——
    // setStyleSheet 会整体替换 ElaText 构造器的 #ElaText{background-color:
    // transparent} 规则, 漏了它标题变不透明浅底, 细笔画抗锯齿全混成灰
    // (探针实测 avgL≈175, solid 仅 22% = "盖滤镜/掉色" 真凶)。
    auto* t = new ElaText(title, 13, w);
    t->setStyleSheet(QStringLiteral(
        "background: transparent; font-weight:700; color:%1;")
                         .arg(cad::ui::Theme::tokens().text1.name()));
    h->addWidget(t);
    h->addStretch();
    if (right) h->addWidget(right);
    return w;
}

/// 分区之间的 1px hairline (全局 QSS QFrame#divider)。
QFrame* makeDivider(QWidget* parent)
{
    auto* d = new QFrame(parent);
    d->setObjectName(QStringLiteral("divider"));
    return d;
}

/// 输入框紧凑化助手 (2026-xx 与状态栏对齐): 高 30 + 11px 紧凑字。
ElaLineEdit* makeCompactEdit(QWidget* parent, int width)
{
    auto* e = new ElaLineEdit(parent);
    e->setFixedHeight(kFieldH);
    e->setMaximumWidth(width);
    e->setStyleSheet(QStringLiteral("font-size: 11px;"));
    return e;
}

/// 线型分段按钮的预览图标 (48x14, 2px 线样: 实/虚/点)。
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

/// 目标块的非 pin 跟随 attachment (自由线时 nullptr)。
const cad::param::Attachment* findFollowerAttachmentFor(
    const cad::param::ParamDocument* doc, const QUuid& blockId)
{
    if (!doc) return nullptr;
    for (const auto& att : doc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId == blockId)
            return &att;
    }
    return nullptr;
}

/// 上槽绑定的物理点 id (2026-08 用户拍板: 槽位绑定物理点, 创建序靠前者 = P1
/// 恒上)。Block::points 向量序 = 创建序, 取两端点中下标较小者。
QUuid fixedTopPointId(const cad::param::Block* block,
                      const cad::param::Segment* seg)
{
    if (!block || !seg) return QUuid();
    int idxStart = -1, idxEnd = -1, i = 0;
    for (const auto& pt : block->points) {
        if (pt.id == seg->startPointId) idxStart = i;
        if (pt.id == seg->endPointId)   idxEnd = i;
        ++i;
    }
    if (idxStart < 0 || idxEnd < 0) return QUuid();
    return idxStart <= idxEnd ? seg->startPointId : seg->endPointId;
}

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
    // ElaTabBarStyle 默认标签宽 220×35 (3 枚 = 660px) — 超出 ~540 宽单列
    // 面板后 tabRect 溢出 bar 范围 (点击/命中判定落到空处)。收窄到 116:
    // 3 枚 ≈ 348px, 适配单列 inspector 布局。
    m_tabs->setTabSize(QSize(116, 35));
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
    // Sub-tab: 锚点 / 辅助点 (extracted widgets; they read/write the model
    // directly and request scene refresh through callbacks).
    m_anchorTab = new SegmentAnchorTab(m_paramDoc,
                                       [this]() { refreshScene(); }, this);
    m_anchorTab->build(m_tabs);
    m_auxTab = new SegmentAuxTab(m_paramDoc, m_scene,
                                 [this]() { refreshScene(); },
                                 [this]() { if (m_debounce) m_debounce->start(); },
                                 this);
    m_auxTab->build(m_tabs);
    // m_auxTab is a bare QWidget container: its page was reparented into the
    // tab widget, but the container itself stays an orphan at (0,0,100,30) —
    // and it ends up VISIBLE, covering the 属性/锚点 tab-bar buttons (the
    // reported "属性 tab 点击判定区域极小，只能点靠下" bug). Hide it so clicks
    // reach the QTabBar. (SegmentAnchorTab is registered via addTab(this) and
    // lives inside the QStackedWidget, so it does not need this.)
    m_auxTab->hide();
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
    // 2026-12 去卡框化单列 inspector: 默认宽 ~540, 纵向滚动兜底防裁切.
    resize(qMin(540, avail.width() - 60), qMin(860, avail.height() - 80));
    connectLiveSignals();
    applyCanvasHighlight();
    updateWindowTitle();
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
    connect(m_chkShowName,   &QPushButton::toggled,          this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowLength, &QPushButton::toggled,          this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkVisible,    &QPushButton::toggled,          this, &LinePropertyDialog::onLiveUpdate);
    connect(m_editStartName, &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowStartName, &QPushButton::toggled,       this, &LinePropertyDialog::onLiveUpdate);
    connect(m_editEndName,   &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_chkShowEndName, &QPushButton::toggled,         this, &LinePropertyDialog::onLiveUpdate);
    connect(m_spinWeight,    &QDoubleSpinBox::valueChanged,   this, &LinePropertyDialog::onLiveUpdate);
    connect(m_btnColor,      &QPushButton::clicked,           this, &LinePropertyDialog::onColorPick);
    // 线型分段: clicked 仅用户点击触发 (程序化 setChecked 不回环)。
    connect(m_styleGroup, &QButtonGroup::idClicked,
            this, [this](int) { onLiveUpdate(); });
    // 粗细分段: 点击预设 → 写入 spin (valueChanged 链接触发 live apply)。
    connect(m_weightGroup, &QButtonGroup::idClicked,
            this, [this](int id) {
        const double preset[3] = {kWeightThin, kWeightMedium, kWeightThick};
        if (id >= 0 && id < 3) m_spinWeight->setValue(preset[id]);
    });
    // spin 变化后回显分段选中态 (无匹配 = 自定义, 分段全不选)。
    connect(m_spinWeight, &QDoubleSpinBox::valueChanged,
            this, [this](double) { updateWeightControls(); });

    // Length: textChanged restarts debounce; editingFinished (Enter/focus-loss) applies immediately
    connect(m_editLength, &QLineEdit::textChanged,     this, &LinePropertyDialog::onLengthDirty);
    connect(m_editLength, &QLineEdit::editingFinished,  this, &LinePropertyDialog::onLengthApply);
    // 长度模式 (2026-xx §6.3): 自动/指定 chips.
    if (m_lenGroup) {
        connect(m_btnLenAuto, &QPushButton::toggled, this,
                [this](bool checked) { if (checked) onLengthModeChanged(true); });
        connect(m_btnLenSpec, &QPushButton::toggled, this,
                [this](bool checked) { if (checked) onLengthModeChanged(false); });
    }
    // 滑轨/影子偏转 (2026-xx §3 从连接卡拆到摆放分区).
    if (m_cmbSlideMode) {
        connect(m_cmbSlideMode, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &LinePropertyDialog::onSlideModeChanged);
        connect(m_editSlideAlong, &ElaLineEdit::editingFinished,
                this, &LinePropertyDialog::onSlideOffsetEdited);
        connect(m_editSlidePerp, &ElaLineEdit::editingFinished,
                this, &LinePropertyDialog::onSlideOffsetEdited);
        connect(m_editShadow, &ElaLineEdit::editingFinished,
                this, &LinePropertyDialog::onShadowEdited);
    }
    // 端点组内连接 (2026-xx §3).
    if (m_refStartConnect) {
        connect(m_refStartConnect, &PointRefEdit::pointResolved,
                this, &LinePropertyDialog::onStartConnectResolved);
        connect(m_btnStartDetach, &QPushButton::clicked,
                this, &LinePropertyDialog::onStartDetachClicked);
        connect(m_refEndConnect, &PointRefEdit::pointResolved,
                this, &LinePropertyDialog::onEndConnectResolved);
        connect(m_btnEndDetach, &QPushButton::clicked,
                this, &LinePropertyDialog::onEndDetachClicked);
    }

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

    // ── 连接 card: topology re-sync (scene + 联动摘要) ──
    connect(m_connCard, &SegmentConnectionCard::changed,
            this, &LinePropertyDialog::onConnCardChanged);
    // ── 引用线段 card (角度基准/指向点): scene + 角度卡灰态 re-sync ──
    connect(m_refCard, &SegmentRefCard::changed,
            this, &LinePropertyDialog::onRefCardChanged);
    // ── 角度 card: debounce + scene re-sync ──
    connect(m_angleCard, &SegmentAngleCard::changed,
            this, [this] { refreshScene(); });
    connect(m_angleCard, &SegmentAngleCard::angleEdited,
            this, [this] { if (m_debounce) m_debounce->start(); });
}

void LinePropertyDialog::buildPage1(ElaTabWidget* tabs)
{
    auto* page = new QWidget(this);
    // 属性页内容高于多数屏幕: 保留滚动区兜底 (窗口最小尺寸不被内容撑大)。
    auto* scroll = new ElaScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto* inner = new QWidget(scroll);
    auto* layout = new QVBoxLayout(inner);
    layout->setContentsMargins(14, 10, 14, 12);
    layout->setSpacing(8);

    // ═══ 2026-12 面板重设计 v3 (去卡框化单列 inspector, 用户拍板) ═══
    // 分区 (黄竖条标题 + hairline 分隔, 无卡框): 基本 → 几何 → 外观 →
    // 端点 → 连接。规范: 标签列 64 (12px 短词) / 行高 35 / 数值读数等宽 /
    // 子组件全部为无边框行组 (角度/延长/连接/引用)。

    const QString chips = cad::ui::chipButtonStyle();
    const QString dimMono = cad::ui::Theme::dimValueStyle()
        + QString::fromLatin1(cad::ui::ThemeTokens::kMonospaceFamily);

    // ─── 本线 (2026-12: 原「基本」+「外观」合并 —— 这条线叫什么、长什么样) ───
    layout->addWidget(makeSectionHeader(QString::fromUtf8("本线"), page));
    {
        // 名称 + 便利贴注释: 首行无标签, 名称输入 150 (全页值输入列宽) 让行内
        // 便签/显示与端点行同构。
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblNameSp = new ElaText(QString(), 11, page);
        lblNameSp->setFixedWidth(kLabelW);
        row->addWidget(lblNameSp);
        m_editName = makeCompactEdit(page, 150);
        m_editName->setPlaceholderText(QString::fromUtf8("名称，如“肩线”“侧缝”"));
        row->addWidget(m_editName);
        m_noteSeg = new NoteButton(page);
        m_noteSeg->setPlaceholder(QString::fromUtf8("这条线的说明…"));
        connect(m_noteSeg, &NoteButton::noteEdited,
                this, &LinePropertyDialog::onSegNoteEdited);
        row->addWidget(m_noteSeg);
        row->addStretch();
        layout->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblRole = new ElaText(QString::fromUtf8("类型"), 11, page);
        lblRole->setFixedWidth(kLabelW);
        row->addWidget(lblRole);
        m_cmbRole = new ElaComboBox(page);
        m_cmbRole->setFixedWidth(150);
        m_cmbRole->setFixedHeight(kFieldH);
        m_cmbRole->setStyleSheet(QStringLiteral("font-size: 11px;"));
        m_cmbRole->addItem(QString::fromUtf8("轮廓线"));
        m_cmbRole->addItem(QString::fromUtf8("内部线"));
        m_cmbRole->addItem(QString::fromUtf8("辅助线"));
        row->addWidget(m_cmbRole);
        // 完整 ID (随机前缀 + 类型序号): 紧挨类型下拉框右侧, 与输入列对齐
        // (用户 2026-12: "L1 改为显示完整 ID, 移过去对齐; 关键名称标红")。
        // 前缀灰色 + 关键 "L#" 红色加粗 (Serial::toHtml, 与 SerialDelegate 同约定)。
        m_lblSegId = new ElaText(QString(), 11, page);
        m_lblSegId->setStyleSheet(dimMono);
        m_lblSegId->setToolTip(QString::fromUtf8("线段完整编号（全局唯一，随机前缀+类型序号）"));
        row->addWidget(m_lblSegId);
        row->addStretch();
        layout->addLayout(row);
    }
    {
        // 线型: 实线/虚线/点线 分段预览按钮 (图标即语义)。
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblStyle = new ElaText(QString::fromUtf8("线型"), 11, page);
        lblStyle->setFixedWidth(kLabelW);
        row->addWidget(lblStyle);
        m_styleGroup = new QButtonGroup(page);
        m_styleGroup->setExclusive(true);
        const Qt::PenStyle penStyles[3] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine};
        const char* styleTips[3] = {"实线", "虚线", "点线"};
        for (int i = 0; i < 3; ++i) {
            auto* b = new QPushButton(page);
            b->setCheckable(true);
            b->setIcon(lineStyleIcon(penStyles[i]));
            b->setIconSize(QSize(48, 14));
            b->setFixedSize(56, kFieldH);
            b->setStyleSheet(chips);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(QString::fromUtf8(styleTips[i]));
            m_styleGroup->addButton(b, i);
            m_styleBtns[i] = b;
            row->addWidget(b);
        }
        row->addStretch();
        layout->addLayout(row);
    }
    {
        // 粗细: 细/中/粗 分段 + 数值 spin (真值来源; 无分段选中 = 自定义)。
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblWeight = new ElaText(QString::fromUtf8("粗细"), 11, page);
        lblWeight->setFixedWidth(kLabelW);
        row->addWidget(lblWeight);
        m_weightGroup = new QButtonGroup(page);
        m_weightGroup->setExclusive(true);
        const char* weightTexts[3] = {"细", "中", "粗"};
        for (int i = 0; i < 3; ++i) {
            auto* b = new QPushButton(QString::fromUtf8(weightTexts[i]), page);
            b->setCheckable(true);
            b->setFixedHeight(kFieldH);
            b->setStyleSheet(chips);
            b->setCursor(Qt::PointingHandCursor);
            m_weightGroup->addButton(b, i);
            m_weightBtns[i] = b;
            row->addWidget(b);
        }
        m_spinWeight = new ElaDoubleSpinBox(page);
        // ElaDoubleSpinBox Inline 模式按钮占 2×高 (70px): 76px 宽时数值域只剩
        // ~12px → 输入框完全看不见 (用户 2026-12 反馈)。放宽到 110 (编辑域 ~46px)。
        m_spinWeight->setFixedWidth(110);
        m_spinWeight->setFixedHeight(kFieldH);
        m_spinWeight->setStyleSheet(QStringLiteral("font-size: 11px;"));
        m_spinWeight->setRange(0.5, 10.0);
        m_spinWeight->setSingleStep(0.2);
        m_spinWeight->setDecimals(1);
        m_spinWeight->setToolTip(QString::fromUtf8("自定义线宽 (px)"));
        row->addWidget(m_spinWeight);
        row->addStretch();
        layout->addLayout(row);
    }
    {
        // 颜色: 色板 + hex 读数 (点击色板打开取色器)。
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblColor = new ElaText(QString::fromUtf8("颜色"), 11, page);
        lblColor->setFixedWidth(kLabelW);
        row->addWidget(lblColor);
        m_btnColor = new QPushButton(page);
        m_btnColor->setFixedSize(44, kFieldH);
        m_btnColor->setCursor(Qt::PointingHandCursor);
        m_btnColor->setToolTip(QString::fromUtf8("点击选择线段颜色"));
        row->addWidget(m_btnColor);
        m_lblColorHex = new ElaText(QString(), 11, page);
        m_lblColorHex->setStyleSheet(dimMono);
        row->addWidget(m_lblColorHex);
        row->addStretch();
        layout->addLayout(row);
    }
    {
        // 显示: 名称/长度/可见 toggle chips (hold N/L 的临时显示也画在这里)。
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblShow = new ElaText(QString::fromUtf8("显示"), 11, page);
        lblShow->setFixedWidth(kLabelW);
        row->addWidget(lblShow);
        struct ChipDef { const char* text; QPushButton** slot; const char* tip; };
        const ChipDef defs[3] = {
            {"名称", &m_chkShowName,   "在画布上显示线段名称"},
            {"长度", &m_chkShowLength, "在画布上显示长度标注"},
            {"可见", &m_chkVisible,    "隐藏是纯视觉属性：不渲染，但仍可悬停/选择/捕捉"},
        };
        for (const auto& d : defs) {
            auto* b = new QPushButton(QString::fromUtf8(d.text), page);
            b->setCheckable(true);
            b->setFixedHeight(kFieldH);
            b->setStyleSheet(chips);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(QString::fromUtf8(d.tip));
            *d.slot = b;
            row->addWidget(b);
        }
        row->addStretch();
        layout->addLayout(row);
    }
    layout->addWidget(makeDivider(page));

    // ─── 摆放 (这条线摆在哪: 长度 + 方向 + 连接 + 端点; 发布长度参数随长度行走) ───
    layout->addWidget(makeSectionHeader(QString::fromUtf8("摆放"), page));
    // ─── 摆放·长度 (长度是枢纽 §6.3): 输入 150 = 全页唯一值输入列宽 ───
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblLen = new ElaText(QString::fromUtf8("长度"), 11, page);
        lblLen->setFixedWidth(kLabelW);
        row->addWidget(lblLen);
        m_lblFx = new ElaText(
            QStringLiteral("<i style='color:%1;'>fx</i>")
                .arg(cad::ui::Theme::tokens().text2.name()),
            11, page);
        m_lblFx->setVisible(false);
        m_lblFx->setFixedWidth(18);
        row->addWidget(m_lblFx);
        m_editLength = makeCompactEdit(page, 150);
        m_editLength->setPlaceholderText(cad::ui::kPlaceholderCmOrFormula);
        row->addWidget(m_editLength);
        // 长度模式 (2026-xx §6.3): 自动 = 两端钉死 (桥接), 指定 = 起点钉死 + 长度。
        m_btnLenAuto = new QPushButton(QString::fromUtf8("自动"), page);
        m_btnLenAuto->setObjectName(QStringLiteral("lengthAutoChip"));
        m_btnLenAuto->setCheckable(true);
        m_btnLenAuto->setFixedSize(48, kFieldH);
        m_btnLenAuto->setStyleSheet(chips);
        m_btnLenAuto->setCursor(Qt::PointingHandCursor);
        m_btnLenAuto->setToolTip(QString::fromUtf8(
            "自动：两端都钉在宿主点上，长度由两点距离算出（桥接行为）。"));
        m_btnLenSpec = new QPushButton(QString::fromUtf8("指定"), page);
        m_btnLenSpec->setObjectName(QStringLiteral("lengthSpecChip"));
        m_btnLenSpec->setCheckable(true);
        m_btnLenSpec->setFixedSize(48, kFieldH);
        m_btnLenSpec->setStyleSheet(chips);
        m_btnLenSpec->setCursor(Qt::PointingHandCursor);
        m_btnLenSpec->setToolTip(QString::fromUtf8(
            "指定：起点钉在宿主点上，按角度延伸，长度由本输入框指定。"));
        m_lenGroup = new QButtonGroup(page);
        m_lenGroup->addButton(m_btnLenAuto);
        m_lenGroup->addButton(m_btnLenSpec);
        row->addWidget(m_btnLenAuto);
        row->addWidget(m_btnLenSpec);
        auto* btnPasteLen = new QPushButton(QStringLiteral("填入"), page);
        btnPasteLen->setFixedSize(48, kFieldH);
        btnPasteLen->setStyleSheet(chips);
        btnPasteLen->setToolTip(QStringLiteral("清空输入框并粘贴剪切板内容"));
        connect(btnPasteLen, &QPushButton::clicked, this, [this] {
            const QString clean = QString(QApplication::clipboard()->text())
                                      .remove(QLatin1Char('\r'))
                                      .remove(QLatin1Char('\n'))
                                      .trimmed();
            if (!clean.isEmpty())
                m_editLength->setText(clean);
        });
        row->addWidget(btnPasteLen);
        // 发布长度参数 (2026-12: 从「基本」区迁回长度行 —— 它发布的就是长度,
        // 放本线区纯属当年为了对齐好看)。
        m_btnPublishLen = new QPushButton(QString::fromUtf8("发布参数"), page);
        m_btnPublishLen->setFixedHeight(kFieldH);
        m_btnPublishLen->setStyleSheet(chips);
        m_btnPublishLen->setToolTip(QString::fromUtf8(
            "将本线段的长度发布为关联参数（只读），其他公式可直接引用其引用名（L+编号）"));
        m_btnPublishLen->setCursor(Qt::PointingHandCursor);
        connect(m_btnPublishLen, &QPushButton::clicked,
                this, &LinePropertyDialog::onPublishLength);
        row->addWidget(m_btnPublishLen);
        m_lblActualLength = new ElaText(QString(), 11, page);
        m_lblActualLength->setStyleSheet(dimMono);
        m_lblActualLength->setToolTip(QString::fromUtf8("当前实际长度（只读）"));
        row->addWidget(m_lblActualLength);
        row->addStretch();
        layout->addLayout(row);
    }
    // 滑轨行 (§3, 2026-08-31 栅格化): [滑轨64][向沿 150][垂直 150] ——
    // 两个输入框同宽同列 (150 = 全页值输入列宽), 内嵌短标签右对齐。
    m_slideRow = new QWidget(page);
    {
        auto* slideLayout = new QHBoxLayout(m_slideRow);
        slideLayout->setContentsMargins(0, 0, 0, 0);
        slideLayout->setSpacing(6);
        auto* lblSlide = new ElaText(QString::fromUtf8("滑轨"), 11, m_slideRow);
        lblSlide->setFixedWidth(kLabelW);
        lblSlide->setToolTip(QString::fromUtf8(
            "抽屉式单向滑动：连接姿态保持（角度始终随基准线），但位置只留一个自由度。"));
        slideLayout->addWidget(lblSlide);
        auto* lblAlong = new ElaText(QString::fromUtf8("沿向"), 11, m_slideRow);
        lblAlong->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lblAlong->setStyleSheet(QStringLiteral("background: transparent;"));
        lblAlong->setToolTip(QString::fromUtf8("沿基准线方向 (cm)"));
        slideLayout->addWidget(lblAlong);
        m_editSlideAlong = new ElaLineEdit(m_slideRow);
        m_editSlideAlong->setFixedWidth(150);
        m_editSlideAlong->setPlaceholderText(QString::fromUtf8("0"));
        m_editSlideAlong->setToolTip(QString::fromUtf8(
            "沿基准线方向偏移（cm）。数值或公式（如 肩宽/2）；留空/0 表示不偏移。"));
        m_editSlideAlong->setFixedHeight(kFieldH);
        m_editSlideAlong->setStyleSheet(QStringLiteral("font-size: 11px;"));
        slideLayout->addWidget(m_editSlideAlong);
        auto* lblPerpSp = new ElaText(QString(), 11, m_slideRow);
        lblPerpSp->setFixedWidth(18);
        slideLayout->addWidget(lblPerpSp);
        auto* lblPerp = new ElaText(QString::fromUtf8("垂直"), 11, m_slideRow);
        lblPerp->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lblPerp->setStyleSheet(QStringLiteral("background: transparent;"));
        lblPerp->setToolTip(QString::fromUtf8("垂直基准线方向 (cm)"));
        slideLayout->addWidget(lblPerp);
        m_editSlidePerp = new ElaLineEdit(m_slideRow);
        m_editSlidePerp->setFixedWidth(150);
        m_editSlidePerp->setPlaceholderText(QString::fromUtf8("0"));
        m_editSlidePerp->setToolTip(QString::fromUtf8(
            "垂直基准线方向偏移（cm）。数值或公式；留空/0 表示不偏移。"));
        m_editSlidePerp->setFixedHeight(kFieldH);
        m_editSlidePerp->setStyleSheet(QStringLiteral("font-size: 11px;"));
        slideLayout->addWidget(m_editSlidePerp);
        m_cmbSlideMode = new QComboBox(m_slideRow);
        m_cmbSlideMode->setFixedHeight(kFieldH);
        m_cmbSlideMode->setStyleSheet(QStringLiteral("font-size: 11px;"));
        m_cmbSlideMode->addItem(QString::fromUtf8("全连接"));
        m_cmbSlideMode->addItem(QString::fromUtf8("沿线滑动"));
        m_cmbSlideMode->addItem(QString::fromUtf8("垂直拉出"));
        m_cmbSlideMode->setVisible(false);
        m_lblSlideBadge = new ElaText(QString(), 11, m_slideRow);
        m_lblSlideBadge->setStyleSheet(cad::ui::Theme::tealBadgeStyle());
        m_lblSlideBadge->setToolTip(QString::fromUtf8(
            "滑轨状态：角度跟随保持，但位置只留一个自由度。"));
        slideLayout->addWidget(m_lblSlideBadge);
        slideLayout->addStretch();
    }
    layout->addWidget(m_slideRow);

    // ─── 连接/端点 (2026-xx §3): 端点组内集成「连接到 + 拆开/重连」 ───
    m_lblConnHint = new ElaText(QString(), 12, page);
    m_lblConnHint->setStyleSheet(dimMono);
    layout->addWidget(m_lblConnHint);

    // 连接卡保留为隐藏辅助对象（测试/逻辑兼容），不参与布局 —— 本对话框的
    // 连接 UI 在端点组内 (连接到/拆开)。**严禁 setVisible(true)**: 它有父但
    // 不在任何布局里, 置真会以 (0,0) 孤儿控件叠在本线分区上。
    m_connCard = new SegmentConnectionCard(m_paramDoc, m_scene, page);
    m_connCard->setTarget(m_blockId, m_segmentId);
    m_connCard->setVisible(false);
    m_refCard = new SegmentRefCard(m_paramDoc, m_scene, page);
    m_refCard->setTarget(m_blockId, m_segmentId);

    // ─── 端点 (2026-08-31 重设计, 去灰底卡框): 徽章 + 主网格行组 + 朝向轴 ───
    // 结构: [P1][名称][便利贴][显示] → 主网格行组 (延长/连接到, 标签列 64 对齐全页)
    //       ···垂直虚线轴··· [↓] ···轴···   (换向箭头嵌在轴中段, §6.1)
    //       [P2][名称][便利贴][显示] → 同构行组
    // 栅格纪律 (2026-08-31 二轮): 标签列恒 64 (kLabelW), 值输入恒 150 ——
    // 端点块不再缩进, 与 长度/滑轨/角度 的标签列/输入列完全对齐。
    {
        auto* col = new QVBoxLayout();
        col->setSpacing(4);

        // 单个端点组。返回该组的主容器 (无样式 QWidget, objectName 是测试契约)。
        auto buildEndpoint = [&](bool isStart) -> QWidget* {
            ElaText*& ptBadge = isStart ? m_lblStartPtId : m_lblEndPtId;
            ElaLineEdit*& nameEdit = isStart ? m_editStartName : m_editEndName;
            NoteButton*& noteBtn = isStart ? m_noteStart : m_noteEnd;
            QPushButton*& showChip = isStart ? m_chkShowStartName : m_chkShowEndName;
            ElaLineEdit*& extEdit = isStart ? m_editStartExtend : m_editEndExtend;
            PointRefEdit*& refConn = isStart ? m_refStartConnect : m_refEndConnect;
            QPushButton*& detachBtn = isStart ? m_btnStartDetach : m_btnEndDetach;
            ElaText*& connSummary = isStart ? m_lblStartConn : m_lblEndConn;
            auto* panel = new QWidget(page);
            panel->setObjectName(isStart ? QStringLiteral("startPointCard")
                                         : QStringLiteral("endPointCard"));
            // 左缘 padding = kLabelW: 首行徽章占标签列位, 行组值列与全页对齐。
            auto* v = new QVBoxLayout(panel);
            v->setContentsMargins(kLabelW, 0, 0, 0);
            v->setSpacing(4);

            // ── 首行: [P1徽章] [名称输入(150, 等宽主输入)][便利贴][显示] ──
            {
                auto* head = new QHBoxLayout();
                head->setSpacing(6);
                ptBadge = new ElaText(QString(), 11, panel);
                ptBadge->setObjectName(QStringLiteral("endpointBadge"));
                ptBadge->setStyleSheet(QStringLiteral(
                    "font-weight:600; %1 background:transparent;")
                    .arg(QString::fromLatin1(cad::ui::ThemeTokens::kMonospaceFamily)));
                ptBadge->setFixedWidth(44);
                ptBadge->setAlignment(Qt::AlignCenter);
                ptBadge->setToolTip(QString::fromUtf8(
                    isStart ? "上组 = 点1（线段第一个点，位置固定）；进出方向见中间箭头"
                            : "下组 = 点2（线段第二个点，位置固定）；进出方向见中间箭头"));
                head->addWidget(ptBadge);
                nameEdit = new ElaLineEdit(panel);
                nameEdit->setFixedWidth(150);   // 值输入列宽, 与 长度/角度 同栅格
                nameEdit->setFixedHeight(kFieldH);
                nameEdit->setStyleSheet(QStringLiteral("font-size: 11px;"));
                nameEdit->setPlaceholderText(QString::fromUtf8("名称，如“肩点”"));
                head->addWidget(nameEdit);
                noteBtn = new NoteButton(panel);
                noteBtn->setPlaceholder(QString::fromUtf8("这个点的说明…"));
                connect(noteBtn, &NoteButton::noteEdited, this,
                        isStart ? &LinePropertyDialog::onStartNoteEdited
                                : &LinePropertyDialog::onEndNoteEdited);
                head->addWidget(noteBtn);
                showChip = new QPushButton(QString::fromUtf8("显示"), panel);
                showChip->setCheckable(true);
                showChip->setFixedHeight(kFieldH);
                showChip->setStyleSheet(chips);
                showChip->setCursor(Qt::PointingHandCursor);
                showChip->setToolTip(QString::fromUtf8("在画布上显示该点名称"));
                head->addWidget(showChip);
                head->addStretch();
                v->addLayout(head);
            }

            // ── 值行组: 标签列 0 (父容器已让出 kLabelW) + 值输入恒 150 ──
            {
                // 延长量 (§6.2)。
                auto* r = new QHBoxLayout();
                r->setSpacing(6);
                auto* lblExt = new ElaText(QString::fromUtf8("延长量"), 11, panel);
                lblExt->setStyleSheet(QStringLiteral("background: transparent;"));
                r->addWidget(lblExt);
                extEdit = new ElaLineEdit(panel);
                extEdit->setObjectName(isStart ? QStringLiteral("startExtendEdit")
                                               : QStringLiteral("endExtendEdit"));
                extEdit->setFixedWidth(150);   // 值输入列宽, 全页统一
                extEdit->setFixedHeight(kFieldH);
                extEdit->setStyleSheet(QStringLiteral("font-size: 11px;"));
                extEdit->setPlaceholderText(QString::fromUtf8("0"));
                extEdit->setToolTip(QString::fromUtf8(
                    "沿该端朝外方向延长的距离；数值或公式 cm；只允许 >= 0"));
                connect(extEdit, &ElaLineEdit::editingFinished, this,
                        isStart ? &LinePropertyDialog::onStartExtendEdited
                                : &LinePropertyDialog::onEndExtendEdited);
                r->addWidget(extEdit);
                r->addStretch();
                v->addLayout(r);
                // 连接到 (§3): 输入 150 + 拆开钮 48。
                {
                    auto* rc = new QHBoxLayout();
                    rc->setSpacing(6);
                    auto* lblConn = new ElaText(QString::fromUtf8("连接到"), 11, panel);
                    lblConn->setStyleSheet(QStringLiteral("background: transparent;"));
                    rc->addWidget(lblConn);
                    refConn = new PointRefEdit(m_paramDoc, panel);
                    refConn->setObjectName(isStart ? QStringLiteral("startConnectEdit")
                                                   : QStringLiteral("endConnectEdit"));
                    refConn->setFixedWidth(150);   // 值输入列宽, 全页统一
                    refConn->setFixedHeight(kFieldH);
                    refConn->setToolTip(QString::fromUtf8(
                        isStart ? "输入目标点 P# 或线段 L#/名称，回车建立/重定向跟随连接（吸附；本端为进/起点时可用）。"
                                : "输入目标点 P# 或线段 L#/名称，回车建立/重定向终点指向（本端为出/终点时可用）。"));
                    rc->addWidget(refConn);
                    detachBtn = new QPushButton(QString::fromUtf8("拆开"), panel);
                    detachBtn->setObjectName(isStart ? QStringLiteral("startDetachBtn")
                                                     : QStringLiteral("endDetachBtn"));
                    detachBtn->setFixedSize(48, kFieldH);
                    detachBtn->setStyleSheet(chips);
                    detachBtn->setCursor(Qt::PointingHandCursor);
                    rc->addWidget(detachBtn);
                    rc->addStretch();
                    v->addLayout(rc);
                }
                // 摘要灰字 (跟随/挂载/指向): 空时隐藏 —— 不留占位灰块。
                connSummary = new ElaText(QString(), 10, panel);
                connSummary->setObjectName(isStart ? QStringLiteral("startPointConn")
                                                   : QStringLiteral("endPointConn"));
                connSummary->setStyleSheet(dimMono);
                connSummary->setFixedHeight(16);
                connSummary->setToolTip(QString::fromUtf8(
                    "该端点上的连接：跟随 = 本线跟随的基准线；"
                    "挂载 = 吸附在本端点上的下游线（反向连接）；"
                    "指向 = 本线终点指向的目标线"));
                connSummary->setVisible(false);
                v->addWidget(connSummary, 0, Qt::AlignLeft);
            }
            return panel;
        };

        auto* startPanel = buildEndpoint(/*isStart=*/true);
        col->addWidget(startPanel);

        // ── 朝向轴 (§6.1): 垂直虚线 + 换向箭头嵌在中段 ──
        m_btnDirectionArrow = new QPushButton(QString::fromUtf8("↓"), page);
        m_btnDirectionArrow->setObjectName(QStringLiteral("directionArrowBtn"));
        m_btnDirectionArrow->setFixedSize(28, kFieldH);
        m_btnDirectionArrow->setStyleSheet(chips);
        m_btnDirectionArrow->setCursor(Qt::PointingHandCursor);
        m_btnDirectionArrow->setToolTip(QString::fromUtf8(
            "调换进/出：↓ = 上进下出，↑ = 下进上出（点位置固定，几何不变）"));
        connect(m_btnDirectionArrow, &QPushButton::clicked,
                this, &LinePropertyDialog::onDirectionArrowClicked);
        {
            auto* axis = new QWidget(page);
            axis->setFixedHeight(kFieldH + 12);
            auto* al = new QHBoxLayout(axis);
            al->setContentsMargins(kLabelW + 6, 0, 0, 0);
            al->setSpacing(8);
            auto* line = new QFrame(axis);
            line->setObjectName(QStringLiteral("endpointAxis"));
            line->setFixedSize(1, kFieldH + 10);
            al->addWidget(line);
            al->addWidget(m_btnDirectionArrow);
            al->addStretch();
            col->addWidget(axis);
        }

        auto* endPanel = buildEndpoint(/*isStart=*/false);
        col->addWidget(endPanel);
        layout->addLayout(col);
    }

    // 角度区 (§3 顺序): ①对齐点+方向两段式行 对齐点 [P1] 方向：点1→点2 [独立]
    // (文案 v2, 2026-12 用户拍板) → ②角度输入行 → ③影子偏转。方向行在前:
    // 先回答"对齐到什么方向", 再给数值。
    m_angleCard = new SegmentAngleCard(m_paramDoc, m_scene, page);
    layout->addWidget(m_refCard);
    layout->addWidget(m_angleCard);

    // 影子偏转行 (§3: 常显可输入): 标签 64 + 输入 150 (全页统一列宽)。
    m_shadowRow = new QWidget(page);
    {
        auto* shadowLayout = new QHBoxLayout(m_shadowRow);
        shadowLayout->setContentsMargins(0, 0, 0, 0);
        shadowLayout->setSpacing(6);
        auto* lblShadowTitle = new ElaText(QString::fromUtf8("影子偏转"), 11, m_shadowRow);
        lblShadowTitle->setFixedWidth(kLabelW);
        lblShadowTitle->setFixedHeight(kFieldH);
        lblShadowTitle->setMinimumHeight(kFieldH);
        shadowLayout->addWidget(lblShadowTitle);
        m_editShadow = new ElaLineEdit(m_shadowRow);
        m_editShadow->setFixedWidth(150);
        m_editShadow->setPlaceholderText(QString::fromUtf8("0"));
        m_editShadow->setToolTip(QString::fromUtf8(
            "影子基准相对基准线累计偏转的度数（选集/整组刚体旋转时产生）："
            "本线的角度跟随仍归基准线，但整体叠加此偏转——像给基准配了一根随组转动的隐形影子。"
            "输入 0 = 本线转回与基准当前方向对齐。"));
        m_editShadow->setFixedHeight(kFieldH);
        m_editShadow->setStyleSheet(QStringLiteral("font-size: 11px;"));
        shadowLayout->addWidget(m_editShadow);
        shadowLayout->addStretch();
    }
    layout->addWidget(m_shadowRow);
    layout->addWidget(makeDivider(page));

    // ─── 按需组 (仅曲线显示: 弧长 / 张力 / 转为直线) ───
    layout->addWidget(makeSectionHeader(QString::fromUtf8("按需组"), page));
    m_arcRow = new QWidget(page);
    {
        auto* arcLayout = new QHBoxLayout(m_arcRow);
        arcLayout->setContentsMargins(0, 0, 0, 0);
        arcLayout->setSpacing(6);
        auto* lblArc = new ElaText(QString::fromUtf8("弧长"), 11, m_arcRow);
        lblArc->setFixedWidth(kLabelW);
        arcLayout->addWidget(lblArc);
        m_lblArcLength = new ElaText(QStringLiteral("—"), 11, m_arcRow);
        m_lblArcLength->setStyleSheet(
            QStringLiteral("font-weight:600; background:transparent;")
            + QString::fromLatin1(cad::ui::ThemeTokens::kMonospaceFamily));
        arcLayout->addWidget(m_lblArcLength);
        arcLayout->addStretch();
    }
    m_arcRow->setVisible(false);
    layout->addWidget(m_arcRow);
    m_tensionRow = new QWidget(page);
    {
        auto* tensionLayout = new QHBoxLayout(m_tensionRow);
        tensionLayout->setContentsMargins(0, 0, 0, 0);
        tensionLayout->setSpacing(6);
        auto* lblTension = new ElaText(QString::fromUtf8("张力"), 11, m_tensionRow);
        lblTension->setFixedWidth(kLabelW);
        tensionLayout->addWidget(lblTension);
        m_editTension = makeCompactEdit(m_tensionRow, 150);
        m_editTension->setPlaceholderText(QStringLiteral("0"));
        m_editTension->setToolTip(QString::fromUtf8("0=平滑(Catmull-Rom)  >0更紧  <0更松"));
        tensionLayout->addWidget(m_editTension);
        tensionLayout->addStretch();
        // 转换按钮仅对曲线显示（一键转回直线）。直线→曲线请用智能笔添加曲线点。
        m_btnConvert = new QPushButton(QString::fromUtf8("转为直线"), m_tensionRow);
        m_btnConvert->setFixedHeight(kFieldH);
        m_btnConvert->setStyleSheet(chips);
        m_btnConvert->setCursor(Qt::PointingHandCursor);
        tensionLayout->addWidget(m_btnConvert);
    }
    m_tensionRow->setVisible(false);
    layout->addWidget(m_tensionRow);
    connect(m_btnConvert, &QPushButton::clicked, this, [this] {
        if (!m_paramDoc) return;
        auto* block = m_paramDoc->findBlock(m_blockId);
        if (!block) return;
        auto* seg = block->findSegment(m_segmentId);
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
    layout->addWidget(makeDivider(page));

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

    // 回填必须静音信号: 这些控件的 textChanged/toggled/currentIndexChanged
    // 都接 onLiveUpdate → applyToModel (把输入框内容写回"当前角色"端点)。
    // 换向后端点角色互换, 逐字段 setText 的回波会用另一输入框的旧内容
    // 覆盖模型名 (先回填起点卡 → 回波把"终点输入框里的旧起点名"写进
    // 新终点 → 再回填终点卡时读到已污染的值 → 原名永久丢失)。
    QSignalBlocker bName(m_editName);
    QSignalBlocker bRole(m_cmbRole);
    QSignalBlocker bShowLen(m_chkShowLength);
    QSignalBlocker bLen(m_editLength);
    QSignalBlocker bTension(m_editTension);
    QSignalBlocker bVis(m_chkVisible);
    QSignalBlocker bShowName(m_chkShowName);
    QSignalBlocker bStartName(m_editStartName);
    QSignalBlocker bStartShow(m_chkShowStartName);
    QSignalBlocker bEndName(m_editEndName);
    QSignalBlocker bEndShow(m_chkShowEndName);

    // 基本信息: 完整 ID 灰前缀 + 红色关键 "L#" (用户 2026-12: 关键名称标红).
    m_lblSegId->setText(cad::param::Serial::toHtml(seg->serial));
    m_editName->setText(seg->name);
    m_noteSeg->setNote(seg->annotation);  ///< setNote 不发 noteEdited (非用户编辑).
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
            m_editLength->setText(cad::geo::Units::formatNumberTrimmed(lenCm));
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
        m_lblArcLength->setText(cad::geo::Units::formatCmTrimmed(arcLen));
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

    // 连接/引用/角度 card (extracted): sync targets.
    if (m_connCard)  m_connCard->setTarget(m_blockId, m_segmentId);
    if (m_refCard)   m_refCard->setTarget(m_blockId, m_segmentId);
    if (m_angleCard) m_angleCard->setTarget(m_blockId, m_segmentId);
    // 省道线: 角度/引用卡无意义 (连接卡已接管省道态) → 隐藏。
    // (m_connCard 恒隐藏 —— 仅测试/逻辑兼容的离屏辅助对象, 见 buildPage1。)
    // 引用卡与终点指向/省道互斥的隐藏判定已收口到 SegmentRefCard::refresh
    // (2026-09 审核 F5), 此处不再重复 setVisible。
    if (m_angleCard) m_angleCard->setVisible(!block->isDart());

    // 外观 (分段按钮 setChecked 不触发 clicked, 无回环)
    if (auto* b = m_styleGroup->button(static_cast<int>(seg->lineStyle)))
        b->setChecked(true);
    m_chkVisible->setChecked(seg->visible);
    m_chkShowName->setChecked(seg->showName);
    m_currentColor = seg->color;
    m_btnColor->setStyleSheet(QStringLiteral(
        "background-color: %1; border:1px solid %2; border-radius:2px;")
        .arg(m_currentColor.name(), cad::ui::Theme::tokens().text3.name()));
    m_lblColorHex->setText(m_currentColor.name().toUpper());

    // 粗细: 分段 + spin 与模型值同步
    updateWeightControls();

    // 端点组 (2026-08 用户再拍板): **槽位绑定物理点** —— 上槽恒 = 先建的点
    // (P1), 下槽 = 后建点 (P2)。换向 = 进出互换 (ReverseSegmentCommand 交换
    // start/end 身份), 面板上点的位置不变, 变化只有: ① 朝向箭头翻面 (↓/↑ =
    // 上进下出/下进上出); ② 各槽的 延长量/连接行 改按该点**当前角色**
    // (start/end) 取值 —— 物理尾巴跟点走, 徽章不跳。
    {
        const QUuid topId = fixedTopPointId(block, seg);
        m_topIsStart = (topId == seg->startPointId);
        const QUuid ptIds[2] = { topId,
                                 topId == seg->startPointId ? seg->endPointId
                                                            : seg->startPointId };
        for (int slot = 0; slot < 2; ++slot) {
            const auto* pt = block->findPoint(ptIds[slot]);
            if (!pt) continue;
            auto& badge = slot == 0 ? m_lblStartPtId : m_lblEndPtId;
            auto& nameEdit = slot == 0 ? m_editStartName : m_editEndName;
            auto& showChip = slot == 0 ? m_chkShowStartName : m_chkShowEndName;
            auto& note = slot == 0 ? m_noteStart : m_noteEnd;
            badge->setText(cad::param::Serial::tag(pt->serial));
            nameEdit->setText(pt->name);
            showChip->setChecked(pt->showName);
            note->setNote(pt->annotation);
        }
        // 延长量按槽内点的**当前角色**取值: 上槽点若当前是 start → 读
        // extendStart, 是 end → 读 extendEnd (物理尾巴跟着点走)。
        // 箭头指示器在 refreshDirectionArrow 统一刷新 (进出方向)。
        refreshEndpointExtends();
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
    refreshConnHint();

    // 端点双列微卡只读连接行 (方案 A, 2026-xx)。
    refreshEndpointConnRows();

    // Aux points tab: refresh direction labels, save snapshots, refresh list
    if (m_auxTab) {
        m_auxTab->refreshDirLabels();
        m_auxTab->saveSnapshots(seg);
        m_auxTab->refreshList();
    }


    // Save snapshot for cancel-revert
    m_snapshot.segName       = seg->name;
    m_snapshot.segAnnotation = seg->annotation;  // 便利贴注释 (撤销全部时还原).
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
    m_snapshot.lengthAuto = block->lengthAuto;   // 2026-09 审核收口 (撤销全部还原)
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
            : QString::fromUtf8("发布参数"));
    }

    // Bridge lines: length/angle are passive measurements — lock the editors.
    applyBridgeReadOnly();

    // 朝向箭头: 换向资格 + 方向 (P1→P2 世界朝向). 换向后**端点组位置固定**
    // (2026-08-31 用户拍板), 箭头只翻方向 —— populate 已按几何位置回填。
    refreshDirectionArrow();

    // 端点延长量: 置灰 + 回填数值/公式.
    refreshEndpointExtends();
    // 长度模式 + 滑轨/影子偏转 (2026-xx §3/§6.3).
    refreshLengthMode();
    refreshSlideShadow();
}

void LinePropertyDialog::refreshEndpointConnRows()
{
    if (!m_lblStartConn || !m_lblEndConn) return;
    if (!m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) {
        m_lblStartConn->setText(QString());
        m_lblEndConn->setText(QString());
        m_lblStartConn->setVisible(false);   // 空摘要不占位 (2026-08-31 重设计)
        m_lblEndConn->setVisible(false);
        return;
    }

    // 端点连接摘要 (方案 A, 2026-xx): 跟随 (本线该端点作为跟随点) / 挂载
    // (下游线吸附在本端点, 反向连接) / 指向 (终点卡的 endTarget 目标线)。
    auto endpointText = [&](const QUuid& pointId, bool isEnd) -> QString {
        QStringList parts;
        for (const auto& att : m_paramDoc->attachments()) {
            if (att.isPin) continue;
            if (att.fromBlockId == m_blockId && att.fromPointId == pointId) {
                if (const auto* ldr = m_paramDoc->findBlock(att.toBlockId)) {
                    if (const auto* ls = ldr->findSegment(att.toSegmentId)) {
                        QString t = cad::param::Serial::tag(ls->serial);
                        if (!ls->name.isEmpty())
                            t += QStringLiteral("·") + ls->name;
                        parts << QString::fromUtf8("跟随 ") + t;
                    }
                }
                break;  // 每块至多一条跟随连接
            }
        }
        for (const auto& att : m_paramDoc->attachments()) {
            if (att.isPin) continue;
            if (att.toBlockId != m_blockId || att.toPointId != pointId) continue;
            if (const auto* fb = m_paramDoc->findBlock(att.fromBlockId)) {
                const QUuid fs = fb->exitSegmentAtPoint(att.fromPointId);
                if (const auto* fsg = fb->findSegment(fs)) {
                    QString t = cad::param::Serial::tag(fsg->serial);
                    if (!fsg->name.isEmpty())
                        t += QStringLiteral("·") + fsg->name;
                    parts << QString::fromUtf8("挂载 ") + t;
                }
            }
        }
        if (isEnd && !block->endTargetPointId.isNull()) {
            if (const auto* tb = m_paramDoc->findBlock(block->endTargetBlockId)) {
                const QUuid ts = tb->exitSegmentAtPoint(block->endTargetPointId);
                if (const auto* tsg = tb->findSegment(ts)) {
                    QString t = cad::param::Serial::tag(tsg->serial);
                    if (!tsg->name.isEmpty())
                        t += QStringLiteral("·") + tsg->name;
                    parts << QString::fromUtf8("指向 ") + t;
                }
            }
        }
        return parts.join(QStringLiteral("  "));
    };

    // 空摘要隐藏 (不占位灰块, 2026-08-31 重设计); 有内容才显示。
    // 2026-08 再拍板: 槽位绑定物理点 —— 摘要按槽内点归属 (跟随/挂载天然按点;
    // 「指向」显示在该线当前终点角色的那个点上)。
    const QUuid topId3 = fixedTopPointId(block, seg);
    const QUuid botId3 = topId3.isNull()
        ? QUuid()
        : (topId3 == seg->startPointId ? seg->endPointId : seg->startPointId);
    const QString topSummary = endpointText(topId3, /*isEnd=*/topId3 == seg->endPointId);
    const QString botSummary = endpointText(botId3, /*isEnd=*/botId3 == seg->endPointId);
    m_lblStartConn->setText(topSummary);   // 上组摘要 (= 先建物理点 P1)
    m_lblEndConn->setText(botSummary);     // 下组摘要 (= 后建物理点 P2)
    m_lblStartConn->setVisible(!topSummary.isEmpty());
    m_lblEndConn->setVisible(!botSummary.isEmpty());

    // 端点组内连接控件 (2026-08 再拍板): **按槽位物理点归属** —— 跟随连接
    // (Attachment, 引擎恒锚线的 start 端) 的「连接到+拆开/重连」显示在
    // 当前 start 端所在槽; 终点指向 (endTarget) 显示在当前 end 端所在槽。
    // 换向后 start/end 换了物理点 → 两组控件跟着角色槽位走 (摘要行仍按点)。
    if (m_refStartConnect) {
        const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
        const QSignalBlocker sb1(m_refStartConnect);
        if (att && m_topIsStart) {
            m_refStartConnect->setPoint(att->toBlockId, att->toPointId);
            m_btnStartDetach->setEnabled(true);
            m_btnStartDetach->setText(att->angleOnly
                ? QString::fromUtf8("重连") : QString::fromUtf8("拆开"));
        } else {
            m_refStartConnect->clearPoint();
            m_btnStartDetach->setEnabled(false);
            m_btnStartDetach->setText(QString::fromUtf8("拆开"));
        }
    }
    if (m_refEndConnect) {
        const QSignalBlocker sb2(m_refEndConnect);
        if (!block->endTargetPointId.isNull() && !m_topIsStart) {
            m_refEndConnect->setPoint(block->endTargetBlockId,
                                      block->endTargetPointId);
            m_btnEndDetach->setEnabled(true);
            m_btnEndDetach->setText(QString::fromUtf8("拆开"));
        } else {
            m_refEndConnect->clearPoint();
            m_btnEndDetach->setEnabled(false);
            m_btnEndDetach->setText(QString::fromUtf8("拆开"));
        }
    }
}

void LinePropertyDialog::refreshConnHint()
{
    if (!m_lblConnHint || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) { m_lblConnHint->clear(); return; }

    QString connHint;
    const cad::param::Attachment* att = nullptr;
    for (const auto& a : m_paramDoc->attachments()) {
        if (a.isPin) continue;
        if (a.fromBlockId == m_blockId) { att = &a; break; }
    }
    if (att) {
        // 连接分区状态提示 (已连接 L#·名; 仅角度/滑轨等子态由卡内 badge 细化)。
        connHint = QString::fromUtf8("已连接");
        if (const auto* leader = m_paramDoc->findBlock(att->toBlockId)) {
            if (const auto* lseg = leader->findSegment(att->toSegmentId)) {
                connHint += QStringLiteral(" ")
                    + cad::param::Serial::tag(lseg->serial);
                if (!lseg->name.isEmpty())
                    connHint += QStringLiteral("·") + lseg->name;
            }
        }
        // 连接子状态 (2026-xx 两维独立四态): 双拆开 = 自由; 独立角 =
        // 有连接线·无基准线; 仅角度 = 无连接线·有基准线。
        if (att->angleIndependent && att->angleOnly)
            connHint += QString::fromUtf8(" · 自由");
        else if (att->angleIndependent)
            connHint += QString::fromUtf8(" · 独立角");
        else if (att->angleOnly)
            connHint += QString::fromUtf8(" · 仅角度");
    }
    // 终点指向 (终点连接, 2026-xx 每端完整连接): 双端连接 = 桥接线 (起点
    // Attachment + 终点 endTarget); 仅终点指向 = 自由线带指向。
    if (!block->endTargetPointId.isNull()) {
        connHint = att ? QString::fromUtf8("桥接线")
                       : QString::fromUtf8("终点指向");
        if (const auto* tb = m_paramDoc->findBlock(block->endTargetBlockId)) {
            const QUuid ts = tb->exitSegmentAtPoint(block->endTargetPointId);
            if (const auto* tsg = tb->findSegment(ts)) {
                QString t = cad::param::Serial::tag(tsg->serial);
                if (!tsg->name.isEmpty())
                    t += QStringLiteral("·") + tsg->name;
                connHint += QStringLiteral(" ") + t;
            }
        }
    }
    if (connHint.isEmpty() && block->isDart())
        connHint = QString::fromUtf8("省道线");
    m_lblConnHint->setText(connHint);
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
    if (const cad::param::ParamPoint* topPt = block->findPoint(fixedTopPointId(block, seg))) {
        const QSignalBlocker b3(m_chkShowStartName);
        m_chkShowStartName->setChecked(topPt->showName || forceName);
    }
    const QUuid topId2 = fixedTopPointId(block, seg);
    const QUuid botId = topId2.isNull()
        ? QUuid()
        : (topId2 == seg->startPointId ? seg->endPointId : seg->startPointId);
    if (const cad::param::ParamPoint* botPt = block->findPoint(botId)) {
        const QSignalBlocker b4(m_chkShowEndName);
        m_chkShowEndName->setChecked(botPt->showName || forceName);
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
        m_editLength->setText(cad::geo::Units::formatNumberTrimmed(lenCm));
    }

    m_editLength->setEnabled(false);
    m_lblFx->setVisible(false);

    // Angle side of the bridge read-only treatment lives in the angle card.
    if (m_angleCard)
        m_angleCard->setBridgeReadOnly(true);
}

void LinePropertyDialog::applyToModel()
{
    if (!m_paramDoc) return;

    cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    cad::param::Segment* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    seg->name = m_editName->text().trimmed();
    // 便利贴已 live 写入, 此处幂等兜底.
    seg->annotation = m_noteSeg->note();
    // Keep the owned measure variable's name in sync (测量对象名称 → 测量变量
    // 名称): renaming a bridge/measure line renames its measurement variable.
    m_paramDoc->setOwnerMeasureName(m_blockId, seg->name);
    seg->role = static_cast<cad::param::SegmentRole>(m_cmbRole->currentIndex());
    seg->showName = m_chkShowName->isChecked();
    seg->showLength = m_chkShowLength->isChecked();
    seg->visible = m_chkVisible->isChecked();
    seg->color = m_currentColor;

    // Length: parse as cm number or treat as formula (formula result is in cm)
    const auto parsedLen = cad::geo::parseNumberOrFormula(m_editLength->text());
    if (parsedLen.isNumber) {
        double numMm = cad::geo::Units::cmToMm(parsedLen.value);
        seg->lengthFormula.clear();
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula.clear();
            ep->distance = numMm;
        }
    } else if (!parsedLen.formula.isEmpty()) {
        seg->lengthFormula = parsedLen.formula;
        if (auto* ep = block->findPoint(seg->endPointId)) {
            ep->distanceFormula = parsedLen.formula;
        }
    }

    // Point names/annotations —— 按槽位绑定写回**槽内物理点** (2026-08 拍板:
    // 上槽 = 先建点; 若它当前是终点角色也写给它, 不跟 start/end 角色走)。
    // 注释由便利贴 live 写入模型 (noteEdited), 这里再读一次只是幂等兜底。
    const QUuid topPt = fixedTopPointId(block, seg);
    const QUuid botPt = topPt.isNull()
        ? QUuid()
        : (topPt == seg->startPointId ? seg->endPointId : seg->startPointId);
    if (auto* p = block->findPoint(topPt)) {
        p->name = m_editStartName->text().trimmed();
        p->showName = m_chkShowStartName->isChecked();
        p->annotation = m_noteStart->note();
    }
    if (auto* p = block->findPoint(botPt)) {
        p->name = m_editEndName->text().trimmed();
        p->showName = m_chkShowEndName->isChecked();
        p->annotation = m_noteEnd->note();
    }

    const int styleId = m_styleGroup ? m_styleGroup->checkedId() : -1;
    if (styleId >= 0)
        seg->lineStyle = static_cast<cad::param::LineStyle>(styleId);
    seg->weight = m_spinWeight->value();

    // NOTE: The follower angle (跟随角度) is intentionally NOT written here.
    // It is owned by the follower's attachment and is applied exclusively by
    // SegmentAngleCard::applyAngle().
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
    // D6: 显示 = 实际长（本体 + 端点延长尾巴）。曲线不支持延长 → 直接本体弧长。
    const double lenMm = seg->isCurve()
        ? block->segmentBaseLength(seg->id)
        : block->segmentEffectiveLength(seg->id);
    m_lblActualLength->setText(lenMm > 0.0
        ? cad::geo::Units::formatLength(lenMm)
        : QStringLiteral("—"));
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

void LinePropertyDialog::onLengthModeChanged(bool autoMode)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
    const bool hasAtt = att != nullptr;
    const bool hasEnd = !block->endTargetPointId.isNull();
    // 双端连接 = 桥接线: 长度必须自动 (指定会超定), 不允许切换。
    if (hasAtt && hasEnd) {
        refreshLengthMode();
        return;
    }
    block->lengthAuto = autoMode;
    refreshLengthMode();
    refreshScene();
}

void LinePropertyDialog::refreshLengthMode()
{
    if (!m_btnLenAuto || !m_btnLenSpec || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;
    const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
    const bool hasAtt = att != nullptr;
    const bool hasEnd = !block->endTargetPointId.isNull();
    const bool bridge = hasAtt && hasEnd;
    const bool autoMode = bridge || block->lengthAuto;
    const QSignalBlocker b1(m_btnLenAuto);
    const QSignalBlocker b2(m_btnLenSpec);
    m_btnLenAuto->setChecked(autoMode);
    m_btnLenSpec->setChecked(!autoMode);
    // 双端连接时两个 chip 都禁用 (状态锁定为自动); 其余情况允许切换。
    m_btnLenAuto->setEnabled(!bridge);
    m_btnLenSpec->setEnabled(!bridge);
    if (m_editLength)
        m_editLength->setEnabled(!autoMode && !bridge);
    if (block->isDart()) {
        if (m_btnLenAuto) m_btnLenAuto->setEnabled(false);
        if (m_btnLenSpec) m_btnLenSpec->setEnabled(false);
        if (m_editLength) m_editLength->setEnabled(false);
    }
}

void LinePropertyDialog::onSlideModeChanged(int index)
{
    if (!m_paramDoc) return;
    const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
    if (!att || att->angleOnly || att->angleIndependent) {
        refreshSlideShadow();
        return;
    }
    const auto mode = static_cast<cad::param::SlideMode>(index);
    if (att->slideMode == mode) {
        refreshSlideShadow();
        return;
    }
    m_paramDoc->setAttachmentSlideMode(att->id, mode);
    refreshSlideShadow();
    refreshScene();
}

void LinePropertyDialog::onSlideOffsetEdited()
{
    if (!m_paramDoc) return;
    const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
    if (!att || att->angleOnly || att->angleIndependent) {
        refreshSlideShadow();
        return;
    }

    const QString aRaw = m_editSlideAlong->text().trimmed();
    const QString pRaw = m_editSlidePerp->text().trimmed();
    bool okA = false, okP = false;
    const double alongCm = aRaw.isEmpty() ? 0.0 : aRaw.toDouble(&okA);
    const double perpCm  = pRaw.isEmpty() ? 0.0 : pRaw.toDouble(&okP);
    const bool aFormula = !aRaw.isEmpty() && !okA;
    const bool pFormula = !pRaw.isEmpty() && !okP;
    const double along = cad::geo::Units::cmToMm(alongCm);
    const double perp  = cad::geo::Units::cmToMm(perpCm);

    const bool hasAlong = !aRaw.isEmpty();
    const bool hasPerp  = !pRaw.isEmpty();

    cad::param::SlideMode mode = cad::param::SlideMode::None;
    if (hasAlong || hasPerp) {
        if (hasAlong && !hasPerp)
            mode = cad::param::SlideMode::AlongLeader;
        else if (!hasAlong && hasPerp)
            mode = cad::param::SlideMode::PerpLeader;
        else
            mode = att->slideMode != cad::param::SlideMode::None
                ? att->slideMode : cad::param::SlideMode::AlongLeader;
    }

    // 2026-09 审核收口: 两轴数值/公式 + 派生模式一步推命令 —— 此前直改附件
    // 不进 undo, 会话外 Ctrl+Z 撤不掉 (与角度/连接编辑均推命令的约定一致)。
    const QString alongFormula = aFormula ? aRaw : QString();
    const QString perpFormula  = pFormula ? pRaw : QString();
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::SetAttachmentSlideOffsetsCommand(
            m_paramDoc, att->id, mode, along, alongFormula, perp, perpFormula));
    } else {
        auto* mut = m_paramDoc->findAttachment(att->id);
        if (mut) {
            mut->slideMode = mode;
            mut->slideAlongMm = along;
            mut->slideAlongFormula = alongFormula;
            mut->slidePerpMm = perp;
            mut->slidePerpFormula = perpFormula;
            m_paramDoc->resolveAll();
        }
    }
    refreshSlideShadow();
}

void LinePropertyDialog::onShadowEdited()
{
    if (!m_paramDoc) return;
    const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
    auto* block = m_paramDoc->findBlock(m_blockId);
    const bool hasEnd = block && !block->endTargetPointId.isNull();
    if (!att || att->angleIndependent || hasEnd) {
        refreshSlideShadow();
        return;
    }
    bool ok = false;
    const double deg = m_editShadow->text().trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(deg)) {
        refreshSlideShadow();
        return;
    }
    if (std::abs(deg - att->baselineOffsetDeg) < 1e-9) {
        refreshSlideShadow();
        return;
    }
    if (auto* stack = m_paramDoc->undoStack())
        stack->push(new cad::cmd::SetAttachmentBaselineOffsetCommand(
            m_paramDoc, att->id, deg));
    else {
        if (auto* mut = m_paramDoc->findAttachment(att->id))
            mut->baselineOffsetDeg = deg;
        m_paramDoc->resolveAll();
    }
    refreshSlideShadow();
    refreshScene();
}

void LinePropertyDialog::onStartConnectResolved(const QUuid& blockId,
                                                const QUuid& pointId)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    // 角色守卫 (2026-08 拍板): 跟随连接恒锚线的 start 端 —— 上槽点当前
    // 不是 start (换向后) 时, 这个槽不承载跟随连接, 输入无效回显。
    if (!m_topIsStart) { refreshEndpointConnRows(); return; }

    const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
    if (att) {
        // 已有连接 → 重定向。
        auto* mut = m_paramDoc->findAttachment(att->id);
        if (!mut) return;
        const auto* leader = m_paramDoc->findBlock(blockId);
        if (!leader || !leader->findPoint(pointId)) { refreshEndpointConnRows(); return; }
        mut->toBlockId = blockId;
        mut->toPointId = pointId;
        mut->toSegmentId = leader->exitSegmentAtPoint(pointId);
        if (auto* s = block->findSegment(m_segmentId)) {
            const double refWorld = leader->transform.rotation
                + leader->exitDirectionAtPoint(pointId, mut->toSegmentId);
            const double localDir = block->directionAtPoint(s->startPointId);
            mut->followerAngle = cad::param::backSolveFollowerAngle(
                block->transform.rotation, localDir, refWorld);
        }
        mut->followerAngleFormula.clear();
        mut->rotationMode = cad::param::RotationMode::Angle;
        mut->arcLength = 0.0;
        mut->arcLengthFormula.clear();
        m_paramDoc->resolveAll();
    } else {
        // 自由线 → 建立连接。
        const auto* leader = m_paramDoc->findBlock(blockId);
        if (!leader || !leader->findPoint(pointId)) { refreshEndpointConnRows(); return; }
        cad::param::Attachment attNew;
        attNew.fromBlockId = m_blockId;
        attNew.fromPointId = seg->startPointId;
        attNew.toBlockId = blockId;
        attNew.toPointId = pointId;
        attNew.toSegmentId = leader->exitSegmentAtPoint(pointId);
        const double refWorld = leader->transform.rotation
            + leader->exitDirectionAtPoint(pointId, attNew.toSegmentId);
        const double localDir = block->directionAtPoint(seg->startPointId);
        attNew.followerAngle = cad::param::backSolveFollowerAngle(
            block->transform.rotation, localDir, refWorld);
        m_paramDoc->addAttachment(attNew);
    }
    refreshEndpointConnRows();
    refreshConnHint();
    refreshScene();
}

void LinePropertyDialog::onStartDetachClicked()
{
    if (!m_paramDoc) return;
    const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
    if (!att) { refreshEndpointConnRows(); return; }
    // 角色守卫: 跟随连接控件只出现在 start 端所在槽 (上槽); 上槽为 end 时禁用。
    if (!m_topIsStart) { refreshEndpointConnRows(); return; }
    if (auto* stack = m_paramDoc->undoStack())
        stack->push(new cad::cmd::SetAttachmentAngleOnlyCommand(
            m_paramDoc, att->id, /*angleOnly=*/!att->angleOnly));
    else
        m_paramDoc->setAttachmentAngleOnly(att->id, !att->angleOnly);
    refreshEndpointConnRows();
    refreshConnHint();
    refreshScene();
}

void LinePropertyDialog::onEndConnectResolved(const QUuid& blockId,
                                              const QUuid& pointId)
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block) return;

    // 角色守卫 (2026-08 拍板): 终点指向恒锚当前 end 端 —— 下槽 (end 端所在
    // 槽) 承载指向; 下槽不是 end (换向后) 时输入无效回显。
    if (m_topIsStart) { refreshEndpointConnRows(); return; }
    auto* seg = block->findSegment(m_segmentId);
    if (!seg) return;

    const auto* targetBlk = m_paramDoc->findBlock(blockId);
    if (!targetBlk || !targetBlk->findPoint(pointId)) { refreshEndpointConnRows(); return; }
    if (blockId == m_blockId) { refreshEndpointConnRows(); return; }

    const bool bridgeLand = block->lengthAuto;
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::ConnectEndCommand(
            m_paramDoc, m_blockId, m_segmentId, blockId, pointId,
            block->endTargetPointId.isNull() ? 0.0 : block->endTargetOffset,
            bridgeLand));
    } else {
        block->endTargetBlockId = blockId;
        block->endTargetPointId = pointId;
        block->endTargetOffset = 0.0;
        block->endTargetOffsetFormula.clear();
        m_paramDoc->resolveAll();
    }
    refreshEndpointConnRows();
    refreshConnHint();
    refreshScene();
}

void LinePropertyDialog::onEndDetachClicked()
{
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    if (!block || block->endTargetPointId.isNull()) { refreshEndpointConnRows(); return; }
    // 角色守卫: 指向控件只出现在 end 端所在槽 (下槽); 上槽为 end 时禁用。
    if (m_topIsStart) { refreshEndpointConnRows(); return; }
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::SetEndTargetCommand(
            m_paramDoc, m_blockId, QUuid(), QUuid(), 0.0));
    } else {
        block->endTargetBlockId = QUuid();
        block->endTargetPointId = QUuid();
        block->endTargetOffset = 0.0;
        block->endTargetOffsetFormula.clear();
        m_paramDoc->resolveAll();
    }
    refreshEndpointConnRows();
    refreshConnHint();
    refreshScene();
}

void LinePropertyDialog::refreshSlideShadow()
{
    if (!m_slideRow || !m_shadowRow || !m_paramDoc) return;
    const auto* block = m_paramDoc->findBlock(m_blockId);
    const auto* att = findFollowerAttachmentFor(m_paramDoc, m_blockId);
    const bool hasAtt = att != nullptr;
    const bool hasEnd = block && !block->endTargetPointId.isNull();
    const bool bridge = hasAtt && hasEnd;

    // 省道线: 滑轨/影子偏转移出面板 (2026-xx §3)。
    if (block && block->isDart()) {
        m_slideRow->setVisible(false);
        m_shadowRow->setVisible(false);
        return;
    }

    // 滑轨行 (恒显; 无连接/拆开/独立角/终点指向态禁用)。
    m_slideRow->setVisible(true);
    {
        const QSignalBlocker cb(m_cmbSlideMode);
        const QSignalBlocker sb1(m_editSlideAlong);
        const QSignalBlocker sb2(m_editSlidePerp);
        const bool slideOk = hasAtt && !att->angleOnly && !att->angleIndependent
            && !hasEnd;
        m_cmbSlideMode->setCurrentIndex(hasAtt ? static_cast<int>(att->slideMode) : 0);
        m_cmbSlideMode->setEnabled(slideOk);
        m_editSlideAlong->setEnabled(slideOk);
        m_editSlidePerp->setEnabled(slideOk);
        const bool hasSlide = hasAtt
            && att->slideMode != cad::param::SlideMode::None;
        m_editSlideAlong->setText(hasSlide
            ? (!att->slideAlongFormula.isEmpty()
                   ? att->slideAlongFormula
                   : cad::geo::Units::formatCmTrimmed(att->slideAlongMm))
            : QString());
        m_editSlidePerp->setText(hasSlide
            ? (!att->slidePerpFormula.isEmpty()
                   ? att->slidePerpFormula
                   : cad::geo::Units::formatCmTrimmed(att->slidePerpMm))
            : QString());
        const QString slideTip = bridge
            ? QString::fromUtf8("桥接线双端锚定：旋转由终点指向驱动，滑轨无意义。")
            : (!hasAtt
                ? QString::fromUtf8("连接后可设：沿基准线单向滑动（角度跟随始终保留）。")
                : (!slideOk
                    ? QString::fromUtf8("滑轨需要完整连接（位置吸附 + 角度跟随）：先「重连」恢复两个维度。")
                    : QString::fromUtf8("滑轨模式（抽屉式单向滑动）：全连接 = 位置吸附 + 角度跟随（默认）；"
                                        "沿线滑动 = 仅沿基准线方向可滑；垂直拉出 = 仅垂直基准线可拉。"
                                        "进入滑轨后拖动跟随线只沿对应方向动，角度跟随始终保留。与「拆开」互斥。")));
        if (m_cmbSlideMode->toolTip() != slideTip)
            m_cmbSlideMode->setToolTip(slideTip);
    }
    m_lblSlideBadge->setVisible(false);

    // 影子偏转行 (常显可输入; 无连接/独立角/终点指向态禁用)。
    {
        const bool shadowActive = hasAtt && !att->angleIndependent && !hasEnd;
        m_editShadow->setEnabled(shadowActive);
        const QSignalBlocker eb(m_editShadow);
        m_editShadow->setText(shadowActive
            ? cad::geo::Units::formatDegTrimmed(att->baselineOffsetDeg)
            : QString());
    }
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
        "background-color: %1; border:1px solid %2; border-radius:2px;")
        .arg(chosen.name(), cad::ui::Theme::tokens().text3.name()));
    if (m_lblColorHex)
        m_lblColorHex->setText(chosen.name().toUpper());

    onLiveUpdate();
}

void LinePropertyDialog::updateWeightControls()
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

    // 分段选中态: 匹配预设则选中, 否则全不选 (= 自定义, 数值见 spin)。
    const double presets[3] = {kWeightThin, kWeightMedium, kWeightThick};
    int match = -1;
    for (int i = 0; i < 3; ++i)
        if (std::abs(w - presets[i]) < 0.05) { match = i; break; }
    for (int i = 0; i < 3; ++i)
        if (m_weightBtns[i]) m_weightBtns[i]->setChecked(i == match);
}

void LinePropertyDialog::reject()
{
    // Esc / window-X close must behave like the "撤销全部" button: revert the
    // live-applied edits (QDialog's default reject() only hides the dialog).
    onRejected();
}

void LinePropertyDialog::keyPressEvent(QKeyEvent* event)
{
    // 回车 = 提交当前输入框 (ElaLineEdit::editingFinished 已触发), 不关闭对话框
    // —— ElaLineEdit 的回车事件会传播到 QDialog 的 default button/accept 路径, 使
    // 在任意输入框 (长度/角度/延长/滑轨) 按回车都会把整个属性对话框悄悄关掉
    // (用户 2026-12: 滑轨输入回车后"没有生效的痕迹"——对话框已关, 输入像被丢弃)。
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        event->accept();
        return;
    }
    // Esc 等其余按键照旧走 QDialog (Esc = 撤销全部, reject() 已被重载)。
    ElaDialog::keyPressEvent(event);
}

void LinePropertyDialog::onAccepted()
{
    applyToModel();
    refreshScene();

    // P0-3: 会话收口 —— 此前 live-apply 直写模型完全绕过 undo (Ctrl+Z 撤不掉,
    // 且 undo 栈的 clean 标记不感知)。按 SegmentEditBarCommand 同款模式: 会话
    // 确认时把「打开快照 → 确认状态」推成一步命令。old = 打开时快照 (取消时
    // 恢复的同一份), new = 确认时模型状态 (applyToModel 已写入)。无差异则
    // 不 push, 避免空命令污染撤销链。
    if (m_paramDoc && !m_isCreation) {
        cad::cmd::SetLinePropertiesCommand::Props oldProps;
        oldProps.name = m_snapshot.segName;
        oldProps.annotation = m_snapshot.segAnnotation;        oldProps.role = static_cast<cad::param::SegmentRole>(m_snapshot.role);
        oldProps.showName = m_snapshot.showName;
        oldProps.showLength = m_snapshot.showLength;
        oldProps.visible = m_snapshot.visible;
        oldProps.color = m_snapshot.color;
        oldProps.lineStyle = static_cast<cad::param::LineStyle>(m_snapshot.lineStyle);
        oldProps.weight = m_snapshot.weight;
        oldProps.lengthAuto = m_snapshot.lengthAuto;
        oldProps.lengthFormula = m_snapshot.lengthFormula;
        oldProps.distance = m_snapshot.distance;
        oldProps.distanceFormula = m_snapshot.distanceFormula;
        oldProps.startName = m_snapshot.startName;
        oldProps.startAnno = m_snapshot.startAnno;
        oldProps.startShowName = m_snapshot.startShowName;
        oldProps.endName = m_snapshot.endName;
        oldProps.endAnno = m_snapshot.endAnno;
        oldProps.endShowName = m_snapshot.endShowName;

        cad::cmd::SetLinePropertiesCommand::Props newProps;
        if (auto* b = m_paramDoc->findBlock(m_blockId)) {
            if (auto* s = b->findSegment(m_segmentId)) {
                newProps.name = s->name;
                newProps.annotation = s->annotation;                newProps.role = s->role;
                newProps.showName = s->showName;
                newProps.showLength = s->showLength;
                newProps.visible = s->visible;
                newProps.color = s->color;
                newProps.lineStyle = s->lineStyle;
                newProps.weight = s->weight;
                newProps.lengthAuto = b->lengthAuto;
                newProps.lengthFormula = s->lengthFormula;
                if (const auto* ep = b->findPoint(s->endPointId)) {
                    newProps.distance = ep->distance;
                    newProps.distanceFormula = ep->distanceFormula;
                    newProps.endName = ep->name;
                    newProps.endShowName = ep->showName;
                    newProps.endAnno = ep->annotation;
                }
                if (const auto* sp = b->findPoint(s->startPointId)) {
                    newProps.startName = sp->name;
                    newProps.startShowName = sp->showName;
                    newProps.startAnno = sp->annotation;
                }
            }
        }
        if (oldProps != newProps)
            m_paramDoc->undoStack()->push(new cad::cmd::SetLinePropertiesCommand(
                m_paramDoc, m_blockId, m_segmentId, oldProps, newProps));
    }

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
                seg->annotation = m_snapshot.segAnnotation;                seg->showName = m_snapshot.showName;
                seg->showLength = m_snapshot.showLength;
                seg->visible = m_snapshot.visible;
                seg->role = static_cast<cad::param::SegmentRole>(m_snapshot.role);
                seg->lengthFormula = m_snapshot.lengthFormula;
                seg->lineStyle = static_cast<cad::param::LineStyle>(m_snapshot.lineStyle);
                seg->weight = m_snapshot.weight;
                seg->color = m_snapshot.color;
                seg->tension = m_snapshot.tension;
                block->lengthAuto = m_snapshot.lengthAuto;   // 2026-09 审核收口
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
    if (m_connCard)  m_connCard->setTarget(blockId, segmentId);
    if (m_refCard)   m_refCard->setTarget(blockId, segmentId);
    if (m_angleCard) m_angleCard->setTarget(blockId, segmentId);
    populateFromModel();
    applyCanvasHighlight();
    updateWindowTitle();
}

void LinePropertyDialog::updateWindowTitle()
{
    if (!m_paramDoc) return;
    const auto* b = m_paramDoc->findBlock(m_blockId);
    if (!b) return;
    const auto* s = b->findSegment(m_segmentId);
    if (!s) return;
    setWindowTitle(QString::fromUtf8("\u7ebf\u6761\u5c5e\u6027 - %1")  // 线条属性 - %1
        .arg(s->name.isEmpty()
                 ? cad::param::Serial::tag(s->serial)
                 : s->name));
}

void LinePropertyDialog::onDebounceTimeout()
{
    onLengthApply();
    if (m_angleCard) m_angleCard->applyAngle();
    if (m_auxTab)
        m_auxTab->onLiveUpdate();
}

// ─── 便利贴注释 (NoteButton, 2026-12) ──────────────────────────────────────
// 三处注释 (段 / 起点 / 终点) 都只 live 写模型, 不自行 push 命令 —— 本对话框
// 是会话制: onAccepted 会把「打开时快照 → 确认时状态」推成一条
// SetLinePropertiesCommand, 注释随之进入撤销链 (一步撤销, 与别的属性一致)。
// 注释纯备忘、不参与求解, 故不 touchGeometry()。
void LinePropertyDialog::onSegNoteEdited(const QString& text)
{
    if (!m_paramDoc) return;
    if (auto* b = m_paramDoc->findBlock(m_blockId))
        if (auto* s = b->findSegment(m_segmentId))
            s->annotation = text;
}

void LinePropertyDialog::onStartNoteEdited(const QString& text)
{
    // 上槽便利贴恒写给**上槽物理点** (先建点, 2026-08 拍板 —— 与角色无关)。
    if (!m_paramDoc) return;
    if (auto* b = m_paramDoc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId))
            if (auto* p = b->findPoint(fixedTopPointId(b, s)))
                p->annotation = text;
    }
}

void LinePropertyDialog::onStartExtendEdited()
{
    applyEndpointExtend(m_editStartExtend, true);
}

void LinePropertyDialog::onEndExtendEdited()
{
    applyEndpointExtend(m_editEndExtend, false);
}

void LinePropertyDialog::applyEndpointExtend(ElaLineEdit* edit, bool isTop)
{
    // isTop = 编辑的是哪个**槽位** (上槽 = 先建物理点 P1)。写入目标 = 槽内点
    // 的**当前角色** (start → extendStart / end → extendEnd) —— 物理尾巴跟点
    // 走, 换向后槽位不动、写的角色字段随 m_topIsStart 翻转。
    if (!m_paramDoc || !edit) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    const QUuid topId = fixedTopPointId(block, seg);
    const bool slotIsStart = isTop ? (topId == seg->startPointId)
                                   : (topId != seg->startPointId);

    const auto parsed = cad::geo::parseNumberOrFormula(edit->text());
    const bool isNumber = parsed.isNumber;
    const double num = parsed.value;
    if (isNumber && num < 0.0) {
        if (m_scene)
            m_scene->showToast(QString::fromUtf8("延长量不能为负数（只允许往外延长）"));
        refreshEndpointExtends();
        return;
    }

    cad::cmd::SetSegmentExtendCommand::Values v;
    v.startMm = seg->extendStartMm;
    v.startFormula = seg->extendStartFormula;
    v.endMm = seg->extendEndMm;
    v.endFormula = seg->extendEndFormula;
    if (slotIsStart) {
        if (parsed.formula.isEmpty()) { v.startMm = 0.0; v.startFormula.clear(); }
        else if (isNumber) { v.startMm = cad::geo::Units::cmToMm(num); v.startFormula.clear(); }
        else v.startFormula = parsed.formula;
    } else {
        if (parsed.formula.isEmpty()) { v.endMm = 0.0; v.endFormula.clear(); }
        else if (isNumber) { v.endMm = cad::geo::Units::cmToMm(num); v.endFormula.clear(); }
        else v.endFormula = parsed.formula;
    }

    m_paramDoc->undoStack()->push(new cad::cmd::SetSegmentExtendCommand(
        m_paramDoc, m_blockId, m_segmentId, v));
    refreshScene();
    refreshEndpointExtends();
}

void LinePropertyDialog::refreshEndpointExtends()
{
    if (!m_editStartExtend || !m_editEndExtend) return;
    if (!m_paramDoc) return;
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) return;

    // 整卡置灰 (曲线/桥/省道) + 单端置灰 (粘死端/同块角点/组件暴露端点)。
    QString cardReason;
    if (seg->isCurve())
        cardReason = QString::fromUtf8("曲线暂不支持延长");
    else if (block->isBridge)
        cardReason = QString::fromUtf8("桥接线两端均已钉住，不支持延长");
    else if (block->isDart())
        cardReason = QString::fromUtf8("省道线为计算线，不支持延长");

    const bool wholeGray = !cardReason.isEmpty();
    // 按槽位绑定的物理点取值与置灰 (2026-08 拍板): 槽内点当前是 start →
    // 读 extendStart; 是 end → 读 extendEnd (物理尾巴跟点走)。
    const QUuid topId = fixedTopPointId(block, seg);
    const QUuid botId = topId.isNull()
        ? QUuid()
        : (topId == seg->startPointId ? seg->endPointId : seg->startPointId);
    const bool topIsStart = (topId == seg->startPointId);
    const QString topReason = wholeGray ? cardReason
        : endpointExtendDisableReason(*block, *seg, topId);
    const QString botReason = wholeGray ? cardReason
        : endpointExtendDisableReason(*block, *seg, botId);

    const QSignalBlocker b1(m_editStartExtend);
    const QSignalBlocker b2(m_editEndExtend);
    const auto extendText = [seg](bool roleIsStart) {
        const QString& f = roleIsStart ? seg->extendStartFormula
                                       : seg->extendEndFormula;
        const double mm = roleIsStart ? seg->extendStartMm : seg->extendEndMm;
        return !f.isEmpty() ? f
             : (mm > 0.0 ? cad::geo::Units::formatCmTrimmed(mm) : QString());
    };
    m_editStartExtend->setText(extendText(topIsStart));
    m_editEndExtend->setText(extendText(!topIsStart));
    m_editStartExtend->setEnabled(topReason.isEmpty());
    m_editEndExtend->setEnabled(botReason.isEmpty());
    m_editStartExtend->setToolTip(topReason.isEmpty()
        ? QString::fromUtf8("数值或公式 cm（0 = 不延长）") : topReason);
    m_editEndExtend->setToolTip(botReason.isEmpty()
        ? QString::fromUtf8("数值或公式 cm（0 = 不延长）") : botReason);
}

QString LinePropertyDialog::endpointExtendDisableReason(const cad::param::Block& block,
                                                        const cad::param::Segment& seg,
                                                        const QUuid& pointId) const
{
    if (pointId.isNull()) return QString::fromUtf8("端点缺失");

    // 粘死端: 本线自身作为跟随线的吸附点不允许延长。
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.fromBlockId == block.id && !att.isPin
            && att.fromPointId == pointId) {
            return QString::fromUtf8("该端已粘在基准线上（跟随连接端），不允许延长");
        }
    }

    // 同块角点: 该端同时是同一 Block 其他线段的端点。
    int incidence = 0;
    for (const auto& s : block.segments)
        if (s.startPointId == pointId || s.endPointId == pointId)
            ++incidence;
    if (incidence > 1)
        return QString::fromUtf8("该端为折线/闭合轮廓的共用角点，暂不支持延长");

    // 组件暴露端点。
    const auto* comp = m_paramDoc->componentsView().ofBlock(block.id);
    if (comp && comp->exposedPointId == pointId)
        return QString::fromUtf8("该端为组件的暴露端点，暂不支持延长");

    return QString();
}

void LinePropertyDialog::onEndNoteEdited(const QString& text)
{
    if (!m_paramDoc) return;
    // 下槽便利贴恒写给**下槽物理点** (后建点, 2026-08 拍板 —— 与角色无关)。
    if (auto* b = m_paramDoc->findBlock(m_blockId)) {
        if (auto* s = b->findSegment(m_segmentId)) {
            const QUuid topId = fixedTopPointId(b, s);
            const QUuid botId = topId.isNull()
                ? QUuid()
                : (topId == s->startPointId ? s->endPointId : s->startPointId);
            if (auto* p = b->findPoint(botId))
                p->annotation = text;
        }
    }
}

void LinePropertyDialog::onDirectionArrowClicked()
{
    if (!m_paramDoc) return;
    QString why;
    if (!cad::cmd::ReverseSegmentCommand::canReverse(m_paramDoc, m_blockId,
                                                     m_segmentId, &why)) {
        if (!why.isEmpty() && m_btnDirectionArrow)
            m_btnDirectionArrow->setToolTip(why);
        return;
    }
    if (auto* stack = m_paramDoc->undoStack()) {
        stack->push(new cad::cmd::ReverseSegmentCommand(m_paramDoc, m_blockId,
                                                        m_segmentId));
    } else {
        cad::cmd::ReverseSegmentCommand cmd(m_paramDoc, m_blockId, m_segmentId);
        cmd.redo();
    }
    refreshScene();
    populateFromModel();
    refreshDirectionArrow();
}

void LinePropertyDialog::refreshDirectionArrow()
{
    if (!m_btnDirectionArrow) return;
    if (!m_paramDoc) { m_btnDirectionArrow->setVisible(false); return; }
    auto* block = m_paramDoc->findBlock(m_blockId);
    auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
    if (!block || !seg) { m_btnDirectionArrow->setVisible(false); return; }

    // 换向资格 (与 SegmentAngleCard 同口径): 桥/省道/指向/滑轨等不合格时隐藏.
    QString why;
    const bool ok = !block->isBridge && !block->isDart() &&
                    block->endTargetPointId.isNull() &&
                    cad::cmd::ReverseSegmentCommand::canReverse(m_paramDoc, m_blockId,
                                                                m_segmentId, &why);
    m_btnDirectionArrow->setVisible(ok);
    if (!ok) return;

    // 箭头 = **进出指示器** (2026-08 用户拍板, 取代旧"start→end 世界朝向"
    // 画布投影): 端点槽位已绑定物理点 (P1 恒上), 箭头表达"谁是进 (start)、
    // 谁是出" —— ↓ = 上进下出 (上槽点 = 当前起点), ↑ = 下进上出。点击箭头
    // = 换向 (进出互换) → 下一帧 refresh 时 m_topIsStart 翻转, 箭头翻面 ——
    // 控件布局与槽内点永不变化。
    m_btnDirectionArrow->setText(m_topIsStart ? QString::fromUtf8("↓")
                                              : QString::fromUtf8("↑"));
    m_btnDirectionArrow->setToolTip(ok
        ? QString::fromUtf8("↓ = 上进下出；↑ = 下进上出。点击调换进/出（点固定，几何不变）")
        : why);
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

void LinePropertyDialog::onRefCardChanged()
{
    // 引用线段/指向点突变: 场景刷新 + 角度卡灰态重算 (指向生效 → 角度灰只读)。
    refreshScene();
    refreshEndpointConnRows();      // 对齐点/终点指向摘要可能变化 ("连接到"行同步)
    refreshConnHint();              // 终点指向态 badge
    if (m_angleCard) m_angleCard->refresh();
    if (m_connCard)  m_connCard->refresh();
    refreshLengthMode();
    refreshSlideShadow();
}

void LinePropertyDialog::onConnCardChanged()
{
    // Mirror of the pre-extraction slot effects: every connection/angle
    // mutation needs a scene refresh.
    refreshScene();
    refreshEndpointConnRows();      // 跟随/挂载摘要 (方案 A)
    refreshConnHint();              // 桥接线/已连接 badge
    if (m_refCard) m_refCard->refresh();   // 终点指向态 → 基准线行隐藏 (互斥)
    refreshLengthMode();
    refreshSlideShadow();
}

} // namespace cad::ui