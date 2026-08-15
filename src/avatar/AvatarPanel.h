#pragma once

#include "avatar/AvatarVec3.h"

#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QSlider;
class QToolButton;
class QTreeWidget;
class QHBoxLayout;
class QVBoxLayout;
class QStackedWidget;
class ElaTabBar;

namespace cad::avatar {
class AvatarModel;
class AvatarSolver;
class AvatarView3D;
class MeasureSystem;
struct SectionPoint;
} // namespace cad::avatar

namespace cad::ui {

/// 3D 虚拟模特控制面板：MakeHuman 滑杆（按分类折叠，hidden 项不显示）
/// + 3D 视口。宏滑杆（性别/年龄/体重…）0-100%，人种滑杆联动归一化；
/// 测量滑杆双击可输入目标 cm（solveSingle 二分逼近）。
/// 所有调整实时增量作用于 AvatarModel 并刷新视口。
class AvatarPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AvatarPanel(QWidget* parent = nullptr);
    ~AvatarPanel() override;

    /// 3D 视口（供外部接线/布局）。
    cad::avatar::AvatarView3D* view() const { return m_view; }

    /// 求解器（测试与外部接线用）。
    cad::avatar::AvatarSolver* solver() const { return m_solver.get(); }

    /// 加载默认数据（base.obj + targets + 测量链 + 滑杆定义），
    /// 并应用 MH 默认体型（宏默认值）。失败返回 false（assets 缺失等）。
    bool loadDefault();

    /// 导出当前网格到指定路径（不带对话框，测试用）。成功返回 true。
    bool exportObjTo(const QString& path);

signals:
    /// 操作反馈（导出成功/失败、加载失败等）。
    void statusMessage(const QString& msg);

public slots:
    /// 重置为 MH 默认体型（宏默认 + 滑杆归零）。
    void resetToDefault();

private slots:
    void exportObj();
    void applyBustMatch(); // 「按胸围匹配」：输入胸围（可选下胸围）→ 自然罩杯联合匹配

private:
    void rebuildGroups();
    void rebuildAdjustPage(); // 「调整」标签页：上半身组（胸围/下胸围/罩杯）快捷复制
    QHBoxLayout* buildSliderRow(size_t sliderIdx, QWidget* parent,
                                const QString& objPrefix); // 构建一个滑杆行并登记到 m_rows
    size_t sliderIdxOf(const std::string& id) const;
    void onSliderValueChanged(int idx);
    void onSliderDoubleClicked(int idx);
    void syncSlidersFromSolver();
    void refreshValueLabels();
    void applyToView();

    // 标注点管理
    void onAnnotationPicked(const cad::avatar::Vec3& pos, int vertexIdx,
                            int tri, double u, double v,
                            const std::string& heightKey,
                            const QString& snapName);
    void onAnnotationMoved(int index, const cad::avatar::Vec3& pos, int vertexIdx);
    void refreshAnnotationList();
    void renameSelectedAnnotation();
    void removeSelectedAnnotation();
    void clearAnnotations();
    void saveAnnotations();
    void loadAnnotations();

    bool eventFilter(QObject* watched, QEvent* event) override;

    std::unique_ptr<cad::avatar::AvatarModel> m_model;
    std::unique_ptr<cad::avatar::MeasureSystem> m_measures;
    std::unique_ptr<cad::avatar::AvatarSolver> m_solver;
    cad::avatar::AvatarView3D* m_view = nullptr;

    QLabel* m_heightLabel = nullptr;   ///< 状态行：身高
    QVBoxLayout* m_groupsLayout = nullptr;
    QVBoxLayout* m_adjustLayout = nullptr; ///< 「调整」标签页折叠组容器
    ElaTabBar* m_tabBar = nullptr;          ///< 顶部标签切换（调整项/测量点）
    QStackedWidget* m_stack = nullptr;      ///< 标签对应的堆叠页面
    QToolButton* m_annotationModeBtn = nullptr; ///< 标注模式开关
    QToolButton* m_lineModeBtn = nullptr;       ///< 画线模式开关（点两点连线）
    QTreeWidget* m_annotationTree = nullptr;    ///< 标注点列表（序号/名称/顶点）
    QToolButton* m_bustLockBtn = nullptr;       ///< 胸围锁开关（「调整」页胸围行）
    QToolButton* m_heightLockBtn = nullptr;     ///< 身高锁开关（「全身」组身高行）
    struct SliderRow {
        QSlider* slider = nullptr;
        QLabel* valueLabel = nullptr;
        size_t sliderIdx = 0; ///< 在 m_solver->sliders() 中的原始下标
    };
    struct GroupSection {
        QToolButton* toggle = nullptr;
        QWidget* content = nullptr;
    };
    std::vector<SliderRow> m_rows;
    std::vector<GroupSection> m_groups;
    bool m_syncing = false; ///< 程序回写滑杆时防重入
    bool m_draggingSlider = false; ///< 拖动体型滑杆中（延迟测量计算，去卡顿）
};

} // namespace cad::ui
