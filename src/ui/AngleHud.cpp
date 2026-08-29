#include "ui/AngleHud.h"

#include "canvas/CanvasScene.h"
#include "canvas/CanvasStyle.h"

#include <QGraphicsView>
#include <QHBoxLayout>
#include "ElaText.h"
#include "ElaLineEdit.h"
#include "ElaPushButton.h"
#include <QKeyEvent>
#include <QEvent>

namespace cad::ui {

AngleHud::AngleHud(QWidget* viewport, const CanvasStyle* style)
    : QWidget(viewport)
    , m_style(style)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    // Palette comes from the owning canvas scene's CanvasStyle so the HUD
    // follows the active theme (pattern-workbench rule: no hardcoded colors).
    // L7 (TOOL_SYSTEM_AUDIT): 构造时直接传入 CanvasStyle*, 去掉旧实现沿
    // viewport->parentWidget() 反查的父链耦合 (一断链就静默回退硬编码色)。
    QColor hudBg = QColor(30, 38, 46, 235);
    QColor hudFg = QColor(154, 163, 173);
    QColor hudBorder = QColor(255, 255, 255, 35);
    QColor validCol = QColor(43, 179, 163);   // teal family
    QColor invalidCol = QColor(240, 101, 90); // danger family
    if (m_style) {
        hudBg = m_style->hudBackground;
        hudFg = m_style->hudText;
        hudBorder = m_style->crosshairColor;
        validCol = m_style->snapPointColor;
        invalidCol = m_style->snapIndicatorColor;
    }

    // Mode toggle button (compact, acts as caption + switch).
    m_btnToggle = new ElaPushButton(this);
    m_btnToggle->setCursor(Qt::PointingHandCursor);
    m_btnToggle->setFixedHeight(22);
    m_btnToggle->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;color:%2;"
        "border:1px solid %3;border-radius:4px;"
        "font-size:11px;padding:2px 6px;}")
        .arg(hudBg.name(QColor::HexArgb), validCol.name(), hudBorder.name(QColor::HexArgb)));

    m_lblCaption = new ElaText(QString(), 13, this);
    m_edit = new ElaLineEdit(this);
    m_edit->setFixedWidth(150);
    m_lblUnit = new ElaText(QString(), 13, this);

    layout->addWidget(m_btnToggle);
    layout->addWidget(m_lblCaption);
    layout->addWidget(m_edit);
    layout->addWidget(m_lblUnit);

    // Floating overlay look (AutoCAD dynamic-input style) — theme-aware.
    // 圆角纪律 (ui-redesign §07): 功能圆角上限 4px (原 8px)。
    setStyleSheet(QStringLiteral(
        "AngleHud{background:%1;"
        "border:1px solid %2;border-radius:4px;}"
        "QLabel{color:%3;font-size:12px;}")
        .arg(hudBg.name(QColor::HexArgb), hudBorder.name(QColor::HexArgb),
             hudFg.name()));
    setValid(true);
    applyModeVisuals();

    // Toggle mode on button click.
    connect(m_btnToggle, &QPushButton::clicked, [this] {
        m_mode = (m_mode == cad::param::RotationMode::Angle)
                     ? cad::param::RotationMode::ArcLength
                     : cad::param::RotationMode::Angle;
        applyModeVisuals();
        if (onModeChanged) onModeChanged(m_mode);
    });

    // Esc must work no matter which child holds keyboard focus.
    // N4 (TOOL_SYSTEM_AUDIT 复核 2026-08-29): 模式切换按钮此前漏装过滤器,
    // 焦点落在它上面时按单字母仍会触发工具快捷键 (H1 的口子没堵全)。按钮
    // 本身没有文本输入, 风险极小, 但既然 eventFilter 已经按 (o == m_edit
    // || o == this) 判定, 把三个可聚焦子件都装上才是一致的。
    m_edit->installEventFilter(this);
    m_btnToggle->installEventFilter(this);
    installEventFilter(this);
    connect(m_edit, &QLineEdit::returnPressed,
            [this] { if (onCommit) onCommit(); });
    connect(m_edit, &QLineEdit::textChanged,
            [this](const QString& t) { if (onTextChanged) onTextChanged(t); });
}

void AngleHud::setMode(cad::param::RotationMode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    applyModeVisuals();
}

void AngleHud::setCaption(const QString& text)
{
    if (m_captionOverride == text) return;
    m_captionOverride = text;
    applyModeVisuals();
}

QString AngleHud::captionText() const
{
    return m_lblCaption ? m_lblCaption->text() : QString();
}

