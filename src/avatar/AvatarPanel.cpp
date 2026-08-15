#include "AvatarPanel.h"

#include "AvatarModel.h"
#include "AvatarSolver.h"
#include "AvatarView3D.h"
#include "JsonReader.h"
#include "MeasureSystem.h"
#include "Mesh3D.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "ElaTabBar.h"

#include <cmath>
#include <unordered_set>

#ifndef AVATAR_ASSETS_DIR
#define AVATAR_ASSETS_DIR "assets/avatar"
#endif

namespace cad::ui {

namespace {
constexpr int kSliderRange = 100;  ///< 滑杆整数范围（权重 -1..1 / 0..1 映射到 ±100）
constexpr int kViewMinWidth = 360; ///< 左侧 3D 视口最小宽度
constexpr int kRightWidth = 360;   ///< 右侧调整面板固定宽度

/// 制版软件定位：模特应为「裸体人台」。加载 base.obj 时按组排除这些
/// 衣物/头发/生殖器（数据文件不删，仅加载时过滤，随时可改回）。
const std::unordered_set<std::string>& excludedClothingGroups() {
    static const std::unordered_set<std::string> s = {
        "helper-tights",
        "helper-skirt",
        "helper-hair",
        "helper-genital",
    };
    return s;
}

/// JSON 字符串转义（标注点名称保存用）。
std::string escapeJson(const std::string& s)
{
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '\\': r += "\\\\"; break;
        case '"': r += "\\\""; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default: r += c; break;
        }
    }
    return r;
}
} // namespace

