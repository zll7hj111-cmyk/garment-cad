#include "CompoundChip.h"

#include "ElaText.h"
#include "ElaLineEdit.h"
#include "Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QClipboard>
#include <QApplication>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFontMetrics>
#include <QSignalBlocker>
#include <QRegularExpression>

namespace cad::ui {

// ============================================================
// CompoundChipLabel
// ============================================================
class CompoundChipLabel : public ElaText
{
public:
    enum class Part { Ref, Name };

    CompoundChipLabel(Part part, CompoundChip* host, QWidget* parent = nullptr)
        : ElaText(QString(), 12, parent)
        , m_part(part)
        , m_host(host)
    {
        setAttribute(Qt::WA_Hover, true);
    }

    void setHovered(bool hovered)
    {
        if (m_hovered == hovered) return;
        m_hovered = hovered;
        update();
    }

    void setPlaceholder(bool ph)
    {
        if (m_isPlaceholder == ph) return;
        m_isPlaceholder = ph;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const auto& t = cad::ui::Theme::tokens();
        const bool dark = cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark;
        const QColor borderColor = dark ? QColor(0x4E, 0x58, 0x66)
                                        : QColor(0x9A, 0xA4, 0xB2);

        const qreal r = 3.0;
        const QRectF rect(0.5, 0.5, width() - 1.0, height() - 1.0);

        p.setPen(QPen(borderColor, 1));

        if (m_part == Part::Ref) {
            // Ref part: solid slightly tinted background, left rounded corners
            p.setBrush(m_hovered ? t.surface3 : t.surface2);
            QPainterPath path;
            path.moveTo(rect.right(), rect.top());
            path.lineTo(rect.left() + r, rect.top());
            path.arcTo(QRectF(rect.left(), rect.top(), 2 * r, 2 * r), 90, 90);
            path.lineTo(rect.left(), rect.bottom() - r);
            path.arcTo(QRectF(rect.left(), rect.bottom() - 2 * r, 2 * r, 2 * r), 180, 90);
            path.lineTo(rect.right(), rect.bottom());
            path.closeSubpath();
            p.drawPath(path);
        } else {
            // Name part: right rounded corners only, shared left divider
            p.setBrush(m_hovered ? t.surface2 : t.surface);
            QPainterPath path;
            path.moveTo(rect.left(), rect.top());
            path.lineTo(rect.right() - r, rect.top());
            path.arcTo(QRectF(rect.right() - 2 * r, rect.top(), 2 * r, 2 * r), 90, -90);
            path.lineTo(rect.right(), rect.bottom() - r);
            path.arcTo(QRectF(rect.right() - 2 * r, rect.bottom() - 2 * r, 2 * r, 2 * r), 0, -90);
            path.lineTo(rect.left(), rect.bottom());
            path.closeSubpath();
            p.drawPath(path);
        }

        // Draw label text
        ElaText::paintEvent(event);
    }

private:
    Part m_part;
    CompoundChip* m_host = nullptr;
    bool m_hovered = false;
    bool m_isPlaceholder = false;
};

// ============================================================
// CompoundChip
// ============================================================

bool CompoundChip::tryParseCompound(const QString& input, QString& outRef, QString& outName)
{
    const QString s = input.trimmed();
    if (s.isEmpty()) return false;

    // Matches: "B 胸围", "B:胸围", "B：胸围", "B=胸围", "V1_腰围"
    static const QRegularExpression re(
        QStringLiteral(R"(^([a-zA-Z][a-zA-Z0-9_]{0,11})[\s:：=]+(.+)$)"));
    const auto match = re.match(s);
    if (match.hasMatch()) {
        outRef = match.captured(1).trimmed().toUpper();
        outName = match.captured(2).trimmed();
        return !outRef.isEmpty() && !outName.isEmpty();
    }
    return false;
}

CompoundChip::CompoundChip(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(22);
    setMinimumWidth(80);

    // Ref slot (left)
    m_refLabel = new CompoundChipLabel(CompoundChipLabel::Part::Ref, this, this);
    m_refLabel->setAlignment(Qt::AlignCenter);
    m_refLabel->setCursor(Qt::PointingHandCursor);
    m_refLabel->installEventFilter(this);

    // Name slot (right)
    m_nameLabel = new CompoundChipLabel(CompoundChipLabel::Part::Name, this, this);
    m_nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_nameLabel->setCursor(Qt::PointingHandCursor);
    m_nameLabel->installEventFilter(this);

    // Edit overlays
    m_refEdit = new ElaLineEdit(this);
    m_refEdit->setMinimumHeight(0);
    m_refEdit->setMaximumHeight(QWIDGETSIZE_MAX);
    m_refEdit->setAlignment(Qt::AlignCenter);
    m_refEdit->setStyleSheet(QStringLiteral(
        "font-family: %1; font-size: %2px; font-weight: 600;")
        .arg(cad::ui::ThemeTokens::kMonospaceFamily,
             QString::number(cad::ui::ThemeTokens::FontSm)));
    m_refEdit->hide();
    m_refEdit->installEventFilter(this);

    m_nameEdit = new ElaLineEdit(this);
    m_nameEdit->setMinimumHeight(0);
    m_nameEdit->setMaximumHeight(QWIDGETSIZE_MAX);
    m_nameEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_nameEdit->setStyleSheet(QStringLiteral(
        "font-size: %1px;")
        .arg(QString::number(cad::ui::ThemeTokens::FontMd)));
    m_nameEdit->hide();
    m_nameEdit->installEventFilter(this);

    // Copy feedback timer
    m_copyFeedbackTimer = new QTimer(this);
    m_copyFeedbackTimer->setSingleShot(true);
    m_copyFeedbackTimer->setInterval(350);
    connect(m_copyFeedbackTimer, &QTimer::timeout, this, [this]() {
        m_showingCopyFeedback = false;
        m_refLabel->setText(m_refName.isEmpty() ? m_refPlaceholder : m_refName);
    });

    connect(m_refEdit, &QLineEdit::editingFinished, this, &CompoundChip::commitRefEdit);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &CompoundChip::commitNameEdit);

