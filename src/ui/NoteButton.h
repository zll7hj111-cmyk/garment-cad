#pragma once

#include <QPushButton>

#include <QElapsedTimer>
#include <QPointer>
#include <QString>

class QFrame;
class QPlainTextEdit;

namespace cad::ui {

/// 便利贴注释按钮 (可复用控件, 2026-12): 名称 / 变量等输入框右侧的小按钮,
/// 把长文本注释收进一个悬浮便利贴 —— 替代超长的单行备注输入框。
///
/// 四态:
///   · 空态   → 灰描边便利贴图标, 不占额外空间
///   · 有内容 → 图标填主题强调色, 一眼看出这条挂了注释
///   · 悬停   → 气泡显示注释全文 (不点开也能看)
///   · 点击   → 弹出便利贴浮层, 可写多行; 失焦 / Esc 关闭并提交
///
/// 控件**不碰模型、不推 undo 命令** —— 只发 noteEdited(text), 提交时机交给
/// 宿主。这是它能一处实现、多处复用的前提:
///   · LinePropertyDialog 是会话制 (打开时快照 → 确认时统一推一条
///     SetLinePropertiesCommand), 故收到信号后只写回模型、不自行 push;
///   · 变量面板等即时提交场景可直接连 push。
///
/// 配色全部走 cad::ui::Theme::tokens() —— accentTint 底 / accentStrong 边 /
/// warning 图标, 明暗两套主题自动切换, 不硬编码任何色值。
class NoteButton : public QPushButton
{
    Q_OBJECT

public:
    explicit NoteButton(QWidget* parent = nullptr);

    [[nodiscard]] QString note() const { return m_note; }

    /// 外部同步 (对话框 refresh 等)。**不发** noteEdited —— 这是把控件拉回
    /// 模型状态, 不能当成一次用户编辑。
    void setNote(const QString& text);

    /// 浮层内未输入时的占位提示 (默认「写点说明…」)。
    void setPlaceholder(const QString& ph);

    [[nodiscard]] QSize sizeHint() const override;

signals:
    /// 用户在便利贴里改完并关闭浮层。仅内容真变化时才发。
    void noteEdited(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void openEditor();
    void refreshVisual();

    QString m_note;
    QString m_placeholder;
    QPointer<QFrame> m_popup;          ///< 便利贴浮层 (关闭即销毁)。
    QPointer<QPlainTextEdit> m_edit;   ///< 浮层内的编辑区。

    /// Popup 因点击外部自动关闭后, Qt 仍会把这次点击派发给按钮 —— 若不拦,
    /// 点按钮会"关了又立刻开", 永远关不掉。关闭后 300ms 内忽略 openEditor。
    QElapsedTimer m_lastClose;
};

} // namespace cad::ui