AvatarPanel::AvatarPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("avatarPanel"));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    m_view = new cad::avatar::AvatarView3D(this);
    m_view->setMinimumWidth(kViewMinWidth);
    root->addWidget(m_view, 1);

    auto* right = new QWidget(this);
    right->setFixedWidth(kRightWidth);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    // 顶部全局状态行：身高（体型相关，两标签页都可见）
    auto* statusRow = new QHBoxLayout();
    auto* heightTitle = new QLabel(QStringLiteral("身高"), this);
    m_heightLabel = new QLabel(QStringLiteral("--"), this);
    m_heightLabel->setObjectName(QStringLiteral("heightLabel"));
    statusRow->addWidget(heightTitle);
    statusRow->addWidget(m_heightLabel);
    statusRow->addStretch(1);
    rightLayout->addLayout(statusRow);

    // 顶部标签切换：调整（核心快捷项）/ 调整项（体型滑杆）/ 测量点（标注点）
    m_tabBar = new ElaTabBar(right);
    m_tabBar->addTab(QStringLiteral("调整"));
    m_tabBar->addTab(QStringLiteral("调整项"));
    m_tabBar->addTab(QStringLiteral("测量点"));
    m_tabBar->setTabSize(QSize(110, 28)); // 三项平分 360px 侧边栏，避免「测量点」被挤出
    m_tabBar->setExpanding(true);
    m_tabBar->setDrawBase(false);
    m_tabBar->setCursor(Qt::PointingHandCursor);
    // 固定标签：ElaTabBar 默认 closable/movable，× 会经 onTabCloseRequested
    // deleteLater 掉堆叠页 → 关闭这两个开关。
    m_tabBar->setTabsClosable(false);
    m_tabBar->setMovable(false);
    m_tabBar->setAcceptDrops(false);
    m_tabBar->setObjectName(QStringLiteral("avatarTabBar"));
    rightLayout->addWidget(m_tabBar);

    m_stack = new QStackedWidget(right);
    rightLayout->addWidget(m_stack, 1);

    // ===== 页 0：调整（上半身核心快捷项，胸围/下胸围/罩杯）=====
    auto* quickPage = new QWidget(m_stack);
    auto* quickLayout = new QVBoxLayout(quickPage);
    quickLayout->setContentsMargins(0, 6, 0, 0);
    quickLayout->setSpacing(4);

    // 按胸围匹配自然体态：输入胸围（可选下胸围）→ 自动匹配自然罩杯
    auto* bustMatchBtn = new QPushButton(QStringLiteral("按胸围匹配自然体态"), quickPage);
    bustMatchBtn->setObjectName(QStringLiteral("btn_bustMatch"));
    quickLayout->addWidget(bustMatchBtn);
    connect(bustMatchBtn, &QPushButton::clicked, this, &AvatarPanel::applyBustMatch);

    m_adjustLayout = new QVBoxLayout();
    m_adjustLayout->setContentsMargins(0, 0, 0, 0);
    m_adjustLayout->setSpacing(4);
    quickLayout->addLayout(m_adjustLayout);
    quickLayout->addStretch(1);
    m_stack->addWidget(quickPage);

    // ===== 页 1：调整项（体型滑杆分组）=====
    auto* adjustPage = new QWidget(m_stack);
    auto* adjustLayout = new QVBoxLayout(adjustPage);
    adjustLayout->setContentsMargins(0, 6, 0, 0);
    adjustLayout->setSpacing(6);

    auto* ioRow = new QHBoxLayout();
    auto* resetBtn = new QPushButton(QStringLiteral("重置"), adjustPage);
    auto* exportBtn = new QPushButton(QStringLiteral("导出 OBJ"), adjustPage);
    resetBtn->setObjectName(QStringLiteral("btn_reset"));
    exportBtn->setObjectName(QStringLiteral("btn_export"));
    ioRow->addWidget(resetBtn);
    ioRow->addWidget(exportBtn);
    ioRow->addStretch(1);
    adjustLayout->addLayout(ioRow);

    auto* hint = new QLabel(QStringLiteral("双击测量滑杆可输入目标 cm"), adjustPage);
    hint->setObjectName(QStringLiteral("avatarHint"));
    adjustLayout->addWidget(hint);

    auto* scroll = new QScrollArea(adjustPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* groupsHost = new QWidget(scroll);
    m_groupsLayout = new QVBoxLayout(groupsHost);
    m_groupsLayout->setContentsMargins(0, 0, 0, 0);
    m_groupsLayout->setSpacing(4);
    scroll->setWidget(groupsHost);
    adjustLayout->addWidget(scroll, 1);
    m_stack->addWidget(adjustPage);

    // ===== 页 1：测量点（标注点管理）=====
    auto* annotPage = new QWidget(m_stack);
    auto* annotLayout = new QVBoxLayout(annotPage);
    annotLayout->setContentsMargins(0, 6, 0, 0);
    annotLayout->setSpacing(6);

    auto* annotHeader = new QHBoxLayout();
    m_annotationModeBtn = new QToolButton(annotPage);
    m_annotationModeBtn->setObjectName(QStringLiteral("btn_annotationMode"));
    m_annotationModeBtn->setText(QStringLiteral("标注模式"));
    m_annotationModeBtn->setCheckable(true);
    m_annotationModeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_lineModeBtn = new QToolButton(annotPage);
    m_lineModeBtn->setObjectName(QStringLiteral("btn_lineMode"));
    m_lineModeBtn->setText(QStringLiteral("画线"));
    m_lineModeBtn->setCheckable(true);
    m_lineModeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_lineModeBtn->setToolTip(QStringLiteral("开启后依次点两个标注点，连一条线（一次一条）"));
    auto* annotHint = new QLabel(QStringLiteral("标注点：点击放点（Ctrl=吸附测量线高度）"), annotPage);
    annotHint->setObjectName(QStringLiteral("annotHint"));
    annotHeader->addWidget(m_annotationModeBtn);
    annotHeader->addWidget(m_lineModeBtn);
    annotHeader->addWidget(annotHint, 1);
    annotLayout->addLayout(annotHeader);

    m_annotationTree = new QTreeWidget(annotPage);
    m_annotationTree->setObjectName(QStringLiteral("annotationTree"));
    m_annotationTree->setHeaderLabels(
        {QStringLiteral("序号"), QStringLiteral("名称"), QStringLiteral("坐标")});
    m_annotationTree->setRootIsDecorated(false);
    m_annotationTree->setColumnWidth(0, 44);
    m_annotationTree->setColumnWidth(1, 120);
    m_annotationTree->setColumnWidth(2, 160);
    m_annotationTree->setMinimumHeight(160);
    annotLayout->addWidget(m_annotationTree, 1);

    auto* annotBtns = new QHBoxLayout();
    auto* renameBtn = new QPushButton(QStringLiteral("重命名"), annotPage);
    auto* delBtn = new QPushButton(QStringLiteral("删除"), annotPage);
    auto* clearBtn = new QPushButton(QStringLiteral("清空"), annotPage);
    auto* clearLineBtn = new QPushButton(QStringLiteral("清线"), annotPage);
    renameBtn->setObjectName(QStringLiteral("btn_annotRename"));
    delBtn->setObjectName(QStringLiteral("btn_annotDelete"));
    clearBtn->setObjectName(QStringLiteral("btn_annotClear"));
    clearLineBtn->setObjectName(QStringLiteral("btn_annotClearLines"));
    annotBtns->addWidget(renameBtn);
    annotBtns->addWidget(delBtn);
    annotBtns->addWidget(clearBtn);
    annotBtns->addWidget(clearLineBtn);
    annotLayout->addLayout(annotBtns);

    auto* annotIo = new QHBoxLayout();
    auto* saveBtn = new QPushButton(QStringLiteral("保存标注"), annotPage);
    auto* loadBtn = new QPushButton(QStringLiteral("加载标注"), annotPage);
    saveBtn->setObjectName(QStringLiteral("btn_annotSave"));
    loadBtn->setObjectName(QStringLiteral("btn_annotLoad"));
    annotIo->addWidget(saveBtn);
    annotIo->addWidget(loadBtn);
    annotLayout->addLayout(annotIo);
    m_stack->addWidget(annotPage);

    root->addWidget(right, 0);

    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
    });

    connect(resetBtn, &QPushButton::clicked, this, &AvatarPanel::resetToDefault);
    connect(exportBtn, &QPushButton::clicked, this, &AvatarPanel::exportObj);

    connect(m_annotationModeBtn, &QToolButton::toggled, this, [this](bool on) {
        m_view->setAnnotationMode(on);
        if (on && m_lineModeBtn && m_lineModeBtn->isChecked())
            m_lineModeBtn->setChecked(false); // 标注模式与画线模式互斥
    });
    connect(m_lineModeBtn, &QToolButton::toggled, this, [this](bool on) {
        m_view->setLineMode(on);
        if (on && m_annotationModeBtn && m_annotationModeBtn->isChecked())
            m_annotationModeBtn->setChecked(false); // 互斥
    });
    connect(m_view, &cad::avatar::AvatarView3D::annotationPicked, this,
            &AvatarPanel::onAnnotationPicked);
    connect(m_view, &cad::avatar::AvatarView3D::annotationMoved, this,
            &AvatarPanel::onAnnotationMoved);
    connect(m_view, &cad::avatar::AvatarView3D::annotationLineDrawn, this,
            [this](int a, int b) {
                emit statusMessage(QStringLiteral("已连线 %1 → %2").arg(a + 1).arg(b + 1));
            });
    connect(renameBtn, &QPushButton::clicked, this, &AvatarPanel::renameSelectedAnnotation);
    connect(delBtn, &QPushButton::clicked, this, &AvatarPanel::removeSelectedAnnotation);
    connect(clearBtn, &QPushButton::clicked, this, &AvatarPanel::clearAnnotations);
    connect(clearLineBtn, &QPushButton::clicked, this, [this] {
        if (m_view)
            m_view->setAnnotationLines({});
        emit statusMessage(QStringLiteral("已清空连线"));
    });
    connect(saveBtn, &QPushButton::clicked, this, &AvatarPanel::saveAnnotations);
    connect(loadBtn, &QPushButton::clicked, this, &AvatarPanel::loadAnnotations);
}

AvatarPanel::~AvatarPanel() = default;

