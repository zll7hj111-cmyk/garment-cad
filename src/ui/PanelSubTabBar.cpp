#include "PanelSubTabBar.h"

#include <QMouseEvent>
#include <QPainter>

#include "Theme.h"

namespace cad::ui {

PanelSubTabBar::PanelSubTabBar(QWidget* parent)
    : QTabBar(parent)
{
    // 30px 高、均分整行、禁滚动箭头/裁剪 (同旧 ElaTabBar 配置; Ela 默认
    // sizeHint ~220px 会把 4 枚标签挤出窄窗, QTabBar 无此问题)。
    setExpanding(true);
    setUsesScrollButtons(false);
    setElideMode(Qt::ElideNone);
    setDrawBase(false);
    setTabsClosable(false);
    setMovable(false);
    setAcceptDrops(false);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(30);
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    QFont f = font();
    f.setPixelSize(12);
    setFont(f);
}

void PanelSubTabBar::setTabProfile(int index, const QColor& accent, bool readOnly)
{
    if (m_accents.size() != count())
        m_accents.resize(count());
    if (m_readOnly.size() != count())
        m_readOnly.resize(count());
    if (index < 0 || index >= m_accents.size())
        return;
    m_accents[index] = accent;
    m_readOnly[index] = readOnly;
    update();
}

void PanelSubTabBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const ThemeTokens& tk = Theme::tokens();

    for (int i = 0; i < count(); ++i) {
        const QRect r = tabRect(i);
        const bool sel = (i == currentIndex());
        const bool hov = (i == m_hoverIndex);
        const QColor accent =
            (i < m_accents.size() && m_accents[i].isValid()) ? m_accents[i]
                                                             : tk.accent;
        const bool ro = (i < m_readOnly.size()) && m_readOnly[i];

        // 底色: 激活 = 面板白, 悬停 = surface2, 其余透明 (§6.3)。
        if (sel)
            p.fillRect(r, tk.surface);
        else if (hov)
            p.fillRect(r, tk.surface2);

        // 文字: 只读恒 text2 不加粗; 其余 hover/激活 text1, 激活 600 字重。
        QColor text = ro ? tk.text2 : ((sel || hov) ? tk.text1 : tk.text2);
        QFont f = font();
        if (sel && !ro)
            f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(text);
        p.drawText(r, Qt::AlignCenter, tabText(i));

        // 激活下划线 = 2px piece 类型色 (内容层信号)。
        if (sel)
            p.fillRect(r.left(), r.bottom() - 1, r.width(), 2, accent);
    }
}

void PanelSubTabBar::mouseMoveEvent(QMouseEvent* event)
{
    syncHover(event->pos());
    QTabBar::mouseMoveEvent(event);
}

void PanelSubTabBar::leaveEvent(QEvent* event)
{
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        update();
    }
    QTabBar::leaveEvent(event);
}

void PanelSubTabBar::syncHover(const QPoint& pos)
{
    const int idx = tabAt(pos);
    if (idx != m_hoverIndex) {
        m_hoverIndex = idx;
        update();
    }
}

} // namespace cad::ui
