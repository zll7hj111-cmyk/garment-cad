# ============================================================================
# ElaWidgetTools patches applied by FetchContent PATCH_COMMAND (see CMakeLists.txt).
# The working directory is the downloaded source root, hence the relative glob.
# ============================================================================

# ---------------------------------------------------------------------------
# Part 1 — Qt 6.9+ compatibility.
#
# Qt 6.9+ removed the classic QChar(ushort)/QChar(short)/QChar(uint)/QChar(int)
# constructors and replaced them with SFINAE-constrained templates whose traits
# exclude unscoped enum types. ElaWidgetTools draws its icon glyphs via
# QChar(ElaIconType::xxx), which no longer compiles on Qt >= 6.9.
#
# This patch wraps every single-token QChar(...) call site as
# QChar(char16_t(...)) (char16_t is a fully supported implicit QChar input).
#
# NOTE: QChar(<expr with nested parens>) is NOT rewritten; the upstream code
# only uses single-token arguments at all QChar call sites (verified 2026-08).
# ---------------------------------------------------------------------------

file(GLOB_RECURSE ELA_CPPS "ElaWidgetTools/*.cpp")
foreach(f ${ELA_CPPS})
    file(READ "${f}" content)
    if(content MATCHES "QChar\\(")
        # Pass 1: QChar(<id>(<args>)) — one nested call level (e.g.
        # QChar(node->getAwesome()), QChar(suggest->getElaIcon())).
        string(REGEX REPLACE "QChar\\(([A-Za-z_][A-Za-z0-9_:>.-]*\\([^()]*\\))\\)" "QChar(char16_t(\\1))" content "${content}")
        # Pass 2: QChar(<single token>) — plain identifiers / enum values.
        string(REGEX REPLACE "QChar\\(([^()]+)\\)" "QChar(char16_t(\\1))" content "${content}")
        file(WRITE "${f}" "${content}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Part 2 — ElaContentDialog: use-after-free in button signal delivery.
#
# Upstream delivers the left/right button signals via
#     d->_doCloseAnimation(false/true);   // synchronously accept()/reject()
#     QTimer::singleShot(0, nullptr, [=]() { Q_EMIT xxxButtonClicked(); });
# so the dialog is closed (exec() returns) and deleted by the caller
# (src/ui/ElaMsgBox.h does exec(); delete dlg;) BEFORE the deferred emit runs
# on the next event-loop iteration — emitting on a freed `this` → crash.
#
# Fix: drop the close call and the singleShot wrapper so the signal is emitted
# synchronously inside the click lambda, while the dialog is still alive.
# The middle button never closed the dialog, so only its singleShot wrapper is
# removed.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE ELA_DLG_CPPS "ElaWidgetTools/ElaContentDialog.cpp")
foreach(f ${ELA_DLG_CPPS})
    file(READ "${f}" content)
    # Normalize line endings so the literal blocks below match regardless of
    # the checkout's EOL style (FetchContent clones keep the repo's CRLF).
    string(REPLACE "\r\n" "\n" content "${content}")
    # Left/right buttons: emit synchronously (dialog still alive); the
    # caller's accept() slot closes it safely. Middle button: drop the
    # singleShot wrapper only.
    string(REPLACE
"        d->_doCloseAnimation(false);
        QTimer::singleShot(0, nullptr, [=]() {
            Q_EMIT leftButtonClicked();
        });"
"        Q_EMIT leftButtonClicked();" content "${content}")
    string(REPLACE
"        d->_doCloseAnimation(true);
        QTimer::singleShot(0, nullptr, [=]() {
            Q_EMIT rightButtonClicked();
        });"
"        Q_EMIT rightButtonClicked();" content "${content}")
    string(REPLACE
"        QTimer::singleShot(0, nullptr, [=]() {
            Q_EMIT middleButtonClicked();
        });"
"        Q_EMIT middleButtonClicked();" content "${content}")
    # Middle button: an empty label hides the button instead of showing the
    # untranslated default text "minimum" (ElaMsgBox relies on this).
    # Guarded so re-running the patch on an already-patched checkout is a no-op.
    if(NOT content MATCHES "_middleButton->setVisible")
        string(REPLACE "    d->_middleButton->setText(text);"
            "    d->_middleButton->setText(text);\n    d->_middleButton->setVisible(!text.isEmpty());"
            content "${content}")
    endif()
    file(WRITE "${f}" "${content}")