bool AvatarPanel::loadDefault()
{
    const QString assetsDir = QString::fromUtf8(AVATAR_ASSETS_DIR);
    const std::string basePath = assetsDir.toStdString() + "/base.obj";
    const std::string targetsDir = assetsDir.toStdString() + "/targets";
    const std::string chainsPath = assetsDir.toStdString() + "/measurement_chains.json";
    const std::string slidersPath = assetsDir.toStdString() + "/sliders.json";

    try {
        cad::avatar::Mesh3D base = cad::avatar::loadObjFile(basePath, excludedClothingGroups());
        // 完整原始坐标（未过滤，含被排除顶点），供测量链表被排除顶点做最近邻重命中。
        const std::vector<cad::avatar::Vec3> fullCoords =
            cad::avatar::loadObjFile(basePath).verts;

        auto measures = std::make_unique<cad::avatar::MeasureSystem>();
        measures->loadChains(chainsPath);
        // 顶点缩减后重映射测量链：被排除顶点（如肩宽链中间点在 helper-hair 的 C7）
        // 用完整坐标找最近保留顶点重新命中到身体。
        if (base.isRemapped())
            measures->remapChains(base.vertexRemap, fullCoords);

        auto model = std::make_unique<cad::avatar::AvatarModel>(std::move(base), targetsDir);
        auto solver = std::make_unique<cad::avatar::AvatarSolver>(model.get(), measures.get());
        solver->loadSliders(slidersPath); // 内部应用默认体型
        m_model = std::move(model);
        m_measures = std::move(measures);
        m_solver = std::move(solver);
    } catch (const std::exception& e) {
        emit statusMessage(QStringLiteral("加载模特数据失败：%1").arg(QString::fromUtf8(e.what())));
        return false;
    }

    m_view->setMesh(m_model->mesh());

    // 测量线显示名：sliders.json 的 measure/* 滑杆 id -> 中文 label
    std::map<std::string, std::string> measureNames;
    for (const auto& def : m_solver->sliders()) {
        if (def.id.compare(0, 8, "measure/") == 0 && !def.label.empty())
            measureNames[def.id] = def.label;
    }
    // 自定义纯测量项（无对应 morph 滑杆，仅 3D 视口显示）
    measureNames["custom/bust-to-bust"] = QStringLiteral("胸点至胸点").toStdString();

    // 删除的两条测量线：肩宽 / 肩颈点至胸点。仅从 3D 视口剔除（不画线、
    // 不生成勾选框）；链数据仍保留在 MeasureSystem。前胸宽（frontchest，
    // 对应测量参考图「前胸上部宽度」红线）保留显示：两点距离链在视口内
    // 走贴肤测地线绘制，与参考图一致。
    auto measureChains = m_measures->chains(); // 拷贝后剔除
    measureChains.erase("custom/shoulder-width");
    measureChains.erase("custom/neck-to-bust");
    m_view->setMeasureChains(measureChains, measureNames, m_measures->pointChains(),
                             m_measures->sectionChains(), m_measures->straightChains());
    rebuildGroups();
    rebuildAdjustPage();
    syncSlidersFromSolver();
    refreshValueLabels();
    applyToView();
    m_heightLabel->setText(
        QStringLiteral("%1 cm").arg(m_solver->currentHeightCm(), 0, 'f', 1));
    return true;
}

