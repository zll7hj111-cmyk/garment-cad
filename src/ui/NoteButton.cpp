#include "ui/NoteButton.h"

#include <QApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScreen>
#include <QToolTip>
#include <QVBoxLayout>

#include "ui/Theme.h"
#include "ui/TooltipFormatter.h"

namespace cad::ui {
namespace {

constexpr int kBtnSize  = 26;   ///< 按钮边长 (与 kFieldH 行高一致)。
constexpr int kEditW    = 216;  ///< 编辑区宽。
constexpr int kEditH    = 104;  ///< 编辑区高。
constexpr int kFold     = 5;    ///< 便利贴右下折角边长。
constexpr int kCloseMs  = 300;  ///< 关闭后忽略重开的窗口 (见 m_lastClose)。

} // namespace

NoteButton::NoteButton(QWidget* parent)
    : QPushButton(parent)
{
    setFlat(true);
    setFixedSize(kBtnSize, kBtnSize);
    setCursor(Qt::PointingHandCursor);
    setToolTip(cad::ui::TooltipFormatter::action(
        QStringLiteral("附加注释"),
        QStringLiteral("点击打开便签，添加详细工艺或测量说明")));
    connect(this, &QPushButton::clicked, this, [this] { openEditor(); });
}

void NoteButton::setNote(const QString& text)
{
    if (m_popup) return;  // 用户正在编辑: 不打断输入
    if (m_note == text) return;
    m_note = text;
    refreshVisual();
}

void NoteButton::setPlaceholder(const QString& ph)
{
    m_placeholder = ph;
}

QSize NoteButton::sizeHint() const
{
    return {kBtnSize, kBtnSize};
}

void NoteButton::refreshVisual()
{
    const bool has = !m_note.trimmed().isEmpty();
    setToolTip(has
        ? cad::ui::TooltipFormatter::status(QStringLiteral("注释内容"), m_note, false)
        : cad::ui::TooltipFormatter::action(QStringLiteral("附加注释"), QStringLiteral("点击打开便签，添加详细工艺或测量说明")));
    update();
}

// ── 便利贴图标自绘 ─────────────────────────────────────────────────────────
// 一个矩形 + 右下折角。空态只描边 (text3, 退到背景里); 有内容时填主题强调
// 色, 让"这条挂了注释"在整排控件里一眼可辨。
void NoteButton::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::tokens();
    const bool has = !m_note.trimmed().isEmpty();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal padX = qBound(3.0, width() * 0.18, 5.0);
    const qreal padY = qBound(2.5, height() * 0.15, 4.0);
    const qreal fold = qBound(3.5, qMin(width(), height()) * 0.20, 5.0);
    const qreal l = padX, tp = padY, r = width() - padX, b = height() - padY;

    // 主体: 矩形缺右下角, 斜边即折痕。
    QPainterPath body;
    body.moveTo(l, tp);
    body.lineTo(r, tp);
    body.lineTo(r, b - fold);
    body.lineTo(r - fold, b);
    body.lineTo(l, b);
    body.closeSubpath();

    p.setPen(QPen(has ? t.warning : t.text3, 1.2));
    p.setBrush(has ? QBrush(t.accentStrong) : Qt::NoBrush);
    p.drawPath(body);

    // 折角小三角: 有内容时填浅一档的洗底, 空态不填 (保持描边感)。
    QPainterPath foldPath;
    foldPath.moveTo(r - fold, b);
    foldPath.lineTo(r, b - fold);
    foldPath.lineTo(r - fold, b - fold);
    foldPath.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(has ? QBrush(t.accentTint) : Qt::NoBrush);
    p.drawPath(foldPath);
    p.setPen(QPen(has ? t.warning : t.text3, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QLineF(r - fold, b - fold, r - fold, b));
}

void NoteButton::openEditor()
{
    // 点击外部导致的自动关闭会紧接着派发 click —— 这段窗口内不再重开。
    if (m_lastClose.isValid() && m_lastClose.elapsed() < kCloseMs) return;
    if (m_popup) { m_popup->close(); return; }

    const auto& t = Theme::tokens();

    auto* popup = new QFrame(nullptr, Qt::Popup | Qt::FramelessWindowHint);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setObjectName(QStringLiteral("gcadNotePopup"));
    popup->setStyleSheet(
        QStringLiteral("QFrame#gcadNotePopup { background:%1; border:1px solid %2;"
                       " border-radius:8px; }")
            .arg(t.accentTint.name(), t.accentStrong.name()));

    auto* lay = new QVBoxLayout(popup);
    lay->setContentsMargins(8, 8, 8, 6);
    lay->setSpacing(4);

    auto* edit = new QPlainTextEdit(popup);
    edit->setPlainText(m_note);
    edit->setPlaceholderText(m_placeholder.isEmpty()
                                 ? QString::fromUtf8("写点说明…")
                                 : m_placeholder);
    edit->setFixedSize(kEditW, kEditH);
    edit->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background:transparent; border:none;"
                       " color:%1; font-size:12px; }")
            .arg(t.text1.name()));
    edit->setTabChangesFocus(false);
    lay->addWidget(edit);

    auto* hint = new QLabel(QString::fromUtf8("Esc 或点击别处保存"), popup);
    hint->setStyleSheet(
        QStringLiteral("color:%1; font-size:11px;").arg(t.text3.name()));
    lay->addWidget(hint, 0, Qt::AlignRight);

    // Esc 直接关闭 (Qt::Popup 对 Esc 的处理依赖平台, 这里显式兜底)。
    edit->installEventFilter(this);

    m_popup = popup;
    m_edit  = edit;
    popup->installEventFilter(this);

    // 定位: 按钮正下方水平居中, 并钳制在屏幕内 (贴边按钮不会弹出界)。
    QPoint g = mapToGlobal(QPoint(width() / 2, height() + 4));
    g.setX(g.x() - popup->sizeHint().width() / 2);
    if (const QScreen* s = QGuiApplication::screenAt(mapToGlobal(rect().center()))) {
        const QRect avail = s->availableGeometry();
        g.setX(qBound(avail.left() + 4, g.x(),
                      avail.right() - popup->sizeHint().width() - 4));
        if (g.y() + popup->sizeHint().height() > avail.bottom())
            g.setY(mapToGlobal(QPoint(0, 0)).y() - popup->sizeHint().height() - 4);
    }
    popup->move(g);
    popup->show();

    edit->setFocus();
    auto c = edit->textCursor();
    c.movePosition(QTextCursor::End);
    edit->setTextCursor(c);
}

bool NoteButton::eventFilter(QObject* obj, QEvent* event)
{
    // Esc / 失焦 / 点击外部 都汇到 QEvent::Close —— 统一在这里提交。
    if (obj == m_popup && event->type() == QEvent::Close) {
        const QString text = m_edit ? m_edit->toPlainText() : m_note;
        m_lastClose.start();
        m_popup = nullptr;
        m_edit  = nullptr;
        if (text != m_note) {
            m_note = text;
            refreshVisual();
            emit noteEdited(text);
        }
        return false;
    }
    if (obj == m_edit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            if (m_popup) m_popup->close();
            return true;
        }
    }
    return QPushButton::eventFilter(obj, event);
}

} // namespace cad::ui
