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
#include "ui/TooltipFormatter.h"
#include "ui/NoteButton.h"
#include "parametric/FollowerAngle.h"
#include "document/commands/ReverseSegmentCommand.h"
#include "ui/PointRefEdit.h"
#include "ui/SegmentAnchorTab.h"
#include "ui/SegmentConnectionCard.h"
#include "ui/SegmentAuxTab.h"
#include "ui/LineEndpointSection.h"
#include "ui/LineAppearanceSection.h"
#include "ui/LineGeometrySection.h"

namespace cad::ui {

namespace {

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
    auto* t = new ElaText(title, 13, w);
    t->setObjectName(QStringLiteral("sectionTitleLabel"));
    t->setStyleSheet(QStringLiteral(
        "#sectionTitleLabel { background: transparent; font-weight:700; color:%1; }")
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

ElaLineEdit* makeCompactEdit(QWidget* parent, int width)
{
    auto* e = new ElaLineEdit(parent);
    e->setFixedHeight(kFieldH);
    e->setMaximumWidth(width);
    e->setStyleSheet(QStringLiteral("font-size: 11px;"));
    return e;
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
    if (m_scene)
        if (auto* item = m_scene->findBlockItem(m_blockId))
            item->setSelected(false);
}

void LinePropertyDialog::connectLiveSignals()
{
    connect(m_editName, &QLineEdit::textChanged,        this, &LinePropertyDialog::onLiveUpdate);
    connect(m_cmbRole,  &QComboBox::currentIndexChanged, this, &LinePropertyDialog::onLiveUpdate);

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
        m_lblSegId->setToolTip(cad::ui::TooltipFormatter::status(
            QStringLiteral("线段编号"),
            QStringLiteral("线段完整编号（全局唯一，随机前缀+类型序号）"),
            false));
        row->addWidget(m_lblSegId);
        row->addStretch();
        layout->addLayout(row);
    }
    // ─── 外观 (LineAppearanceSection 组件化) ───
    m_appearanceSection = new LineAppearanceSection(m_paramDoc, page);
    m_appearanceSection->setTarget(m_blockId, m_segmentId);
    connect(m_appearanceSection, &LineAppearanceSection::liveUpdated, this, &LinePropertyDialog::onLiveUpdate);
    layout->addWidget(m_appearanceSection);

    layout->addWidget(makeDivider(page));

    // ─── 摆放 (这条线摆在哪: 长度 + 方向 + 连接 + 端点; 发布长度参数随长度行走) ───
    layout->addWidget(makeSectionHeader(QString::fromUtf8("摆放"), page));

    // ─── 几何·摆放 (LineGeometrySection 组件化: 长度/滑轨/按需曲线) ───
    m_geometrySection = new LineGeometrySection(m_paramDoc, m_scene, page);
    m_geometrySection->setTarget(m_blockId, m_segmentId);
    connect(m_geometrySection, &LineGeometrySection::liveUpdated, this, &LinePropertyDialog::onLiveUpdate);
    connect(m_geometrySection, &LineGeometrySection::lengthApplied, this, &LinePropertyDialog::onLiveUpdate);
    connect(m_geometrySection, &LineGeometrySection::sceneRefreshRequested, this, &LinePropertyDialog::refreshScene);
    layout->addWidget(m_geometrySection);

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

    // ─── 端点 (LineEndpointSection 组件化) ───
    m_endpointSection = new LineEndpointSection(m_paramDoc, m_scene, page);
    m_endpointSection->setTarget(m_blockId, m_segmentId);
    connect(m_endpointSection, &LineEndpointSection::liveUpdated, this, &LinePropertyDialog::onLiveUpdate);
    connect(m_endpointSection, &LineEndpointSection::directionArrowClicked, this, &LinePropertyDialog::onDirectionArrowClicked);
    connect(m_endpointSection, &LineEndpointSection::connectionChanged, this, [this] {
        refreshConnHint();
        if (m_refCard) m_refCard->refresh();
        refreshScene();
    });
    layout->addWidget(m_endpointSection);

    // 角度区 (§3 顺序): ①对齐点+方向两段式行 对齐点 [P1] 方向：点1→点2 [独立]
    // (文案 v2, 2026-12 用户拍板) → ②角度输入行。方向行在前:
    // 先回答"对齐到什么方向", 再给数值。
    m_angleCard = new SegmentAngleCard(m_paramDoc, m_scene, page);
    layout->addWidget(m_refCard);
    layout->addWidget(m_angleCard);

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
    // 基本信息: 完整 ID 灰前缀 + 红色关键 "L#" (用户 2026-12: 关键名称标红).
    m_lblSegId->setText(cad::param::Serial::toHtml(seg->serial));
    m_editName->setText(seg->name);
    m_noteSeg->setNote(seg->annotation);  ///< setNote 不发 noteEdited (非用户编辑).
    m_cmbRole->setCurrentIndex(static_cast<int>(seg->role));

    if (m_appearanceSection)
        m_appearanceSection->populateFromModel(*block, *seg);

    if (m_geometrySection)
        m_geometrySection->populateFromModel(*block, *seg);

    // 锚点 Tab
    const bool isCurve = seg->isCurve();
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
    if (m_angleCard) m_angleCard->setVisible(!block->isDart());

    if (m_endpointSection)
        m_endpointSection->populateFromModel(*block, *seg);

    m_session.takeSnapshot(m_paramDoc, m_blockId, m_segmentId);
    refreshConnHint();

    // 端点双列微卡只读连接行 (方案 A, 2026-xx)。
    refreshEndpointConnRows();

    // Aux points tab: refresh direction labels, save snapshots, refresh list
    if (m_auxTab) {
        m_auxTab->refreshDirLabels();
        m_auxTab->saveSnapshots(seg);
        m_auxTab->refreshList();
    }



    // Bridge lines: length/angle are passive measurements — lock the editors.
    applyBridgeReadOnly();

    // 朝向箭头: 换向资格 + 方向 (P1→P2 世界朝向). 换向后**端点组位置固定**
    // (2026-08-31 用户拍板), 箭头只翻方向 —— populate 已按几何位置回填。
    refreshDirectionArrow();

    // 端点延长量: 置灰 + 回填数值/公式.
    refreshEndpointExtends();
    // 长度模式 + 滑轨 (2026-xx §3/§6.3).
    refreshLengthMode();
    refreshSlideRow();
}

void LinePropertyDialog::refreshEndpointConnRows()
{
    if (m_endpointSection)
        m_endpointSection->refreshEndpointConnRows();
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
    if (m_appearanceSection)
        m_appearanceSection->applyHoldOverride(forceName, forceLength);

    if (m_endpointSection && m_paramDoc) {
        const auto* block = m_paramDoc->findBlock(m_blockId);
        const auto* seg = block ? block->findSegment(m_segmentId) : nullptr;
        if (block && seg) {
            const QUuid topId = LineEndpointSection::fixedTopPointId(block, seg);
            if (const auto* topPt = block->findPoint(topId)) {
                const QSignalBlocker b(m_endpointSection->chkShowStartName());
                m_endpointSection->chkShowStartName()->setChecked(topPt->showName || forceName);
            }
            const QUuid botId = topId.isNull() ? QUuid()
                : (topId == seg->startPointId ? seg->endPointId : seg->startPointId);
            if (const auto* botPt = block->findPoint(botId)) {
                const QSignalBlocker b(m_endpointSection->chkShowEndName());
                m_endpointSection->chkShowEndName()->setChecked(botPt->showName || forceName);
            }
        }
    }
}

void LinePropertyDialog::applyBridgeReadOnly()
{
    if (!m_paramDoc) return;
    const cad::param::Block* block = m_paramDoc->findBlock(m_blockId);
    if (!block || !block->isBridge) return;

    if (m_geometrySection)
        m_geometrySection->applyBridgeReadOnly();

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

    if (m_appearanceSection)
        m_appearanceSection->applyToModel(block, seg);

    if (m_geometrySection)
        m_geometrySection->applyToModel(block, seg);

    if (m_endpointSection)
        m_endpointSection->applyToModel(block, seg);
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
    if (m_geometrySection)
        m_geometrySection->refreshActualLengthLabel();
}

void LinePropertyDialog::refreshLengthMode()
{
    if (m_geometrySection)
        m_geometrySection->refreshLengthMode();
}

void LinePropertyDialog::refreshSlideRow()
{
    if (m_geometrySection)
        m_geometrySection->refreshSlideRow();
}

void LinePropertyDialog::onLiveUpdate()
{
    applyToModel();
    refreshScene();
}

void LinePropertyDialog::updateWeightControls()
{
    if (m_appearanceSection)
        m_appearanceSection->updateWeightControls();
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
    m_session.commit(m_paramDoc, m_blockId, m_segmentId, m_isCreation);
    m_confirmed = true;
    accept();
    deleteLater();
}

void LinePropertyDialog::onRejected()
{
    m_session.rollback(m_paramDoc, m_blockId, m_segmentId, m_isCreation, m_auxTab);
    refreshScene();
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
    if (m_endpointSection)   m_endpointSection->setTarget(blockId, segmentId);
    if (m_appearanceSection) m_appearanceSection->setTarget(blockId, segmentId);
    if (m_geometrySection)   m_geometrySection->setTarget(blockId, segmentId);
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

void LinePropertyDialog::refreshEndpointExtends()
{
    if (m_endpointSection)
        m_endpointSection->refreshEndpointExtends();
}

void LinePropertyDialog::onDirectionArrowClicked()
{
    if (!m_paramDoc) return;
    QString why;
    if (!cad::cmd::ReverseSegmentCommand::canReverse(m_paramDoc, m_blockId, m_segmentId, &why)) {
        if (!why.isEmpty() && m_endpointSection && m_endpointSection->btnDirectionArrow())
            m_endpointSection->btnDirectionArrow()->setToolTip(cad::ui::TooltipFormatter::plain(why));
        return;
    }
    if (auto* stack = m_paramDoc->undoStack())
        stack->push(new cad::cmd::ReverseSegmentCommand(m_paramDoc, m_blockId, m_segmentId));
    else {
        cad::cmd::ReverseSegmentCommand cmd(m_paramDoc, m_blockId, m_segmentId);
        cmd.redo();
    }
    refreshScene();
    populateFromModel();
    refreshDirectionArrow();
}

void LinePropertyDialog::refreshDirectionArrow()
{
    if (m_endpointSection)
        m_endpointSection->refreshDirectionArrow();
}

// ---------------------------------------------------------------------------
const cad::param::MeasureVariable* LinePropertyDialog::findBridgeMeasure() const
{
    return m_geometrySection ? m_geometrySection->findBridgeMeasure() : nullptr;
}

void LinePropertyDialog::onRefCardChanged()
{
    refreshScene();
    refreshEndpointConnRows();
    refreshConnHint();
    if (m_angleCard) m_angleCard->refresh();
    if (m_connCard)  m_connCard->refresh();
    refreshLengthMode();
    refreshSlideRow();
}

void LinePropertyDialog::onConnCardChanged()
{
    refreshScene();
    refreshEndpointConnRows();
    refreshConnHint();
    if (m_refCard) m_refCard->refresh();
    refreshLengthMode();
    refreshSlideRow();
}

} // namespace cad::ui