void AvatarPanel::rebuildGroups()
{
    // 清旧分组
    while (QLayoutItem* item = m_groupsLayout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_rows.clear();
    m_groups.clear();

    const auto& sliders = m_solver->sliders();

    // ===== 「全身」特殊组（放最前）：身高（cm + 锁）+ 身材比例（档位）=====
    const std::vector<std::string> fullBodyIds = {
        "macrodetails-height/Height",
        "macrodetails-proportions/BodyProportions",
    };
    {
        auto* fullContent = new QWidget(this);
        auto* fullRows = new QVBoxLayout(fullContent);
        fullRows->setContentsMargins(12, 0, 0, 0);
        fullRows->setSpacing(4);
        for (const auto& id : fullBodyIds) {
            const size_t sidx = sliderIdxOf(id);
            if (sidx >= sliders.size())
                continue;
            QHBoxLayout* row = buildSliderRow(sidx, fullContent, QStringLiteral("slider_"));
            fullRows->addLayout(row);
        }
        auto* fullToggle = new QToolButton(this);
        fullToggle->setObjectName(QStringLiteral("avatarGroupToggle"));
        fullToggle->setText(QStringLiteral("全身"));
        fullToggle->setCheckable(true);
        fullToggle->setChecked(true);
        fullToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        fullToggle->setArrowType(Qt::RightArrow);
        fullToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_groupsLayout->addWidget(fullToggle);
        m_groupsLayout->addWidget(fullContent);
        m_groups.push_back({fullToggle, fullContent});
        connect(fullToggle, &QToolButton::toggled, fullContent, [fullContent](bool on) {
            fullContent->setVisible(on);
        });
    }

    m_rows.reserve(sliders.size());

    // 按 group 顺序切分；每组一个折叠区（全 hidden 的组不显示）
    size_t i = 0;
    while (i < sliders.size()) {
        const std::string groupName = sliders[i].group;
        auto* content = new QWidget(this);
        auto* rowsLayout = new QVBoxLayout(content);
        rowsLayout->setContentsMargins(12, 0, 0, 0);
        rowsLayout->setSpacing(4);

        const size_t rowBegin = m_rows.size();
        while (i < sliders.size() && sliders[i].group == groupName) {
            const auto& sid = sliders[i].id;
            // 面板隐藏项 + 已抽到「全身」组的项不在此显示
            if (sliders[i].hidden || sid == "macrodetails-height/Height" ||
                sid == "macrodetails-proportions/BodyProportions") {
                ++i;
                continue;
            }
            QHBoxLayout* row = buildSliderRow(i, content, QStringLiteral("slider_"));
            rowsLayout->addLayout(row);
            ++i;
        }
        if (m_rows.size() == rowBegin) {
            content->deleteLater(); // 该组全部 hidden，不创建折叠区
            continue;
        }

        auto* toggle = new QToolButton(this);
        toggle->setObjectName(QStringLiteral("avatarGroupToggle"));
        toggle->setText(QString::fromStdString(groupName));
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_groupsLayout->addWidget(toggle);
        m_groupsLayout->addWidget(content);
        m_groups.push_back({toggle, content});
        connect(toggle, &QToolButton::toggled, content, [content](bool on) {
            content->setVisible(on);
        });
    }
    m_groupsLayout->addStretch(1);
}

QHBoxLayout* AvatarPanel::buildSliderRow(size_t sliderIdx, QWidget* parent,
                                         const QString& objPrefix)
{
    const auto& def = m_solver->sliders()[sliderIdx];
    auto* row = new QHBoxLayout();
    auto* nameLabel = new QLabel(QString::fromStdString(def.label), parent);
    nameLabel->setMinimumWidth(120);
    nameLabel->setToolTip(QString::fromStdString(def.id));
    auto* slider = new QSlider(Qt::Horizontal, parent);
    const bool isUnit = (def.kind == cad::avatar::SliderKind::Single ||
                         def.kind == cad::avatar::SliderKind::Macro ||
                         def.kind == cad::avatar::SliderKind::Ethnic);
    slider->setRange(isUnit ? 0 : -kSliderRange, kSliderRange);
    slider->setObjectName(objPrefix + QString::number(sliderIdx));
    auto* valueLabel = new QLabel(QStringLiteral("--"), parent);
    valueLabel->setMinimumWidth(56);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(nameLabel);
    row->addWidget(slider, 1);
    row->addWidget(valueLabel);

    // 同一个 solver 值可以被多个页面共用一个滑杆视图（复制），
    // 全部登记进 m_rows，由 syncSlidersFromSolver / onSliderValueChanged 统一联动。
    m_rows.push_back({slider, valueLabel, sliderIdx});
    const int idx = static_cast<int>(m_rows.size() - 1);
    connect(slider, &QSlider::valueChanged, this,
            [this, idx] { onSliderValueChanged(idx); });
    // 拖动中延迟测量计算与法线（保持旧值），松开后重算最终值，去卡顿。
    connect(slider, &QSlider::sliderPressed, this, [this] {
        m_draggingSlider = true;
        if (m_view) m_view->setDeferMeasure(true);
        if (m_model) m_model->setUpdateNormals(false);
    });
    connect(slider, &QSlider::sliderReleased, this, [this] {
        m_draggingSlider = false;
        if (m_view) m_view->setDeferMeasure(false);
        if (m_model) m_model->setUpdateNormals(true); // 内部补算法线
        // 胸围锁：松开时二分反调胸腔使测量回锁值（拖动中跳过，只算最终值）。
        if (m_solver && m_solver->measureLockActive()) {
            const double after = m_solver->enforceMeasureLock();
            if (std::fabs(after - m_solver->measureLockTargetCm()) > 0.15) {
                emit statusMessage(
                    QStringLiteral("胸围锁：胸腔补偿已达极限，当前 %1 cm（目标 %2 cm）")
                        .arg(after, 0, 'f', 1)
                        .arg(m_solver->measureLockTargetCm(), 0, 'f', 1));
            }
            syncSlidersFromSolver();
        }
        // 身高锁：调纵向段后总身高会变 → 二分 Height 宏回锁值（同上，只算最终值）。
        if (m_solver && m_solver->heightLockActive()) {
            const double after = m_solver->enforceHeightLock();
            if (std::fabs(after - m_solver->heightLockTargetCm()) > 0.15) {
                emit statusMessage(
                    QStringLiteral("身高锁：超出可调范围，当前 %1 cm（目标 %2 cm）")
                        .arg(after, 0, 'f', 1)
                        .arg(m_solver->heightLockTargetCm(), 0, 'f', 1));
            }
            syncSlidersFromSolver();
        }
        refreshValueLabels(); // 松开后刷新一次测量值显示（测地线最终值）
        applyToView();
    });
    // 测量滑杆 + 身高宏双击 -> 输入目标 cm
    if (!def.measureKey.empty() || def.id == "macrodetails-height/Height") {
        slider->installEventFilter(this);
        slider->setProperty("sliderIdx", idx);
    }
    return row;
}

size_t AvatarPanel::sliderIdxOf(const std::string& id) const
{
    const auto& sliders = m_solver->sliders();
    for (size_t i = 0; i < sliders.size(); ++i)
        if (sliders[i].id == id)
            return i;
    return sliders.size(); // 未找到（调用方校验）
}

void AvatarPanel::rebuildAdjustPage()
{
    if (!m_adjustLayout || !m_solver)
        return;
    // 清空「调整」页旧内容（直接子项只有折叠开关 + 内容 widget）
    while (QLayoutItem* item = m_adjustLayout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    // 「调整」页 = 按「上半身/下半身」组织的合体快捷项（复制视图）。
    // 与「调整项」里同名滑杆共享同一个 solver 值（m_rows 多视图联动）。
    const auto& sliders = m_solver->sliders();
    const auto addGroup = [&](const QString& name, const std::vector<std::string>& ids) {
        auto* content = new QWidget(this);
        auto* rowsLayout = new QVBoxLayout(content);
        rowsLayout->setContentsMargins(12, 0, 0, 0);
        rowsLayout->setSpacing(4);
        for (const auto& id : ids) {
            const size_t sidx = sliderIdxOf(id);
            if (sidx >= sliders.size())
                continue; // 该 id 不存在则跳过
            QHBoxLayout* row = buildSliderRow(sidx, content, QStringLiteral("adjustSlider_"));
            rowsLayout->addLayout(row);
            // 胸围行加「锁」开关：锁定后调其他滑杆（如罩杯）自动反调胸腔保持胸围恒定
            if (id == "measure/measure-bust-circ-decr|incr") {
                m_bustLockBtn = new QToolButton(content);
                m_bustLockBtn->setObjectName(QStringLiteral("bustLockBtn"));
                m_bustLockBtn->setText(QStringLiteral("锁"));
                m_bustLockBtn->setCheckable(true);
                m_bustLockBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
                m_bustLockBtn->setToolTip(QStringLiteral(
                    "锁定胸围：调整罩杯等其他滑杆时自动补偿胸腔，胸围保持恒定；"
                    "直接拖动胸围滑杆 = 修改锁值"));
                row->addWidget(m_bustLockBtn);
                connect(m_bustLockBtn, &QToolButton::toggled, this, [this](bool on) {
                    if (!m_solver)
                        return;
                    const std::string bustId = "measure/measure-bust-circ-decr|incr";
                    if (on) {
                        const double v = m_solver->measureNow(bustId);
                        m_solver->setMeasureLock(bustId, true, v);
                        m_bustLockBtn->setText(QStringLiteral("已锁"));
                        emit statusMessage(QStringLiteral("胸围已锁定 %1 cm").arg(v, 0, 'f', 1));
                    } else {
                        m_solver->setMeasureLock(bustId, false, 0.0);
                        m_bustLockBtn->setText(QStringLiteral("锁"));
                        emit statusMessage(QStringLiteral("胸围锁已解除"));
                    }
                });
            }
            // 身高行加「锁」：锁定后调纵向分段自动反调 Height 宏保持总身高恒定
            if (id == "macrodetails-height/Height") {
                m_heightLockBtn = new QToolButton(content);
                m_heightLockBtn->setObjectName(QStringLiteral("heightLockBtn"));
                m_heightLockBtn->setText(QStringLiteral("锁"));
                m_heightLockBtn->setCheckable(true);
                m_heightLockBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
                m_heightLockBtn->setToolTip(QStringLiteral(
                    "锁定身高：调整大腿高/小腿高等纵向分段时自动补偿，总身高保持恒定；"
                    "直接拖动身高滑杆 = 修改锁值"));
                row->addWidget(m_heightLockBtn);
                connect(m_heightLockBtn, &QToolButton::toggled, this, [this](bool on) {
                    if (!m_solver)
                        return;
                    if (on) {
                        const double h = m_solver->currentHeightCm();
                        m_solver->setHeightLock(true, h);
                        m_heightLockBtn->setText(QStringLiteral("已锁"));
                        emit statusMessage(QStringLiteral("身高已锁定 %1 cm").arg(h, 0, 'f', 1));
                    } else {
                        m_solver->setHeightLock(false, 0.0);
                        m_heightLockBtn->setText(QStringLiteral("锁"));
                        emit statusMessage(QStringLiteral("身高锁已解除"));
                    }
                });
            }
        }
        auto* toggle = new QToolButton(this);
        toggle->setObjectName(QStringLiteral("avatarGroupToggle"));
        toggle->setText(name);
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(Qt::RightArrow);
        toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(toggle, &QToolButton::toggled, content, [content](bool on) {
            content->setVisible(on);
        });
        m_adjustLayout->addWidget(toggle);
        m_adjustLayout->addWidget(content);
    };

    // 全身（身高 cm+锁、身材比例档位）
    addGroup(QStringLiteral("全身"), {
        "macrodetails-height/Height",
        "macrodetails-proportions/BodyProportions",
    });

    // 上半身（从上到下：颈 → 肩 → 胸 → 臂 → 腰）
    addGroup(QStringLiteral("上半身"), {
        "measure/measure-neck-circ-decr|incr",
        "measure/measure-neck-height-decr|incr",
        "measure/measure-shoulder-dist-decr|incr",
        "measure/measure-bust-circ-decr|incr",
        "measure/measure-underbust-circ-decr|incr",
        "measure/measure-frontchest-dist-decr|incr",
        "breast/BreastSize",
        "breast/breast-trans-down|up",
        "breast/breast-dist-decr|incr",
        "measure/measure-upperarm-circ-decr|incr",
        "measure/measure-upperarm-length-decr|incr",
        "measure/measure-lowerarm-length-decr|incr",
        "measure/measure-wrist-circ-decr|incr",
        "measure/measure-waist-circ-decr|incr",
        "measure/measure-napetowaist-dist-decr|incr",
        "measure/measure-waisttohip-dist-decr|incr",
    });

    // 下半身（从上到下：臀 → 大腿 → 膝 → 小腿 → 踝）
    addGroup(QStringLiteral("下半身"), {
        "measure/measure-hips-circ-decr|incr",
        "measure/measure-upperleg-height-decr|incr",
        "measure/measure-thigh-circ-decr|incr",
        "measure/measure-knee-circ-decr|incr",
        "measure/measure-lowerleg-height-decr|incr",
        "measure/measure-calf-circ-decr|incr",
        "measure/measure-ankle-circ-decr|incr",
    });
}

bool AvatarPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (auto* slider = qobject_cast<QSlider*>(watched)) {
            const QVariant idxV = slider->property("sliderIdx");
            if (idxV.isValid())
                onSliderDoubleClicked(idxV.toInt());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AvatarPanel::onSliderDoubleClicked(int idx)
{
    if (!m_solver || idx < 0 || idx >= static_cast<int>(m_rows.size()))
        return;
    const auto& def = m_solver->sliders()[m_rows[static_cast<size_t>(idx)].sliderIdx];
    if (def.measureKey.empty() && def.id != "macrodetails-height/Height")
        return;

    // 身高宏：双击输入目标 cm → 二分 Height 宏（身高锁开则更新锁值）。
    if (def.id == "macrodetails-height/Height") {
        bool ok = false;
        const double target = QInputDialog::getDouble(
            this, QStringLiteral("设置身高"),
            QStringLiteral("身高目标值 (cm)："),
            m_solver->currentHeightCm(), 100.0, 250.0, 1, &ok);
        if (!ok)
            return;
        const double got = m_solver->solveHeightCm(target);
        if (m_solver->heightLockActive())
            m_solver->setHeightLock(true, target);
        syncSlidersFromSolver();
        refreshValueLabels();
        applyToView();
        m_heightLabel->setText(
            QStringLiteral("%1 cm").arg(m_solver->currentHeightCm(), 0, 'f', 1));
        emit statusMessage(QStringLiteral("身高 = %1 cm").arg(got, 0, 'f', 1));
        return;
    }

    bool ok = false;
    const double current = m_solver->measureNow(def.id);
    const double target = QInputDialog::getDouble(
        this, QStringLiteral("设置测量值"),
        QStringLiteral("%1 目标值 (cm)：").arg(QString::fromStdString(def.label)),
        current, 20.0, 300.0, 1, &ok);
    if (!ok)
        return;
    m_solver->solveSingle(def.id, target);
    // 锁联动：双击主滑杆 = 更新锁值；双击其他滑杆后若锁激活则补偿回锁值。
    if (m_solver->measureLockActive()) {
        if (def.id == m_solver->measureLockSliderId())
            m_solver->setMeasureLock(def.id, true, target);
        else
            m_solver->enforceMeasureLock();
    }
    // 身高锁：纵向段双击后身高也会变 → 补偿回身高锁值。
    if (m_solver->heightLockActive())
        m_solver->enforceHeightLock();
    syncSlidersFromSolver();
    refreshValueLabels();
    applyToView();
    m_heightLabel->setText(
        QStringLiteral("%1 cm").arg(m_solver->currentHeightCm(), 0, 'f', 1));
    emit statusMessage(QStringLiteral("%1 = %2 cm")
                           .arg(QString::fromStdString(def.label))
                           .arg(target, 0, 'f', 1));
}

void AvatarPanel::syncSlidersFromSolver()
{
    if (!m_solver)
        return;
    m_syncing = true;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const auto& def = m_solver->sliders()[m_rows[i].sliderIdx];
        const double v = m_solver->sliderValue(def.id);
        const int intVal = static_cast<int>(std::lround(v * kSliderRange));
        m_rows[i].slider->setValue(intVal);
    }
    m_syncing = false;
}

void AvatarPanel::refreshValueLabels()
{
    if (!m_solver)
        return;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const auto& def = m_solver->sliders()[m_rows[i].sliderIdx];
        QString text;
        if (def.id == "macrodetails-height/Height") {
            // 身高显示 cm 数值（非百分比）
            text = QStringLiteral("%1 cm").arg(m_solver->currentHeightCm(), 0, 'f', 1);
        } else if (def.id == "macrodetails-proportions/BodyProportions") {
            // 身材比例：腿长占比档位文字（理想/常规/非常规）
            const double v = m_solver->sliderValue(def.id);
            text = (v < 0.34) ? QStringLiteral("非常规")
                 : (v < 0.67) ? QStringLiteral("常规")
                              : QStringLiteral("理想");
        } else if (def.id == "breast/BreastSize") {
            // 罩杯量化显示：差值 = 胸围 − 下胸围，附杯码（替代难读的百分比）
            const double diff =
                m_solver->measureNow("measure/measure-bust-circ-decr|incr") -
                m_solver->measureNow("measure/measure-underbust-circ-decr|incr");
            text = QStringLiteral("%1cm·%2杯")
                       .arg(diff, 0, 'f', 1)
                       .arg(QString::fromLatin1(cad::avatar::AvatarSolver::cupLetter(diff)));
        } else if (def.id == "breast/breast-dist-decr|incr") {
            // 乳房间距量化显示：乳头间距 = 胸点（顶点 1784/8456）水平距离（cm）
            const auto& verts = m_solver->mesh().verts;
            const double d = (verts.size() > 8456)
                                 ? verts[1784].distanceTo(verts[8456]) * 10.0
                                 : 0.0;
            text = QStringLiteral("%1 cm").arg(d, 0, 'f', 1);
        } else if (!def.measureKey.empty()) {
            text = QString::number(m_solver->measureNow(def.id), 'f', 1);
        } else if (def.kind == cad::avatar::SliderKind::Macro ||
                   def.kind == cad::avatar::SliderKind::Ethnic) {
            text = QStringLiteral("%1%").arg(std::lround(m_solver->sliderValue(def.id) * 100.0));
        } else {
            text = QString::number(m_solver->sliderValue(def.id), 'f', 2);
        }
        m_rows[i].valueLabel->setText(text);
    }
}

void AvatarPanel::onSliderValueChanged(int idx)
{
    if (m_syncing || !m_solver || idx < 0 || idx >= static_cast<int>(m_rows.size()))
        return;
    const auto& def = m_solver->sliders()[m_rows[static_cast<size_t>(idx)].sliderIdx];
    const bool isUnit = (def.kind == cad::avatar::SliderKind::Single ||
                         def.kind == cad::avatar::SliderKind::Macro ||
                         def.kind == cad::avatar::SliderKind::Ethnic);
    const double v = static_cast<double>(m_rows[idx].slider->value()) / kSliderRange;
    m_solver->setSliderValue(def.id, v);
    // 锁定中直接拖主滑杆 = 更新锁值（其他滑杆的补偿在松开时 enforce）。
    if (m_solver->measureLockActive() && def.id == m_solver->measureLockSliderId())
        m_solver->setMeasureLock(def.id, true, m_solver->measureNow(def.id));
    // 身高锁：直接拖身高宏 = 更新身高锁值。
    if (m_solver->heightLockActive() && def.id == "macrodetails-height/Height")
        m_solver->setHeightLock(true, m_solver->currentHeightCm());
    // 宏/人种联动会改变其他滑杆值 -> 整表同步显示
    syncSlidersFromSolver();
    // 拖动中跳过测量值刷新（测地线 Dijkstra 昂贵），只更新体型与渲染；
    // 松开时 refreshValueLabels 统一算最终值。
    if (!m_draggingSlider)
        refreshValueLabels();
    applyToView();
    m_heightLabel->setText(
        QStringLiteral("%1 cm").arg(m_solver->currentHeightCm(), 0, 'f', 1));
}

void AvatarPanel::applyToView()
{
    m_view->setMesh(m_model->mesh());
}

void AvatarPanel::resetToDefault()
{
    if (!m_solver)
        return;
    m_solver->resetAll();
    if (m_solver->measureLockActive())
        m_solver->enforceMeasureLock(); // 重置后胸围回到锁值
    if (m_solver->heightLockActive())
        m_solver->enforceHeightLock(); // 重置后身高回到锁值
    syncSlidersFromSolver();
    refreshValueLabels();
    applyToView();
    m_heightLabel->setText(
        QStringLiteral("%1 cm").arg(m_solver->currentHeightCm(), 0, 'f', 1));
    emit statusMessage(QStringLiteral("已重置为默认体型"));
}

void AvatarPanel::exportObj()
{
    if (!m_model)
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出虚拟模特"), QStringLiteral("avatar.obj"),
        QStringLiteral("Wavefront OBJ (*.obj)"));
    if (path.isEmpty())
        return;
    if (exportObjTo(path))
        emit statusMessage(QStringLiteral("已导出：%1").arg(path));
    else
        emit statusMessage(QStringLiteral("导出失败：%1").arg(path));
}