    // Auto-uppercase for refName
    connect(m_refEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString up = text.toUpper();
        if (up != text) {
            const int cursor = m_refEdit->cursorPosition();
            const QSignalBlocker blocker(m_refEdit);
            m_refEdit->setText(up);
            m_refEdit->setCursorPosition(cursor);
        }
    });

    setName(QString());
    setRefName(QString());
}

void CompoundChip::setRefName(const QString& ref)
{
    const QString up = ref.trimmed().toUpper();
    if (m_refName == up && !m_showingCopyFeedback) return;
    m_refName = up;
    if (!m_showingCopyFeedback) {
        if (m_refName.isEmpty()) {
            m_refLabel->setText(m_refPlaceholder);
            m_refLabel->setPlaceholder(true);
            m_refLabel->setStyleSheet(QStringLiteral(
                "font-family: %1; font-size: %2px; font-weight: 500; color: %3; background: transparent;")
                .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                     QString::number(cad::ui::ThemeTokens::FontXs),
                     cad::ui::Theme::tokens().text3.name()));
            m_refLabel->setToolTip(QStringLiteral("双击设置代码/代号（用于公式引用）"));
        } else {
            m_refLabel->setText(m_refName);
            m_refLabel->setPlaceholder(false);
            m_refLabel->setStyleSheet(QStringLiteral(
                "font-family: %1; font-size: %2px; font-weight: 600; color: %3; background: transparent;")
                .arg(cad::ui::ThemeTokens::kMonospaceFamily,
                     QString::number(cad::ui::ThemeTokens::FontSm),
                     cad::ui::Theme::tokens().text1.name()));
            m_refLabel->setToolTip(QStringLiteral("代码: %1 (单击复制，双击编辑)").arg(m_refName));
        }
    }
    updatePartsGeometry();
}

void CompoundChip::setName(const QString& name)
{
    const QString n = name.trimmed();
    if (m_name == n) return;
    m_name = n;
    if (m_name.isEmpty()) {
        m_nameLabel->setText(m_namePlaceholder);
        m_nameLabel->setPlaceholder(true);
        m_nameLabel->setStyleSheet(QStringLiteral(
            "font-size: %1px; color: %2; background: transparent; padding-left: 5px;")
            .arg(QString::number(cad::ui::ThemeTokens::FontMd),
                 cad::ui::Theme::tokens().text3.name()));
        m_nameLabel->setToolTip(QStringLiteral("双击设置名称"));
    } else {
        m_nameLabel->setText(m_name);
        m_nameLabel->setPlaceholder(false);
        m_nameLabel->setStyleSheet(QStringLiteral(
            "font-size: %1px; color: %2; background: transparent; padding-left: 5px;")
            .arg(QString::number(cad::ui::ThemeTokens::FontMd),
                 cad::ui::Theme::tokens().text1.name()));
        m_nameLabel->setToolTip(QStringLiteral("名称: %1 (双击编辑)").arg(m_name));
    }
    updatePartsGeometry();
}

void CompoundChip::setPlaceholderText(const QString& ph)
{
    m_namePlaceholder = ph;
    if (m_name.isEmpty()) {
        m_nameLabel->setText(m_namePlaceholder);
    }
}

