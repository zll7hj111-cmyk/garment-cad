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
#include "parametric/FollowerAngle.h"
#include "document/commands/VariableCommands.h"
#include "document/commands/BlockCommands.h"  // SetLinePropertiesCommand (P0-3)
#include "ui/PointRefEdit.h"
#include "ui/SegmentAnchorTab.h"
#include "ui/SegmentAimCard.h"
#include "ui/SegmentConnectionCard.h"
#include "ui/SegmentExtendCard.h"
#include "ui/SegmentAuxTab.h"

namespace cad::ui {

namespace {

/// Weight presets: 细 / 中 / 粗 (分段按钮; 无选中 = 自定义, 数值见 spin)。
constexpr double kWeightThin   = 0.8;
constexpr double kWeightMedium = 1.2;
constexpr double kWeightThick  = 2.0;

/// 标签列宽 (短词 12px, 全页统一)。
constexpr int kLabelW = 64;
/// 行内控件统一高度 (全页规范: 35px)。
constexpr int kFieldH = 35;

/// 分区标题: [3px 信号黄竖条][12px 600 标题] … [可选右侧控件/提示]。
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
    auto* t = new ElaText(title, 12, w);
    t->setStyleSheet(QStringLiteral("font-weight:600;"));
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

/// Toggle chip / 分段按钮的实例 QSS (theme-token 驱动, 明暗双模):
/// 未选 = 透明底 + 细边 + 次级文字; 选中 = 信号黄淡洗 + 碳黑边 + 正文色。
QString chipStyle()
{
    const auto& t = cad::ui::Theme::tokens();
    return QStringLiteral(
        "QPushButton { border:1px solid %1; border-radius:2px;"
        " background:transparent; color:%2; padding:3px 10px; font-size:12px; }"
        "QPushButton:hover { background:%3; }"
        "QPushButton:checked { background:%4; border-color:%5; color:%6; }"
        "QPushButton:disabled { color:%7; }")
        .arg(t.border.name(), t.text2.name(), t.surface2.name(),
             t.accentTint.name(), t.borderStrong.name(), t.text1.name(),
             t.text3.name());
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
    // ElaTabBarStyle 默认标签宽 220×35 (4 枚 = 880px) — 超出 ~540 宽单列
    // 面板后 tabRect 溢出 bar 范围 (点击/命中判定落到空处)。收窄到 116:
    // 4 枚 ≈ 464px, 适配单列 inspector 布局。
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

    // ── 连接线段 card: topology re-sync (aux list + scene) ──
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
    // ── 换向: 端点身份互换 → 全量重填 (端点微卡标签/名称回填) + 刷画布 ──
    connect(m_angleCard, &SegmentAngleCard::reversed,
            this, [this] { refreshScene(); populateFromModel(); });
    // ── 延长 card (端点延长线): 变更即刷新画布 (模型已 resolveAll) ──
    connect(m_extendCard, &SegmentExtendCard::changed,
            this, [this] { refreshScene(); });
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

    const QString chips = chipStyle();
    const QString dimMono = cad::ui::Theme::dimValueStyle()
        + QString::fromLatin1(cad::ui::ThemeTokens::kMonospaceFamily);

    // ─── 基本 ───
    layout->addWidget(makeSectionHeader(QString::fromUtf8("基本"), page));
    m_editName = new ElaLineEdit(page);
    m_editName->setPlaceholderText(QString::fromUtf8("名称，如“肩线”“侧缝”"));
    layout->addWidget(m_editName);
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblRole = new ElaText(QString::fromUtf8("类型"), 12, page);
        lblRole->setFixedWidth(kLabelW);
        row->addWidget(lblRole);
        m_cmbRole = new ElaComboBox(page);
        m_cmbRole->setFixedWidth(150);
        m_cmbRole->addItem(QString::fromUtf8("轮廓线"));
        m_cmbRole->addItem(QString::fromUtf8("内部线"));
        m_cmbRole->addItem(QString::fromUtf8("辅助线"));
        row->addWidget(m_cmbRole);
        // 完整 ID (随机前缀 + 类型序号): 紧挨类型下拉框右侧, 与输入列对齐
        // (用户 2026-12: "L1 改为显示完整 ID, 移过去对齐; 关键名称标红")。
        // 前缀灰色 + 关键 "L#" 红色加粗 (Serial::toHtml, 与 SerialDelegate 同约定)。
        m_lblSegId = new ElaText(QString(), 12, page);
        m_lblSegId->setStyleSheet(dimMono);
        m_lblSegId->setToolTip(QString::fromUtf8("线段完整编号（全局唯一，随机前缀+类型序号）"));
        row->addWidget(m_lblSegId);
        row->addStretch();
        layout->addLayout(row);
    }
    {
        // 发布长度参数: 放到类型下拉框正下方, 与下拉框左缘/同宽对齐
        // (用户 2026-12 拍板: "发布参数放到轮廓下拉框下面, 对齐这样好看")。
        auto* pubRow = new QHBoxLayout();
        pubRow->setSpacing(6);
        pubRow->addSpacing(kLabelW + 6);
        m_btnPublishLen = new ElaPushButton(QString::fromUtf8("发布长度参数"), page);
        m_btnPublishLen->setFixedSize(150, kFieldH);
        m_btnPublishLen->setToolTip(QString::fromUtf8(
            "将本线段的长度发布为关联参数（只读），其他公式可直接引用其引用名（L+编号）"));
        m_btnPublishLen->setCursor(Qt::PointingHandCursor);
        connect(m_btnPublishLen, &QPushButton::clicked,
                this, &LinePropertyDialog::onPublishLength);
        pubRow->addWidget(m_btnPublishLen);
        pubRow->addStretch();
        layout->addLayout(pubRow);
    }
    layout->addWidget(makeDivider(page));