bool AvatarPanel::exportObjTo(const QString& path)
{
    if (!m_model)
        return false;
    return m_model->saveObj(path.toStdString());
}

void AvatarPanel::applyBustMatch()
{
    if (!m_solver)
        return;
    const std::string bust = "measure/measure-bust-circ-decr|incr";
    const std::string under = "measure/measure-underbust-circ-decr|incr";
    const std::string cup = "breast/BreastSize";

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("按胸围匹配自然体态"));
    auto* form = new QFormLayout(&dlg);

    auto* bustSpin = new QDoubleSpinBox(&dlg);
    bustSpin->setRange(50.0, 160.0);
    bustSpin->setDecimals(1);
    bustSpin->setSuffix(QStringLiteral(" cm"));
    bustSpin->setValue(m_solver->measureNow(bust));
    form->addRow(QStringLiteral("胸围"), bustSpin);

    // 下胸围：0 = 自动（胸围 − 12cm，中国女性典型差值 ≈ B 杯）
    auto* underSpin = new QDoubleSpinBox(&dlg);
    underSpin->setRange(0.0, 150.0);
    underSpin->setDecimals(1);
    underSpin->setSuffix(QStringLiteral(" cm"));
    underSpin->setSpecialValueText(QStringLiteral("自动（胸围 − 12）"));
    underSpin->setValue(0.0);
    form->addRow(QStringLiteral("下胸围"), underSpin);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                      Qt::Horizontal, &dlg);
    btns->button(QDialogButtonBox::Ok)->setText(QStringLiteral("匹配"));
    btns->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const double B = bustSpin->value();
    const double U = (underSpin->value() <= 0.0) ? std::max(60.0, B - 12.0)
                                                 : underSpin->value();
    if (U >= B) {
        emit statusMessage(QStringLiteral("下胸围必须小于胸围"));
        return;
    }
    m_solver->solveNaturalBust(bust, B, under, U, cup);
    // 胸围锁联动：锁开着则把锁值更新为本次目标胸围。
    if (m_solver->measureLockActive() && m_solver->measureLockSliderId() == bust)
        m_solver->setMeasureLock(bust, true, B);
    syncSlidersFromSolver();
    refreshValueLabels();
    applyToView();
    m_heightLabel->setText(
        QStringLiteral("%1 cm").arg(m_solver->currentHeightCm(), 0, 'f', 1));

    const double bNow = m_solver->measureNow(bust);
    const double uNow = m_solver->measureNow(under);
    const double diff = bNow - uNow;
    emit statusMessage(
        QStringLiteral("已按胸围匹配自然体态：胸围 %1 · 下胸围 %2 · 差值 %3 cm（%4杯）")
            .arg(bNow, 0, 'f', 1)
            .arg(uNow, 0, 'f', 1)
            .arg(diff, 0, 'f', 1)
            .arg(QString::fromLatin1(cad::avatar::AvatarSolver::cupLetter(diff))));
}