void CompoundChip::setRefPlaceholderText(const QString& ph)
{
    m_refPlaceholder = ph;
    if (m_refName.isEmpty()) {
        m_refLabel->setText(m_refPlaceholder);
    }
    updatePartsGeometry();
}

void CompoundChip::focusNameEdit()
{
    enterNameEdit();
}

void CompoundChip::focusRefEdit()
{
    if (m_refEditable) {
        enterRefEdit();
    }
}

void CompoundChip::resizeEvent(QResizeEvent*)
{
    updatePartsGeometry();
}

void CompoundChip::updatePartsGeometry()
{
    const int h = height();

    // Both slots are permanently visible
    m_refLabel->setVisible(true);
    m_nameLabel->setVisible(true);

    int refW = 34; // default width for "代码"
    if (!m_refName.isEmpty()) {
        QFontMetrics fm(m_refLabel->font());
        const int textW = fm.horizontalAdvance(m_refName);
        refW = qBound(32, textW + 14, width() / 2);
    }

    m_refLabel->setGeometry(0, 0, refW, h);
    m_nameLabel->setGeometry(refW, 0, width() - refW, h);

    if (m_refEdit->isVisible()) {
        m_refEdit->setGeometry(m_refLabel->geometry());
    }
    if (m_nameEdit->isVisible()) {
        m_nameEdit->setGeometry(m_nameLabel->geometry());
    }
}

void CompoundChip::enterRefEdit()
{
    if (!m_refEditable) return;
    m_refEdit->setText(m_refName);
    m_refEdit->setGeometry(m_refLabel->geometry());
    m_refEdit->show();
    m_refEdit->setFocus();
    m_refEdit->selectAll();
}

void CompoundChip::commitRefEdit()
{
    if (!m_refEdit->isVisible()) return;
    const QString newRef = m_refEdit->text().trimmed().toUpper();
    m_refEdit->hide();
    if (newRef != m_refName) {
        setRefName(newRef);
        emit refEdited(newRef);
    }
}

void CompoundChip::enterNameEdit()
{
    m_nameEdit->setText(m_name);
    m_nameEdit->setGeometry(m_nameLabel->geometry());
    m_nameEdit->show();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void CompoundChip::commitNameEdit()
{
    if (!m_nameEdit->isVisible()) return;
    const QString rawInput = m_nameEdit->text().trimmed();
    m_nameEdit->hide();

    // Check smart compound input (e.g. "B 胸围", "B:胸围")
    QString parsedRef, parsedName;
    if (tryParseCompound(rawInput, parsedRef, parsedName)) {
        if (parsedRef != m_refName) {
            setRefName(parsedRef);
            emit refEdited(parsedRef);
        }
        if (parsedName != m_name) {
            setName(parsedName);
            emit nameEdited(parsedName);
        }
        return;
    }

    if (rawInput != m_name) {
        setName(rawInput);
        emit nameEdited(rawInput);
    }
}

void CompoundChip::copyRefText()
{
    if (m_refName.isEmpty()) return;
    QClipboard* cb = QApplication::clipboard();
    if (cb) {
        cb->setText(m_refName);
    }
    m_showingCopyFeedback = true;
    m_refLabel->setText(QStringLiteral("\u2713"));  // ✓
    m_copyFeedbackTimer->start();
    emit refClicked(m_refName);
}

bool CompoundChip::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_refLabel) {
        if (event->type() == QEvent::Enter) {
            m_refLabel->setHovered(true);
        } else if (event->type() == QEvent::Leave) {
            m_refLabel->setHovered(false);
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                if (m_refName.isEmpty()) {
                    // Empty ref: clicking also opens editor
                    enterRefEdit();
                    return true;
                } else if (m_refCopyEnabled) {
                    copyRefText();
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                enterRefEdit();
                return true;
            }
        }
    } else if (obj == m_nameLabel) {
        if (event->type() == QEvent::Enter) {
            m_nameLabel->setHovered(true);
        } else if (event->type() == QEvent::Leave) {
            m_nameLabel->setHovered(false);
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                enterNameEdit();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            emit nameClicked(m_name);
        }
    } else if (obj == m_refEdit) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                m_refEdit->hide();
                return true;
            } else if (ke->key() == Qt::Key_Tab) {
                // Tab from ref to name
                commitRefEdit();
                enterNameEdit();
                return true;
            }
        }
    } else if (obj == m_nameEdit) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                m_nameEdit->hide();
                return true;
            } else if (ke->key() == Qt::Key_Backtab) {
                // Shift+Tab back to ref
                commitNameEdit();
                enterRefEdit();
                return true;
            } else if (ke->key() == Qt::Key_Tab) {
                // Tab to next widget in card
                commitNameEdit();
                focusNextChild();
                return true;
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

} // namespace cad::ui