endforeach()

# ---------------------------------------------------------------------------
# Part 3 — ElaMenu: drop the 400ms slide-in animation.
#
# showEvent() grabs the menu and plays a QPropertyAnimation that shifts the
# static grab around while paintEvent() draws it. During those 400ms the menu
# content is blank/out of place (grab happens before the content is painted)
# and hover highlighting is suppressed (paintEvent is hijacked by the anim).
# Users perceive the hovered item as "text missing". Menus now appear
# immediately with live hover.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE ELA_MENU_CPPS "ElaWidgetTools/ElaMenu.cpp")
foreach(f ${ELA_MENU_CPPS})
    file(READ "${f}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    if(content MATCHES "_animationPix = this->grab")
        string(REPLACE
"    d->_animationPix = this->grab(this->rect());
    QPropertyAnimation* posAnimation = new QPropertyAnimation(d, \"pAnimationImagePosY\");
    connect(posAnimation, &QPropertyAnimation::finished, this, [=]() {
        d->_animationPix = QPixmap();
        update();
    });
    connect(posAnimation, &QPropertyAnimation::valueChanged, this, [=](const QVariant& value) {
        update();
    });
    posAnimation->setEasingCurve(QEasingCurve::OutCubic);
    posAnimation->setDuration(400);
    int targetPosY = height();
    if (targetPosY > 160)
    {
        if (targetPosY < 320)
        {
            targetPosY = 160;
        }
        else
        {
            targetPosY /= 2;
        }
    }

    if (pos().y() + d->_menuStyle->getMenuItemHeight() + 9 >= QCursor::pos().y())
    {
        posAnimation->setStartValue(-targetPosY);
    }
    else
    {
        posAnimation->setStartValue(targetPosY);
    }

    posAnimation->setEndValue(0);
    posAnimation->start(QAbstractAnimation::DeleteWhenStopped);"
"    // [GCAD patch] remove 400ms slide-in animation: it blanks the menu while
    // animating and suppresses hover highlight during that window."
        content "${content}")
        file(WRITE "${f}" "${content}")
        message(STATUS "ElaMenu animation block removed in ${f}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Part 4 — ElaAppBar default close path: respect a rejected close().
#
# The app-bar close button runs, unconditionally:
#     window->close();
#     QApplication::processEvents();
#     windowHandle->close();
# When the top-level widget's closeEvent IGNORES the event (e.g. the user
# cancels a "save changes?" prompt in MainWindow::closeEvent), window->close()
# returns false but the code force-closes the native window handle anyway —
# that delivers a SECOND QCloseEvent to the widget (QWidgetWindow forwards
# window close events), re-entering closeEvent and showing the prompt twice.
# Fix: abort the default-close path when window->close() was rejected. For an
# accepted close the widget is already hidden, so the trailing
# windowHandle->close() was a no-op anyway.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE ELA_APPBAR_CPPS "ElaWidgetTools/private/ElaAppBarPrivate.cpp")
foreach(f ${ELA_APPBAR_CPPS})
    file(READ "${f}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    string(REPLACE
"        const auto window = q->window();
        window->close();
        QApplication::processEvents();"
"        const auto window = q->window();
        if (!window->close())
        {
            return;
        }
        QApplication::processEvents();"
        content "${content}")
    file(WRITE "${f}" "${content}")
endforeach()

# ---------------------------------------------------------------------------
# Part 5 — SpinBox: support QAbstractSpinBox::NoButtons.
#
# ElaSpinBoxStyle unconditionally drew Add/Sub buttons and icons (AngleUp/Down)
# ignoring buttonSymbols(). It also did not adjust editField rect when buttons
# are hidden. We update ElaSpinBoxStyle to respect NoButtons, and default
# ElaDoubleSpinBox and ElaSpinBox to NoButtons.
# ---------------------------------------------------------------------------

file(GLOB_RECURSE ELA_SPIN_CPPS "ElaWidgetTools/ElaDoubleSpinBox.cpp" "ElaWidgetTools/ElaSpinBox.cpp")
foreach(f ${ELA_SPIN_CPPS})
    file(READ "${f}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    if(NOT content MATCHES "setButtonSymbols\\(QAbstractSpinBox::NoButtons\\)")
        string(REPLACE
"    setStyle(d->_style);
    lineEdit()->setAlignment"
"    setStyle(d->_style);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    lineEdit()->setAlignment"
            content "${content}")
        file(WRITE "${f}" "${content}")
    endif()
endforeach()

file(GLOB_RECURSE ELA_STYLE_CPPS "ElaWidgetTools/DeveloperComponents/ElaSpinBoxStyle.cpp")
foreach(f ${ELA_STYLE_CPPS})
    file(READ "${f}" content)
    string(REPLACE "\r\n" "\n" content "${content}")
    if(NOT content MATCHES "sopt->buttonSymbols != QAbstractSpinBox::NoButtons")
        string(REPLACE
"        painter->drawRoundedRect(spinBoxRect, 4, 4);
        //添加按钮
        QRect addLineRect = subControlRect(control, sopt, SC_ScrollBarAddLine, widget);
        //减少按钮
        QRect subLineRect = subControlRect(control, sopt, SC_ScrollBarSubLine, widget);
        if (isEnable)
        {
            //添加按钮
            if (sopt->activeSubControls == SC_ScrollBarAddLine)
            {
                painter->setPen(Qt::NoPen);
                if (sopt->state & QStyle::State_Sunken && sopt->state & QStyle::State_MouseOver)
                {
                    painter->setBrush(ElaThemeColor(_themeMode, BasicPressAlpha));
                }
                else
                {
                    if (sopt->state & QStyle::State_MouseOver)
                    {
                        painter->setBrush(ElaThemeColor(_themeMode, BasicHoverAlpha));
                    }
                }
                painter->drawRoundedRect(addLineRect, 4, 4);
            }
            //减少按钮
            if (sopt->activeSubControls == SC_ScrollBarSubLine)
            {
                painter->setPen(Qt::NoPen);
                if (sopt->state & QStyle::State_Sunken && sopt->state & QStyle::State_MouseOver)
                {
                    painter->setBrush(ElaThemeColor(_themeMode, BasicPressAlpha));
                }
                else
                {
                    if (sopt->state & QStyle::State_MouseOver)
                    {
                        painter->setBrush(ElaThemeColor(_themeMode, BasicHoverAlpha));
                    }
                }
                painter->drawRoundedRect(subLineRect, 4, 4);
            }
        }
        //底边线
        painter->setPen(Qt::NoPen);
        painter->setBrush(ElaThemeColor(_themeMode, BasicHemline));
        QPainterPath path;
        path.moveTo(4, spinBoxRect.y() + spinBoxRect.height());
        path.lineTo(spinBoxRect.width() - 4, spinBoxRect.y() + spinBoxRect.height());
        path.arcTo(QRectF(spinBoxRect.width() - 8, spinBoxRect.y() + spinBoxRect.height() - 8, 8, 8), -90, 45);
        path.lineTo(4 - 2 * std::sqrt(2), spinBoxRect.y() + spinBoxRect.height() - (4 - 2 * std::sqrt(2)));
        path.arcTo(QRectF(0, spinBoxRect.y() + spinBoxRect.height() - 8, 8, 8), 225, 45);
        path.closeSubpath();
        painter->drawPath(path);

        //添加图标
        QFont iconFont = QFont(\"ElaAwesome\");
        iconFont.setPixelSize(17);
        painter->setFont(iconFont);
        painter->setPen(isEnable ? ElaThemeColor(_themeMode, BasicText) : ElaThemeColor(_themeMode, BasicTextDisable));
        painter->drawText(addLineRect, Qt::AlignCenter, _pButtonMode == ElaSpinBoxType::PMSide ? QChar(char16_t(char16_t(ElaIconType::Plus))) : QChar(char16_t(char16_t(ElaIconType::AngleUp))));
        //减小图标
        painter->drawText(subLineRect, Qt::AlignCenter, _pButtonMode == ElaSpinBoxType::PMSide ? QChar(char16_t(char16_t(ElaIconType::Minus))) : QChar(char16_t(char16_t(ElaIconType::AngleDown))));"
"        painter->drawRoundedRect(spinBoxRect, 4, 4);
        //底边线
        painter->setPen(Qt::NoPen);
        painter->setBrush(ElaThemeColor(_themeMode, BasicHemline));
        QPainterPath path;
        path.moveTo(4, spinBoxRect.y() + spinBoxRect.height());
        path.lineTo(spinBoxRect.width() - 4, spinBoxRect.y() + spinBoxRect.height());
        path.arcTo(QRectF(spinBoxRect.width() - 8, spinBoxRect.y() + spinBoxRect.height() - 8, 8, 8), -90, 45);
        path.lineTo(4 - 2 * std::sqrt(2), spinBoxRect.y() + spinBoxRect.height() - (4 - 2 * std::sqrt(2)));
        path.arcTo(QRectF(0, spinBoxRect.y() + spinBoxRect.height() - 8, 8, 8), 225, 45);
        path.closeSubpath();
        painter->drawPath(path);

        if (sopt->buttonSymbols != QAbstractSpinBox::NoButtons)
        {
            //添加按钮
            QRect addLineRect = subControlRect(control, sopt, SC_ScrollBarAddLine, widget);
            //减少按钮
            QRect subLineRect = subControlRect(control, sopt, SC_ScrollBarSubLine, widget);
            if (isEnable)
            {
                //添加按钮
                if (sopt->activeSubControls == SC_ScrollBarAddLine)
                {
                    painter->setPen(Qt::NoPen);
                    if (sopt->state & QStyle::State_Sunken && sopt->state & QStyle::State_MouseOver)
                    {
                        painter->setBrush(ElaThemeColor(_themeMode, BasicPressAlpha));
                    }
                    else
                    {
                        if (sopt->state & QStyle::State_MouseOver)
                        {
                            painter->setBrush(ElaThemeColor(_themeMode, BasicHoverAlpha));
                        }
                    }
                    painter->drawRoundedRect(addLineRect, 4, 4);
                }
                //减少按钮
                if (sopt->activeSubControls == SC_ScrollBarSubLine)
                {
                    painter->setPen(Qt::NoPen);
                    if (sopt->state & QStyle::State_Sunken && sopt->state & QStyle::State_MouseOver)
                    {
                        painter->setBrush(ElaThemeColor(_themeMode, BasicPressAlpha));
                    }
                    else
                    {
                        if (sopt->state & QStyle::State_MouseOver)
                        {
                            painter->setBrush(ElaThemeColor(_themeMode, BasicHoverAlpha));
                        }
                    }
                    painter->drawRoundedRect(subLineRect, 4, 4);
                }
            }

            //添加图标
            QFont iconFont = QFont(\"ElaAwesome\");
            iconFont.setPixelSize(17);
            painter->setFont(iconFont);
            painter->setPen(isEnable ? ElaThemeColor(_themeMode, BasicText) : ElaThemeColor(_themeMode, BasicTextDisable));
            painter->drawText(addLineRect, Qt::AlignCenter, _pButtonMode == ElaSpinBoxType::PMSide ? QChar(char16_t(char16_t(ElaIconType::Plus))) : QChar(char16_t(char16_t(ElaIconType::AngleUp))));
            //减小图标
            painter->drawText(subLineRect, Qt::AlignCenter, _pButtonMode == ElaSpinBoxType::PMSide ? QChar(char16_t(char16_t(ElaIconType::Minus))) : QChar(char16_t(char16_t(ElaIconType::AngleDown))));
        }"
            content "${content}")

        string(REPLACE
"    case CC_SpinBox:
    {
        switch (sc)"
"    case CC_SpinBox:
    {
        const QStyleOptionSpinBox* sopt = qstyleoption_cast<const QStyleOptionSpinBox*>(opt);
        if (sopt && sopt->buttonSymbols == QAbstractSpinBox::NoButtons)
        {
            if (sc == SC_SpinBoxEditField)
            {
                QRect spinBoxRect = QProxyStyle::subControlRect(cc, opt, SC_SpinBoxFrame, widget);
                return {spinBoxRect.x(), spinBoxRect.y(), spinBoxRect.width(), spinBoxRect.height()};
            }
            if (sc == SC_ScrollBarAddLine || sc == SC_ScrollBarSubLine)
            {
                return QRect();
            }
        }
        switch (sc)"
            content "${content}")
        file(WRITE "${f}" "${content}")
    endif()
endforeach()