void AvatarPanel::onAnnotationPicked(const cad::avatar::Vec3& pos, int vertexIdx,
                                    int tri, double u, double v,
                                    const std::string& heightKey,
                                    const QString& snapName)
{
    if (!m_view)
        return;
    auto points = m_view->annotations();
    const QString defaultName = QStringLiteral("P%1").arg(points.size() + 1);
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("标注点"), QStringLiteral("名称："),
        QLineEdit::Normal, defaultName, &ok);
    if (!ok)
        return;
    cad::avatar::AnnotationPoint a;
    a.pos = pos;
    a.vertex = vertexIdx;
    a.tri = tri;      // 贴皮肤绑定：命中三角形
    a.bcU = u;        // 重心坐标
    a.bcV = v;
    a.heightKey = heightKey;   // Ctrl 借用水平面：高度约束（Y 跟随该测量线）
    a.name = name.toStdString();
    points.push_back(a);
    m_view->setAnnotations(points);
    refreshAnnotationList();
    QString msg = QStringLiteral("已添加标注点 %1（%.2f, %.2f, %.2f）")
                      .arg(QString::fromStdString(a.name))
                      .arg(pos.x)
                      .arg(pos.y)
                      .arg(pos.z);
    if (!snapName.isEmpty()) {
        if (snapName == QStringLiteral("（未命中测量线）"))
            msg += QStringLiteral(" · 按住 Ctrl 未命中测量线（保持点击位置）");
        else
            msg += QStringLiteral(" · 已借用「%1」水平面").arg(snapName);
    }
    emit statusMessage(msg);
}