void AngleHud::applyModeVisuals()
{
    if (m_mode == cad::param::RotationMode::Angle) {
        m_btnToggle->setText(QStringLiteral("\xe2\x88\xa0"));  // ∠
        // 默认标签 = 跟随角度（跟随线相对基准线的夹角）；自由线场景由
        // ToolRotate 通过 setCaption 覆盖为“绝对角度”/“相对角度”。
        m_lblCaption->setText(m_captionOverride.isEmpty()
            ? QString::fromUtf8("跟随角度") : m_captionOverride);
        m_edit->setPlaceholderText(QStringLiteral(
            "\xe5\xba\xa6\xe6\x95\xb0\xe6\x88\x96\xe5\x85\xac\xe5\xbc\x8f\xef\xbc\x8c\xe5\xa6\x82 b/4+5"));  // 度数或公式，如 b/4+5
    } else {
        m_btnToggle->setText(QStringLiteral("\xe2\x8c\x92"));  // ⌒
        m_lblCaption->setText(QStringLiteral("\xe5\xbc\xa7\xe9\x95\xbf"));  // 弧长
        m_edit->setPlaceholderText(QStringLiteral(
            "\xe9\x95\xbf\xe5\xba\xa6\xe6\x88\x96\xe5\x85\xac\xe5\xbc\x8f\xef\xbc\x8c\xe5\xa6\x82 sleeve/2"));  // 长度或公式，如 sleeve/2
    }
    // 单位标签: 错误短文优先 (M8), 无错误才显示单位。
    applyErrorVisual();
}

void AngleHud::setValid(bool ok)
{
    // Theme-aware valid/invalid colors (L7: 构造时传入的 CanvasStyle* 直取,
    // 不再沿父链反查 — 旧实现静默回退硬编码色, 曾有暗色下不可读的前科)。
    QColor validCol = QColor(43, 179, 163);
    QColor invalidCol = QColor(240, 101, 90);
    QColor editBg = QColor(255, 255, 255, 245);
    QColor editFg = QColor(232, 234, 237);
    if (m_style) {
        validCol = m_style->snapPointColor;
        invalidCol = m_style->snapIndicatorColor;
        editBg = m_style->hudBackground.lighter(115);
        editFg = m_style->hudText;
    }
    m_edit->setStyleSheet(ok
        ? QStringLiteral("QLineEdit{border:1px solid %1;border-radius:4px;"
                         "padding:3px 6px;background:%2;"
                         "color:%3;selection-background-color:%1;}")
              .arg(validCol.name(), editBg.name(QColor::HexArgb), editFg.name())
        : QStringLiteral("QLineEdit{border:1px solid %1;border-radius:4px;"
                         "padding:3px 6px;background:%2;color:%1;}")
              .arg(invalidCol.name(), errorWash(invalidCol)));
}

void AngleHud::setError(const QString& msg)
{
    if (m_errorText == msg) return;
    m_errorText = msg;
    applyErrorVisual();
}

void AngleHud::applyErrorVisual()
{
    if (!m_lblUnit) return;
    m_lblUnit->setText(m_errorText.isEmpty()
        ? (m_mode == cad::param::RotationMode::Angle
               ? QStringLiteral("\xc2\xb0")   // °
               : QStringLiteral("cm"))
        : QStringLiteral("\xe2\x9a\xa0 %1").arg(m_errorText));  // ⚠ 原因
}

// Danger wash used by the invalid-state edit background. Same 12% alpha
// tinting rule as Theme::badgeStyle, kept here because the hue itself comes
// from CanvasStyle (canvas-side tokens), not the UI token table.
QString AngleHud::errorWash(const QColor& fg)
{
    QColor wash = fg;
    wash.setAlphaF(0.12f);
    return wash.name(QColor::HexArgb);
}

bool AngleHud::eventFilter(QObject* o, QEvent* e)
{
    // 只看自己与可聚焦子件 (输入框 / 模式切换按钮), 别的手下对象不拦。
    const bool watched = (o == m_edit || o == m_btnToggle || o == this);
    if (!watched)
        return QWidget::eventFilter(o, e);

    // 输入包含 (同 SegmentEditBar / SmartPenPreInputBar 范式): 编辑期内吞掉
    // 全部 ShortcutOverride, 阻断主窗口单字母工具快捷键 (V/L/C/R/B/I/A/H,
    // ApplicationShortcut 与焦点无关) 抢键 —— 否则照着占位符输推荐公式
    // "b/4+5" 的第一个字符 b 就会触发打断工具, 旋转会话被静默销毁
    // (TOOL_SYSTEM_AUDIT H1, 2026-08-29).
    //
    // 注: 连 Ctrl+Z / Ctrl+S 这类组合键也一并吞掉, 与 SmartPenPreInputBar
    // 的既有约定一致 (它注释里明写了 Ctrl+Z / Ctrl+Y)。代价是 HUD 自动聚焦
    // 期间按 Ctrl+Z 不会撤销文档; 若要保留撤销, 这里按修饰键收窄即可。
    if (e->type() == QEvent::ShortcutOverride) {
        static_cast<QKeyEvent*>(e)->accept();
        return true;
    }
    if (e->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
            if (onCancel) onCancel();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}

} // namespace cad::ui