    // ─── 几何 (发布按钮已移至基本区类型行下方) ───
    layout->addWidget(makeSectionHeader(QString::fromUtf8("几何"), page));
    {
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblLen = new ElaText(QString::fromUtf8("长度"), 12, page);
        lblLen->setFixedWidth(kLabelW);
        row->addWidget(lblLen);
        m_lblFx = new ElaText(
            QStringLiteral("<i style='color:%1;'>fx</i>")
                .arg(cad::ui::Theme::tokens().text2.name()),
            12, page);
        m_lblFx->setVisible(false);
        m_lblFx->setFixedWidth(18);
        row->addWidget(m_lblFx);
        m_editLength = new ElaLineEdit(page);
        m_editLength->setFixedWidth(150);
        m_editLength->setPlaceholderText(cad::ui::kPlaceholderCmOrFormula);
        row->addWidget(m_editLength);
        auto* btnPasteLen = new ElaPushButton(QStringLiteral("填入"), page);
        btnPasteLen->setFixedSize(48, kFieldH);
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
        m_lblActualLength = new ElaText(QString(), 12, page);
        m_lblActualLength->setStyleSheet(dimMono);
        m_lblActualLength->setToolTip(QString::fromUtf8("当前实际长度（只读）"));
        row->addWidget(m_lblActualLength);
        row->addStretch();
        layout->addLayout(row);
    }
    // 角度行组 (SegmentAngleCard): 长度之下, 长度+方向同属几何。
    m_angleCard = new SegmentAngleCard(m_paramDoc, m_scene, page);
    layout->addWidget(m_angleCard);
    // 曲线专有: 弧长 (只读) / 张力 (+转为直线)。
    m_arcRow = new QWidget(page);
    {
        auto* arcLayout = new QHBoxLayout(m_arcRow);
        arcLayout->setContentsMargins(0, 0, 0, 0);
        arcLayout->setSpacing(6);
        auto* lblArc = new ElaText(QString::fromUtf8("弧长"), 12, m_arcRow);
        lblArc->setFixedWidth(kLabelW);
        arcLayout->addWidget(lblArc);
        m_lblArcLength = new ElaText(QStringLiteral("—"), 12, m_arcRow);
        m_lblArcLength->setStyleSheet(
            QStringLiteral("font-weight:600;")
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
        auto* lblTension = new ElaText(QString::fromUtf8("张力"), 12, m_tensionRow);
        lblTension->setFixedWidth(kLabelW);
        tensionLayout->addWidget(lblTension);
        m_editTension = new ElaLineEdit(m_tensionRow);
        m_editTension->setFixedWidth(80);
        m_editTension->setPlaceholderText(QStringLiteral("0"));
        m_editTension->setToolTip(QString::fromUtf8("0=平滑(Catmull-Rom)  >0更紧  <0更松"));
        tensionLayout->addWidget(m_editTension);
        tensionLayout->addStretch();
        // 转换按钮仅对曲线显示（一键转回直线）。直线→曲线请用智能笔添加曲线点。
        m_btnConvert = new ElaPushButton(QString::fromUtf8("转为直线"), m_tensionRow);
        m_btnConvert->setFixedHeight(kFieldH);
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
    // 延长行组 (端点延长线, 几何的叠加参数)。
    m_extendCard = new SegmentExtendCard(m_paramDoc, m_scene, page);
    m_extendCard->setObjectName(QStringLiteral("extendCard"));
    m_extendCard->setTarget(m_blockId, m_segmentId);
    layout->addWidget(m_extendCard);
    layout->addWidget(makeDivider(page));

    // ─── 连接 (拓扑 + 引用, 行组堆叠; 状态提示在分区标题右侧) ───
    m_lblConnHint = new ElaText(QString(), 12, page);
    m_lblConnHint->setStyleSheet(dimMono);
    layout->addWidget(makeSectionHeader(QString::fromUtf8("连接"), page,
                                        m_lblConnHint));
    m_connCard = new SegmentConnectionCard(m_paramDoc, m_scene, page);
    m_connCard->setTarget(m_blockId, m_segmentId);
    layout->addWidget(m_connCard);
    m_refCard = new SegmentRefCard(m_paramDoc, m_scene, page);
    m_refCard->setTarget(m_blockId, m_segmentId);
    layout->addWidget(m_refCard);
    layout->addWidget(makeDivider(page));

    // ─── 端点 (起/终双列微卡, objectName 是测试查找契约) ───
    layout->addWidget(makeSectionHeader(QString::fromUtf8("端点"), page));
    {
        const auto& t = cad::ui::Theme::tokens();
        const QString panelQss = QStringLiteral(
            "QFrame { background:%1; border:1px solid %2; border-radius:2px; }")
            .arg(t.surface3.name(), t.border.name());
        auto* row = new QHBoxLayout();
        row->setSpacing(8);

        auto* startPanel = new QFrame(page);
        startPanel->setObjectName(QStringLiteral("startPointCard"));
        startPanel->setStyleSheet(panelQss);
        {
            auto* v = new QVBoxLayout(startPanel);
            v->setContentsMargins(8, 8, 8, 8);
            v->setSpacing(6);
            m_lblStartPtId = new ElaText(QString(), 12, startPanel);
            m_lblStartPtId->setStyleSheet(dimMono);
            v->addWidget(m_lblStartPtId);
            m_editStartName = new ElaLineEdit(startPanel);
            m_editStartName->setPlaceholderText(QString::fromUtf8("名称，如“肩点”"));
            v->addWidget(m_editStartName);
            m_editStartAnno = new ElaLineEdit(startPanel);
            m_editStartAnno->setPlaceholderText(QString::fromUtf8("备注"));
            v->addWidget(m_editStartAnno);
            m_chkShowStartName = new QPushButton(QString::fromUtf8("显示名称"), startPanel);
            m_chkShowStartName->setCheckable(true);
            m_chkShowStartName->setFixedHeight(24);
            m_chkShowStartName->setStyleSheet(chips);
            m_chkShowStartName->setCursor(Qt::PointingHandCursor);
            v->addWidget(m_chkShowStartName, 0, Qt::AlignLeft);
        }
        row->addWidget(startPanel, 1);

        auto* endPanel = new QFrame(page);
        endPanel->setObjectName(QStringLiteral("endPointCard"));
        endPanel->setStyleSheet(panelQss);
        {
            auto* v = new QVBoxLayout(endPanel);
            v->setContentsMargins(8, 8, 8, 8);
            v->setSpacing(6);
            m_lblEndPtId = new ElaText(QString(), 12, endPanel);
            m_lblEndPtId->setStyleSheet(dimMono);
            v->addWidget(m_lblEndPtId);
            m_editEndName = new ElaLineEdit(endPanel);
            m_editEndName->setPlaceholderText(QString::fromUtf8("名称，如“颈点”"));
            v->addWidget(m_editEndName);
            m_editEndAnno = new ElaLineEdit(endPanel);
            m_editEndAnno->setPlaceholderText(QString::fromUtf8("备注"));
            v->addWidget(m_editEndAnno);
            m_chkShowEndName = new QPushButton(QString::fromUtf8("显示名称"), endPanel);
            m_chkShowEndName->setCheckable(true);
            m_chkShowEndName->setFixedHeight(24);
            m_chkShowEndName->setStyleSheet(chips);
            m_chkShowEndName->setCursor(Qt::PointingHandCursor);
            v->addWidget(m_chkShowEndName, 0, Qt::AlignLeft);
        }
        row->addWidget(endPanel, 1);
        layout->addLayout(row);
    }
    layout->addWidget(makeDivider(page));

    // ─── 外观 ───
    layout->addWidget(makeSectionHeader(QString::fromUtf8("外观"), page));
    {
        // 线型: 实线/虚线/点线 分段预览按钮 (图标即语义)。
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblStyle = new ElaText(QString::fromUtf8("线型"), 12, page);
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
            b->setFixedSize(56, 28);
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
        auto* lblWeight = new ElaText(QString::fromUtf8("粗细"), 12, page);
        lblWeight->setFixedWidth(kLabelW);
        row->addWidget(lblWeight);
        m_weightGroup = new QButtonGroup(page);
        m_weightGroup->setExclusive(true);
        const char* weightTexts[3] = {"细", "中", "粗"};
        for (int i = 0; i < 3; ++i) {
            auto* b = new QPushButton(QString::fromUtf8(weightTexts[i]), page);
            b->setCheckable(true);
            b->setFixedHeight(28);
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
        auto* lblColor = new ElaText(QString::fromUtf8("颜色"), 12, page);
        lblColor->setFixedWidth(kLabelW);
        row->addWidget(lblColor);
        m_btnColor = new QPushButton(page);
        m_btnColor->setFixedSize(44, 26);
        m_btnColor->setCursor(Qt::PointingHandCursor);
        m_btnColor->setToolTip(QString::fromUtf8("点击选择线段颜色"));
        row->addWidget(m_btnColor);
        m_lblColorHex = new ElaText(QString(), 12, page);
        m_lblColorHex->setStyleSheet(dimMono);
        row->addWidget(m_lblColorHex);
        row->addStretch();
        layout->addLayout(row);
    }
    {
        // 显示: 名称/长度/可见 toggle chips (hold N/L 的临时显示也画在这里)。
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* lblShow = new ElaText(QString::fromUtf8("显示"), 12, page);
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
            b->setFixedHeight(26);
            b->setStyleSheet(chips);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(QString::fromUtf8(d.tip));
            *d.slot = b;
            row->addWidget(b);
        }
        row->addStretch();
        layout->addLayout(row);
    }
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
    QSignalBlocker bStartAnno(m_editStartAnno);
    QSignalBlocker bEndName(m_editEndName);
    QSignalBlocker bEndShow(m_chkShowEndName);
    QSignalBlocker bEndAnno(m_editEndAnno);

    // 基本信息: 完整 ID 灰前缀 + 红色关键 "L#" (用户 2026-12: 关键名称标红).
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
    // 省道线: 角度/引用卡无意义 (连接卡已接管省道态) → 隐藏.
    if (m_refCard)   m_refCard->setVisible(!block->isDart());
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

    // 端点
    if (auto* sp = block->findPoint(seg->startPointId)) {
        m_lblStartPtId->setText(cad::param::Serial::tag(sp->serial));
        m_editStartName->setText(sp->name);
        m_chkShowStartName->setChecked(sp->showName);
        m_editStartAnno->setText(sp->annotation);
    }
    if (auto* ep = block->findPoint(seg->endPointId)) {
        m_lblEndPtId->setText(cad::param::Serial::tag(ep->serial));
        m_editEndName->setText(ep->name);
        m_chkShowEndName->setChecked(ep->showName);
        m_editEndAnno->setText(ep->annotation);
    }

    // Follower-state snapshot for 撤销全部: taken ONCE at open time so the
    // revert always restores the pre-dialog state (retargets / connects made
    // inside this dialog are undone too).
    m_snapshot.followerAtt.reset();
    QString connHint;
    for (const auto& att : m_paramDoc->attachments()) {
        if (att.isPin) continue;
        if (att.fromBlockId != m_blockId) continue;
        m_snapshot.followerAtt = att;
        // 连接分区状态提示 (已连接 L#·名; 仅角度/滑轨等子态由卡内 badge 细化)。
        connHint = QString::fromUtf8("已连接");
        if (const auto* leader = m_paramDoc->findBlock(att.toBlockId)) {
            if (const auto* lseg = leader->findSegment(att.toSegmentId)) {
                connHint += QStringLiteral(" ")
                    + cad::param::Serial::tag(lseg->serial);
                if (!lseg->name.isEmpty())
                    connHint += QStringLiteral("·") + lseg->name;
            }
        }
        // 连接子状态 (2026-xx 两维独立四态): 双拆开 = 自由; 独立角 =
        // 有连接线·无基准线; 仅角度 = 无连接线·有基准线。
        if (att.angleIndependent && att.angleOnly)
            connHint += QString::fromUtf8(" · 自由");
        else if (att.angleIndependent)
            connHint += QString::fromUtf8(" · 独立角");
        else if (att.angleOnly)
            connHint += QString::fromUtf8(" · 仅角度");
        break;
    }
    if (m_lblConnHint) {
        if (connHint.isEmpty() && block->isDart())
            connHint = QString::fromUtf8("省道线");
        m_lblConnHint->setText(connHint);
    }

    // Aux connections tab
    if (m_auxTab) m_auxTab->refreshConnections();

    // Aux points tab: refresh direction labels, save snapshots, refresh list
    if (m_auxTab) {
        m_auxTab->refreshDirLabels();
        m_auxTab->saveSnapshots(seg);
        m_auxTab->refreshList();
    }


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
        oldProps.role = static_cast<cad::param::SegmentRole>(m_snapshot.role);
        oldProps.showName = m_snapshot.showName;
        oldProps.showLength = m_snapshot.showLength;
        oldProps.visible = m_snapshot.visible;
        oldProps.color = m_snapshot.color;
        oldProps.lineStyle = static_cast<cad::param::LineStyle>(m_snapshot.lineStyle);
        oldProps.weight = m_snapshot.weight;
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
                newProps.role = s->role;
                newProps.showName = s->showName;
                newProps.showLength = s->showLength;
                newProps.visible = s->visible;
                newProps.color = s->color;
                newProps.lineStyle = s->lineStyle;
                newProps.weight = s->weight;
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
    if (m_extendCard) m_extendCard->setTarget(blockId, segmentId);
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
    if (m_angleCard) m_angleCard->refresh();
    if (m_connCard)  m_connCard->refresh();
}

void LinePropertyDialog::onConnCardChanged(SegmentConnectionCard::ChangeKind kind)
{
    // Mirror of the pre-extraction slot effects: every connection/angle
    // mutation needs a scene refresh; only topology changes (connect /
    // disconnect / re-target) also refresh the aux tab's connection list.
    refreshScene();
    switch (kind) {
    case SegmentConnectionCard::ChangeKind::Connected:
    case SegmentConnectionCard::ChangeKind::Retargeted:
        if (m_auxTab) m_auxTab->refreshConnections();
        break;
    default:
        break;  // 拆开/重连/影子归零 / Slide / Angle: no topology change.
    }
}

} // namespace cad::ui