void AvatarPanel::onAnnotationMoved(int index, const cad::avatar::Vec3& pos, int vertexIdx)
{
    (void)vertexIdx;
    refreshAnnotationList();
    if (!m_view)
        return;
    const auto& points = m_view->annotations();
    if (index >= 0 && index < static_cast<int>(points.size())) {
        const auto& a = points[static_cast<size_t>(index)];
        emit statusMessage(QStringLiteral("标注点 %1 已调整到（%.2f, %.2f, %.2f）")
                               .arg(QString::fromStdString(a.name))
                               .arg(pos.x)
                               .arg(pos.y)
                               .arg(pos.z));
    }
}

void AvatarPanel::refreshAnnotationList()
{
    if (!m_annotationTree || !m_view)
        return;
    m_annotationTree->clear();
    const auto& points = m_view->annotations();
    int i = 1;
    for (const auto& a : points) {
        auto* item = new QTreeWidgetItem(m_annotationTree);
        item->setText(0, QString::number(i));
        item->setText(1, QString::fromStdString(a.name));
        item->setText(2, QStringLiteral("%1, %2, %3")
                             .arg(a.pos.x, 0, 'f', 2)
                             .arg(a.pos.y, 0, 'f', 2)
                             .arg(a.pos.z, 0, 'f', 2));
        ++i;
    }
}

void AvatarPanel::renameSelectedAnnotation()
{
    if (!m_annotationTree || !m_view)
        return;
    auto* item = m_annotationTree->currentItem();
    if (!item)
        return;
    const int idx = m_annotationTree->indexOfTopLevelItem(item);
    auto points = m_view->annotations();
    if (idx < 0 || idx >= static_cast<int>(points.size()))
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("重命名"), QStringLiteral("名称："),
        QLineEdit::Normal, QString::fromStdString(points[static_cast<size_t>(idx)].name), &ok);
    if (!ok)
        return;
    points[static_cast<size_t>(idx)].name = name.toStdString();
    m_view->setAnnotations(points);
    refreshAnnotationList();
}

void AvatarPanel::removeSelectedAnnotation()
{
    if (!m_annotationTree || !m_view)
        return;
    auto* item = m_annotationTree->currentItem();
    if (!item)
        return;
    const int idx = m_annotationTree->indexOfTopLevelItem(item);
    auto points = m_view->annotations();
    if (idx < 0 || idx >= static_cast<int>(points.size()))
        return;
    points.erase(points.begin() + idx);
    m_view->setAnnotations(points);
    // 同步连线：删除引用该点的线，其余索引 > idx 的线前移
    auto lines = m_view->annotationLines();
    std::vector<cad::avatar::AnnotationLine> kept;
    for (const auto& l : lines) {
        if (l.a == idx || l.b == idx)
            continue; // 引用被删点的线丢弃
        cad::avatar::AnnotationLine nl = l;
        if (nl.a > idx) --nl.a;
        if (nl.b > idx) --nl.b;
        kept.push_back(nl);
    }
    m_view->setAnnotationLines(kept);
    refreshAnnotationList();
}

void AvatarPanel::clearAnnotations()
{
    if (!m_view)
        return;
    m_view->setAnnotations({});
    m_view->setAnnotationLines({}); // 连线随标注点一起清空
    refreshAnnotationList();
}

void AvatarPanel::saveAnnotations()
{
    if (!m_view)
        return;
    const auto& points = m_view->annotations();
    if (points.empty()) {
        emit statusMessage(QStringLiteral("无标注点可保存"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存标注点"), QStringLiteral("annotations.json"),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;

    std::string json = "{\n  \"source\": \"avatar-annotation\",\n  \"points\": [\n";
    for (size_t i = 0; i < points.size(); ++i) {
        json += "    {\"name\": \"" + escapeJson(points[i].name) + "\", \"pos\": [" +
                std::to_string(points[i].pos.x) + ", " +
                std::to_string(points[i].pos.y) + ", " +
                std::to_string(points[i].pos.z) + "], \"vertex\": " +
                std::to_string(points[i].vertex) +
                ", \"tri\": " + std::to_string(points[i].tri) +
                ", \"bcU\": " + std::to_string(points[i].bcU) +
                ", \"bcV\": " + std::to_string(points[i].bcV) +
                ", \"heightKey\": \"" + escapeJson(points[i].heightKey) + "\"}";
        json += (i + 1 < points.size()) ? ",\n" : "\n";
    }
    json += "  ]\n}\n";

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit statusMessage(QStringLiteral("保存标注点失败：%1").arg(path));
        return;
    }
    f.write(json.data(), static_cast<qint64>(json.size()));
    f.close();
    emit statusMessage(QStringLiteral("已保存 %1 个标注点到 %2")
                           .arg(points.size())
                           .arg(path));
}

void AvatarPanel::loadAnnotations()
{
    if (!m_view)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载标注点"), QString(), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    try {
        const cad::avatar::JsonValue root = cad::avatar::parseJsonFile(path.toStdString());
        const cad::avatar::JsonValue* pts = root.find("points");
        if (!pts || pts->type() != cad::avatar::JsonValue::Type::Array)
            throw std::runtime_error("missing points array");
        std::vector<cad::avatar::AnnotationPoint> loaded;
        for (const auto& e : pts->asArray()) {
            const cad::avatar::JsonValue* name = e.find("name");
            if (!name)
                throw std::runtime_error("point missing name");
            cad::avatar::AnnotationPoint a;
            a.name = name->asString();
            // pos（世界坐标，可选）：缺省时回退用 vertex 从当前网格推导
            const cad::avatar::JsonValue* pos = e.find("pos");
            if (pos && pos->type() == cad::avatar::JsonValue::Type::Array &&
                pos->asArray().size() == 3) {
                a.pos = cad::avatar::Vec3(pos->asArray()[0].asNumber(),
                                          pos->asArray()[1].asNumber(),
                                          pos->asArray()[2].asNumber());
            }
            const cad::avatar::JsonValue* vertex = e.find("vertex");
            if (vertex) {
                a.vertex = static_cast<int>(vertex->asInt64());
                if (a.pos == cad::avatar::Vec3::zero() && m_model &&
                    a.vertex >= 0 && a.vertex < static_cast<int>(m_model->mesh().verts.size()))
                    a.pos = m_model->mesh().verts[static_cast<size_t>(a.vertex)];
            }
            // 贴皮肤绑定（tri + 重心坐标）
            const cad::avatar::JsonValue* tri = e.find("tri");
            const cad::avatar::JsonValue* bcU = e.find("bcU");
            const cad::avatar::JsonValue* bcV = e.find("bcV");
            if (tri) a.tri = static_cast<int>(tri->asInt64());
            if (bcU) a.bcU = bcU->asNumber();
            if (bcV) a.bcV = bcV->asNumber();
            // 借用水平面的高度约束（heightKey）
            const cad::avatar::JsonValue* hk = e.find("heightKey");
            if (hk) a.heightKey = hk->asString();
            loaded.push_back(a);
        }
        m_view->setAnnotations(loaded);
        refreshAnnotationList();
        emit statusMessage(QStringLiteral("已加载 %1 个标注点").arg(loaded.size()));
    } catch (const std::exception& e) {
        emit statusMessage(QStringLiteral("加载标注点失败：%1").arg(QString::fromUtf8(e.what())));
    }
}

} // namespace cad::ui
