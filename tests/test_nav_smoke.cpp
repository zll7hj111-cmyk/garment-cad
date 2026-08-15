/// @file test_nav_smoke.cpp
/// Smoke test: construct MainWindow, switch the web-style tab page stack
/// (画布) repeatedly to reproduce the reported "点击图层=闪退" crash
/// (Ela page-switch animation grabs the QOpenGLWidget canvas — now avoided
/// both by StackSwitchMode::None and by the custom QStackedWidget).
/// 变量/图层/组 标签现在切换面板悬浮窗 (侧边栏样式), 不再进入页面堆栈。
/// Not registered with ctest — run manually:
///   build\out\Debug\test_nav_smoke.exe

#include <QApplication>
#include <QTimer>
#include <QStackedWidget>
#include <QMenuBar>
#include <QMenu>
#include <QLineEdit>
#include <QScrollBar>
#include <QAbstractButton>
#include <QPushButton>
#include <QPointer>
#include <QRect>
#include <QLabel>
#include <QVBoxLayout>
#include <QGraphicsSceneMouseEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <iostream>
#include <climits>

#include "ElaMenuBar.h"
#include "ElaTabBar.h"
#include "ElaTheme.h"
#include "ElaAppBar.h"
#include "ElaIconButton.h"
#include "ui/CopyChip.h"
#include "ui/VariableCard.h"
#include "ui/VirtualCardList.h"
#include "parametric/ParamDocument.h"
#include "canvas/CanvasScene.h"
#include "canvas/CanvasView.h"
#include "canvas/BlockItem.h"
#include "tools/LinePropertyDialog.h"
#include "tools/ToolSelect.h"
#include "TestHelpers.h"

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep)
{
    std::cout << "CRASH at " << (void*)ep->ExceptionRecord->ExceptionAddress
              << " code 0x" << std::hex << ep->ExceptionRecord->ExceptionCode
              << std::dec << std::endl;
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(),
                         GetCurrentThread(), &sf, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        DWORD64 disp = 0;
        char buf[sizeof(SYMBOL_INFO) + 256]{};
        auto* si = reinterpret_cast<SYMBOL_INFO*>(buf);
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 256;
        if (SymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &disp, si)) {
            std::cout << "  @" << (void*)sf.AddrPC.Offset << " " << si->Name
                      << "+0x" << std::hex << disp << std::dec << std::endl;
        } else {
            std::cout << "  @" << (void*)sf.AddrPC.Offset << " ?" << std::endl;
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#include "app/MainWindow.h"
#include "ui/Theme.h"
#include "ElaApplication.h"
#include "ElaMenuBar.h"

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(crashHandler);
#endif
    std::cout << "step1: QApplication" << std::endl;
    QApplication app(argc, argv);
    std::cout << "step2: ElaApplication init" << std::endl;
    ElaApplication::getInstance()->init();
    std::cout << "step3: Theme apply" << std::endl;
    cad::ui::Theme::apply(cad::ui::ThemeMode::Dark);
    std::cout << "step4: construct MainWindow" << std::endl;
    MainWindow window;
    std::cout << "step5: show" << std::endl;
    window.show();
    std::cout << "step6: start timer" << std::endl;

    // ---- MENU POPUP CRASH PROBE (文件 menu) ----
    QTimer::singleShot(1500, [&]() {
        std::cout << "menu probe: popup ElaMenuBar menus" << std::endl;
        auto* mb = window.findChild<ElaMenuBar*>();
        if (!mb) { std::cout << "  no ElaMenuBar!" << std::endl; app.quit(); return; }
        std::cout << "  ElaMenuBar found, actions=" << mb->actions().size() << std::endl;
        const auto acts = mb->actions();
        for (QAction* a : acts) {
            QMenu* m = a->menu();
            std::cout << "  action '" << a->text().toStdString() << "' menu=" << (m ? "yes" : "null")
                      << std::endl;
            if (m) {
                m->popup(mb->mapToGlobal(QPoint(60, 30)));
                std::cout << "  popup called" << std::endl;
                for (int i = 0; i < 10; ++i)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                std::cout << "  popup events processed" << std::endl;
                m->close();
                std::cout << "  menu closed" << std::endl;
            }
        }
        std::cout << "menu probe done" << std::endl;
    });

    // ---- THEME PIXEL PROBE: sample panel backgrounds in Dark, toggle Light,
    //      sample again. Ground truth for "变量/图层/组 没适配白色模式". ----
    QTimer::singleShot(2500, [&]() {
        // 主窗口标签条: 画布/变量/图层/组 (count==4)。面板窗内还有一条
        // 大标签条 (count==3), 按 count 区分。
        ElaTabBar* tabs = nullptr;
        for (auto* tb : window.findChildren<ElaTabBar*>())
            if (tb->count() == 4) { tabs = tb; break; }
        if (tabs) {
            tabs->setCurrentIndex(1);  // 变量 → 打开面板悬浮窗
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        QStackedWidget* pageStack = nullptr;
        for (auto* s : window.findChildren<QStackedWidget*>())
            if (s->count() == 3) { pageStack = s; break; }  // 面板窗三页堆栈
        auto sample = [](QStackedWidget* st, int idx, QWidget* win) {
            st->setCurrentIndex(idx);
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QWidget* page = st->widget(idx);
            QPixmap pm = page->grab();
            QImage im = pm.toImage();
            auto px = [&](int x, int y) {
                QColor c = im.pixelColor(x, y);
                return QString("#%1%2%3")
                    .arg(c.red(), 2, 16, QLatin1Char('0'))
                    .arg(c.green(), 2, 16, QLatin1Char('0'))
                    .arg(c.blue(), 2, 16, QLatin1Char('0'));
            };
            std::cout << "  page" << idx << " bg(" << px(4, 4).toStdString()
                      << ") mid(" << px(im.width() / 2, im.height() / 2).toStdString()
                      << ")" << std::endl;
        };
        std::cout << "theme probe: DARK" << std::endl;
        sample(pageStack, 0, &window);  // 变量页
        sample(pageStack, 1, &window);  // 图层页
        sample(pageStack, 2, &window);  // 组页
        // 面板悬浮窗: 抓取内容像素作为白色模式证据.
        {
            if (auto* varWin = window.findChild<QWidget*>(
                    QStringLiteral("panelFloatingWindow"))) {
                QPixmap pm = varWin->grab();
                QImage im = pm.toImage();
                std::cout << "  varWin visible=" << varWin->isVisible()
                          << " geo=" << varWin->geometry().x() << ","
                          << varWin->geometry().y() << " "
                          << varWin->geometry().width() << "x"
                          << varWin->geometry().height()
                          << " bg=" << im.pixelColor(4, 4).name().toStdString()
                          << " mid=" << im.pixelColor(im.width() / 2,
                                                      im.height() / 2).name().toStdString()
                          << std::endl;
                // 窄窗子标签探针: 变量页的四个子标签必须整行可见, 不允许
                // 滚动裁剪 (第一条 ElaTabBar 是 变量/图层/组 大标签, 按
                // count==4 找变量页的子标签条)。
                for (auto* subTabs : varWin->findChildren<ElaTabBar*>()) {
                    if (subTabs->count() != 4)
                        continue;
                    const QRect r3 = subTabs->tabRect(3);
                    std::cout << "  subTabs count=" << subTabs->count()
                              << " w=" << subTabs->width()
                              << " expanding=" << subTabs->expanding()
                              << " scrollBtns=" << subTabs->usesScrollButtons()
                              << " tab3=(" << r3.x() << "," << r3.y() << " "
                              << r3.width() << "x" << r3.height() << ")"
                              << " visible="
                              << (r3.isValid()
                                  && subTabs->rect().contains(r3.center())
                                  ? 1 : 0)
                              << std::endl;
                }
                // 大标签切换探针: 点击 图层/组 大标签必须切换面板内容页
                // (曾漏接 currentChanged→stack 导致内容永远停在变量页)。
                for (auto* bigBar : varWin->findChildren<ElaTabBar*>()) {
                    if (bigBar->count() != 3)
                        continue;
                    QStackedWidget* pstack = nullptr;
                    for (auto* s : varWin->findChildren<QStackedWidget*>())
                        if (s->count() == 3) { pstack = s; break; }
                    auto switchBig = [&](int idx) {
                        bigBar->setCurrentIndex(idx);
                        for (int i = 0; i < 10; ++i)
                            QCoreApplication::processEvents(
                                QEventLoop::AllEvents, 50);
                        std::cout << "  bigTab" << idx << " stack="
                                  << (pstack ? pstack->currentIndex() : -1)
                                  << " expect=" << idx << std::endl;
                    };
                    switchBig(1);  // 图层
                    switchBig(2);  // 组
                    switchBig(0);  // 回变量页
                }
            }
        }
        // ---- ACCENT BAR ALTERNATION PROBE ----
        // 左侧竖线: 偶数行蓝 #2F6FED / 奇数行橙 #F59E0B (四页统一蓝橙交替,
        // 替代类型色条与背景斑马纹)。DPR 校正采样 (grab 图像是设备像素)。
        {
            auto* win = new QWidget(&window);
            win->resize(260, 420);
            auto* lay = new QVBoxLayout(win);
            cad::param::Variable v;
            v.name = QStringLiteral("x");
            v.refName = QStringLiteral("V1");
            v.value = 1.0;
            for (int i = 0; i < 6; ++i) {
                auto* card = new VariableCard(v, i % 2 == 1, win);  // 全局命名空间
                card->setFixedHeight(56);
                lay->addWidget(card);
            }
            win->show();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QString blue = QColor(0x2F, 0x6F, 0xED).name();
            const QString orange = QColor(0xF5, 0x9E, 0x0B).name();
            int okCount = 0;
            int idx = 0;
            // VariableCard 与 VirtualCardList 同为全局命名空间类 (头文件里
            // 只有 CopyChip 的 forward declaration 在 cad::ui 中)。
            const QList<VariableCard*> cards = win->findChildren<VariableCard*>();
            for (VariableCard* card : cards) {
                const QImage img = card->grab().toImage();
                const qreal dpr = img.devicePixelRatio();
                const int devX = static_cast<int>(1.5 * dpr);
                const int devY = static_cast<int>((card->height() / 2) * dpr);
                const QString got = img.pixelColor(devX, devY).name();
                const bool expectOrange = (idx % 2 == 1);
                const bool pass = got == (expectOrange ? orange : blue);
                okCount += pass ? 1 : 0;
                std::cout << "  bar card" << idx << "=" << got.toStdString()
                          << " expect=" << (expectOrange ? orange : blue).toStdString()
                          << (pass ? " OK" : " MISMATCH") << std::endl;
                ++idx;
            }
            std::cout << "  bar result=" << okCount << "/6" << std::endl;
            win->close();
            delete win;
        }
        // ---- COPYCHIP BOX PIXEL PROBE ----
        // 常驻输入框描边 (CopyChip::paintEvent 自绘): 边框像素应 =
        // @borderStrong, 框内 = @surface (空文本无文字干扰采样)。
        {
            auto* win = new QWidget(&window);
            win->resize(220, 60);
            auto* lay = new QVBoxLayout(win);
            auto* chip = new cad::ui::CopyChip(cad::ui::CopyChip::Variant::Name, win);
            chip->setFixedSize(120, 20);
            lay->addWidget(chip);
            win->show();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QImage img = chip->grab().toImage();
            const qreal dpr = img.devicePixelRatio();
            auto pxAt = [&](int x, int y) {
                return img.pixelColor(static_cast<int>(x * dpr),
                                      static_cast<int>(y * dpr)).name();
            };
            // 描边专用色 (与 CopyChip::paintEvent 同源逻辑: 亮 #9AA4B2 / 暗 #4E5866).
            const bool darkMode =
                cad::ui::Theme::mode() == cad::ui::ThemeMode::Dark;
            const QColor chipBorder = darkMode ? QColor(0x4E, 0x58, 0x66)
                                               : QColor(0x9A, 0xA4, 0xB2);
            const QString border = chipBorder.name();
            const QString surface = cad::ui::Theme::tokens().surface.name();
            const QString gotB = pxAt(0, 10);   // x=0 完全落在 1px 描边内
            const QString gotS = pxAt(6, 10);
            std::cout << "  chipBox border=" << gotB.toStdString()
                      << " expect=" << border.toStdString()
                      << (gotB == border ? " OK" : " MISMATCH")
                      << " inner=" << gotS.toStdString()
                      << " expect=" << surface.toStdString()
                      << (gotS == surface ? " OK" : " MISMATCH") << std::endl;
            // ---- 名称 chip 编辑→提交→框还在吗 (用户报告: 名称编辑后没框) ----
            {
                auto* card = new VariableCard(cad::param::Variable{}, false, win);
                card->setFixedSize(360, 68);
                lay->addWidget(card);
                for (int i = 0; i < 10; ++i)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                auto* nameChip = card->findChild<cad::ui::CopyChip*>();
                auto* chipLabel = nameChip
                    ? nameChip->findChild<QLabel*>() : nullptr;
                auto* chipEdit = nameChip
                    ? nameChip->findChild<QLineEdit*>() : nullptr;
                auto scanBox = [&](const char* tag) {
                    const QImage cimg = card->grab().toImage();
                    const QColor bc = darkMode ? QColor(0x4E, 0x58, 0x66)
                                               : QColor(0x9A, 0xA4, 0xB2);
                    int px = 0;
                    for (int y = 0; y < cimg.height(); ++y)
                        for (int x = 0; x < cimg.width(); ++x) {
                            const QColor c = cimg.pixelColor(x, y);
                            if (std::abs(c.red() - bc.red()) <= 14
                                && std::abs(c.green() - bc.green()) <= 14
                                && std::abs(c.blue() - bc.blue()) <= 14)
                                ++px;
                        }
                    std::cout << "  nameChip[" << tag << "] borderPx=" << px
                              << (px > 80 ? " OK" : " MISSING") << std::endl;
                };
                scanBox("idle-before");
                // 双击标签 → enterEdit
                if (nameChip && chipLabel && chipEdit) {
                    QMouseEvent dbl(QEvent::MouseButtonDblClick,
                                    QPointF(20, 10), Qt::LeftButton,
                                    Qt::LeftButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(chipLabel, &dbl);
                    for (int i = 0; i < 5; ++i)
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    chipEdit->setText(QStringLiteral("测试"));
                    // 回车提交 (eventFilter 里 KeyPress Return → commitEdit)
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return,
                                    Qt::NoModifier);
                    QCoreApplication::sendEvent(chipEdit, &enter);
                    for (int i = 0; i < 5; ++i)
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    scanBox("after-commit");
                }
            }
            // 落盘 PNG 供人工核对 chip 渲染 (名称 chip + 引用名 chip)。
            {
                auto* card = new VariableCard(cad::param::Variable{}, false, win);
                card->setFixedSize(360, 68);
                lay->addWidget(card);
                for (int i = 0; i < 10; ++i)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                card->grab().save(QStringLiteral("e:/garment-cad/build/var_card_probe.png"));
                // 分析卡片图像: 统计描边专用色像素与行分布,
                // 确认名称/引用名两个输入框的描边真实渲染。
                const QImage cimg = card->grab().toImage();
                const QColor bc = darkMode ? QColor(0x4E, 0x58, 0x66)
                                           : QColor(0x9A, 0xA4, 0xB2);
                int borderPx = 0;
                int minRow = INT_MAX, maxRow = -1;
                for (int y = 0; y < cimg.height(); ++y) {
                    for (int x = 0; x < cimg.width(); ++x) {
                        const QColor c = cimg.pixelColor(x, y);
                        if (std::abs(c.red() - bc.red()) <= 14
                            && std::abs(c.green() - bc.green()) <= 14
                            && std::abs(c.blue() - bc.blue()) <= 14) {
                            ++borderPx;
                            minRow = qMin(minRow, y);
                            maxRow = qMax(maxRow, y);
                        }
                    }
                }
                std::cout << "  cardBorderPx=" << borderPx
                          << " rows=" << minRow << ".." << maxRow
                          << (borderPx > 40 ? " OK (两框描边可见)" : " MISSING")
                          << std::endl;
            }
            win->close();
            delete win;
        }
        // ---- 真实应用路径探针: 在真实 MainWindow 面板窗里点「添加」创建
        // 真实变量卡片, 抓面板窗口扫描描边 (复刻用户实际看到的一切)。
        auto realPanelScan = [&](const QString& expectHex, const char* label) {
            auto* tabs2 = window.findChild<ElaTabBar*>();
            for (auto* tb : window.findChildren<ElaTabBar*>())
                if (tb->count() == 4) { tabs2 = tb; break; }
            auto* varWin = window.findChild<QWidget*>(
                QStringLiteral("panelFloatingWindow"));
            if (!varWin || !tabs2) {
                std::cout << "  realPanel[" << label << "] no window/tabs"
                          << std::endl;
                return;
            }
            if (!varWin->isVisible())
                tabs2->setCurrentIndex(1);  // 打开面板 (仅当隐藏时, 防 toggle)
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            // 先钉回「变量」页再创建变量 (隐藏列表页上插入卡片几何失效).
            for (auto* s : varWin->findChildren<QStackedWidget*>())
                if (s->count() == 3) s->setCurrentIndex(0);
            for (auto* tb : varWin->findChildren<ElaTabBar*>())
                if (tb->count() == 3) tb->setCurrentIndex(0);
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            auto* btn = varWin->findChild<QPushButton*>(
                QStringLiteral("primaryButton"));
            if (!btn) {
                std::cout << "  realPanel[" << label << "] no add btn"
                          << std::endl;
                return;
            }
            btn->click();  // 真实创建变量 → 卡片进 VirtualCardList
            for (int i = 0; i < 20; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            // 新卡片自动进入名称编辑态 → 真实提交: 输入名称 → 回车.
            // 挑「可见」的编辑框 (findChild 可能命中旧卡已隐藏的覆盖层).
            QLineEdit* nameEdit = nullptr;
            for (auto* e : varWin->findChildren<QLineEdit*>(
                     QStringLiteral("chipEdit")))
                if (e->isVisible()) { nameEdit = e; break; }
            if (nameEdit) {
                nameEdit->setText(QStringLiteral("测试"));
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return,
                                Qt::NoModifier);
                QCoreApplication::sendEvent(nameEdit, &enter);
                for (int i = 0; i < 20; ++i)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            // 抓图前钉回变量页 (双保险).
            for (auto* s : varWin->findChildren<QStackedWidget*>())
                if (s->count() == 3) s->setCurrentIndex(0);
            const QImage img = varWin->grab().toImage();
            const QColor bc(expectHex);
            const QColor otherBorder =
                (expectHex == QStringLiteral("#4E5866"))
                    ? QColor(0x9A, 0xA4, 0xB2)   // 反向色: 亮色描边
                    : QColor(0x4E, 0x58, 0x66);  // 反向色: 暗色描边
            int borderPx = 0, otherBorderPx = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const QColor c = img.pixelColor(x, y);
                    if (std::abs(c.red() - bc.red()) <= 14
                        && std::abs(c.green() - bc.green()) <= 14
                        && std::abs(c.blue() - bc.blue()) <= 14)
                        ++borderPx;
                    if (std::abs(c.red() - otherBorder.red()) <= 14
                        && std::abs(c.green() - otherBorder.green()) <= 14
                        && std::abs(c.blue() - otherBorder.blue()) <= 14)
                        ++otherBorderPx;
                }
            img.save(QStringLiteral("e:/garment-cad/build/real_panel_%1.png")
                         .arg(QLatin1String(label)));
            std::cout << "  realPanel[" << label << "] borderPx=" << borderPx
                      << " otherBorderPx=" << otherBorderPx
                      << " expect=" << expectHex.toStdString()
                      << (borderPx > 40 ? " OK" : " MISSING") << std::endl;
            // 屏幕级验证: 抓真实屏幕 (用户肉眼所见).
            varWin->show();
            varWin->raise();
            varWin->activateWindow();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (QScreen* scr = varWin->screen()) {
                const QImage full = scr->grabWindow(0).toImage();
                const qreal dpr = scr->devicePixelRatio();
                const QRect vg = varWin->geometry();
                const QPoint org = scr->geometry().topLeft();
                const QRect dr(qRound((vg.x() - org.x()) * dpr),
                               qRound((vg.y() - org.y()) * dpr),
                               qRound(vg.width() * dpr),
                               qRound(vg.height() * dpr));
                const QImage simg = full.copy(dr.intersected(full.rect()));
                simg.save(QStringLiteral("e:/garment-cad/build/real_screen_%1.png")
                              .arg(QLatin1String(label)));
                int scPx = 0, scOther = 0;
                for (int y = 0; y < simg.height(); ++y)
                    for (int x = 0; x < simg.width(); ++x) {
                        const QColor c = simg.pixelColor(x, y);
                        if (std::abs(c.red() - bc.red()) <= 14
                            && std::abs(c.green() - bc.green()) <= 14
                            && std::abs(c.blue() - bc.blue()) <= 14)
                            ++scPx;
                        if (std::abs(c.red() - otherBorder.red()) <= 14
                            && std::abs(c.green() - otherBorder.green()) <= 14
                            && std::abs(c.blue() - otherBorder.blue()) <= 14)
                            ++scOther;
                    }
                std::cout << "  realScreen[" << label << "] borderPx=" << scPx
                          << " otherPx=" << scOther
                          << " winVis=" << (varWin->isVisible() ? 1 : 0)
                          << (scPx > 40 ? " SCREEN-OK" : " SCREEN-MISSING")
                          << std::endl;
            }
        };
        realPanelScan(QStringLiteral("#4E5866"), "dark");
        std::cout << "theme probe: toggle LIGHT" << std::endl;
        window.toggleTheme(false);
        QWidget* pg = pageStack->widget(1);
        std::cout << "  Theme::mode=" << (cad::ui::Theme::mode() == cad::ui::ThemeMode::Light ? "Light" : "Dark")
                  << " ElaMode=" << (ElaTheme::getInstance()->getThemeMode() == ElaThemeType::Light ? "Light" : "Dark")
                  << std::endl;
        std::cout << "  page1 autoFill=" << pg->autoFillBackground()
                  << " styledBg=" << pg->testAttribute(Qt::WA_StyledBackground)
                  << " hasOwnPal=" << pg->testAttribute(Qt::WA_SetPalette)
                  << " palWin=" << pg->palette().window().color().name().toStdString()
                  << " stylesheet=" << !pg->styleSheet().isEmpty() << std::endl;
        std::cout << "  appPalWin=" << QApplication::palette().window().color().name().toStdString() << std::endl;
        for (QWidget* a = pg; a; a = a->parentWidget())
            std::cout << "  anc " << a->metaObject()->className()
                      << " ownPal=" << a->testAttribute(Qt::WA_SetPalette)
                      << " win=" << a->palette().window().color().name().toStdString()
                      << " stylesheet=" << !a->styleSheet().isEmpty() << std::endl;
        for (QWidget* w : QApplication::allWidgets())
            w->setPalette(QApplication::palette());
        std::cout << "  after allWidgets setPalette:" << std::endl;
        for (QWidget* a = pg; a; a = a->parentWidget())
            std::cout << "  anc " << a->metaObject()->className()
                      << " ownPal=" << a->testAttribute(Qt::WA_SetPalette)
                      << " win=" << a->palette().window().color().name().toStdString() << std::endl;
        sample(pageStack, 2, &window);
        QImage wim = window.grab().toImage();
        std::cout << "  windowMid=" << wim.pixelColor(wim.width() / 2, wim.height() / 2).name().toStdString()
                  << " topLeft=" << wim.pixelColor(8, 60).name().toStdString() << std::endl;
        sample(pageStack, 1, &window);
        sample(pageStack, 2, &window);
        std::cout << "theme probe done" << std::endl;
        // ---- LIGHT 主题下卡片描边扫描 (用户默认亮色, 必须同样可见) ----
        {
            auto* win = new QWidget(&window);
            win->resize(380, 90);
            auto* lay = new QVBoxLayout(win);
            auto* card = new VariableCard(cad::param::Variable{}, false, win);
            card->setFixedSize(360, 68);
            lay->addWidget(card);
            win->show();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const QImage cimg = card->grab().toImage();
            // 亮色主题: 描边专用色 #9AA4B2.
            const QColor bc(0x9A, 0xA4, 0xB2);
            int borderPx = 0;
            for (int y = 0; y < cimg.height(); ++y)
                for (int x = 0; x < cimg.width(); ++x) {
                    const QColor c = cimg.pixelColor(x, y);
                    if (std::abs(c.red() - bc.red()) <= 14
                        && std::abs(c.green() - bc.green()) <= 14
                        && std::abs(c.blue() - bc.blue()) <= 14)
                        ++borderPx;
                }
            std::cout << "  lightCardBorderPx=" << borderPx
                      << " borderColor=" << bc.name().toStdString()
                      << (borderPx > 40 ? " OK" : " MISSING") << std::endl;
            win->close();
            delete win;
        }
        realPanelScan(QStringLiteral("#9AA4B2"), "light");
        // 关闭面板窗, 恢复初始状态。
        if (auto* varWin = window.findChild<QWidget*>(
                QStringLiteral("panelFloatingWindow")))
            varWin->hide();
        // ---- COPYCHIP EDIT-OVERLAY HEIGHT PROBE ----
        // ElaLineEdit's ctor hard-codes setFixedHeight(35), which used to
        // defeat enterEdit()'s setGeometry(label rect): the overlay stayed
        // 35px tall on an ~18px label row, pushing text down and clipping.
        {
            cad::ui::CopyChip chip(cad::ui::CopyChip::Variant::Name, &window);
            chip.resize(220, 60);
            chip.setText(QStringLiteral("XK"));
            chip.focusEdit();
            auto* edit = chip.findChild<QLineEdit*>();
            auto* label = chip.findChild<QLabel*>();
            if (edit && label) {
                std::cout << "copychip edit h=" << edit->height()
                          << " label h=" << label->height() << std::endl;
                if (edit->height() > label->height() + 4) {
                    std::cout << "copychip MISMATCH: overlay taller than label"
                              << std::endl;
                    window.close();
                    app.quit();
                    return;
                }
            } else {
                std::cout << "copychip edit/label not found" << std::endl;
            }
        }
        // ---- LINE-PROPERTY DIALOG OPEN→CLOSE CRASH PROBE ----
        // Reproduce "打开线条属性窗口然后关闭会触发闪退": X → window->close()
        // → reject() → onRejected() → WA_DeleteOnClose deleteLater.
        {
            cad::param::ParamDocument doc;
            const auto line = cad::test::makeLine(doc, 120.0);
            doc.resolveAll();
            CanvasScene scene(&doc);
            CanvasView view(&scene);
            view.resize(900, 600);
            view.show();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            auto* dlg = new cad::tools::LinePropertyDialog(
                line.blockId, line.segId, &doc, &scene, &view);
            QPointer<cad::tools::LinePropertyDialog> guard(dlg);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            std::cout << "lpdialog: created children=" << dlg->children().size()
                      << " guardNull=" << guard.isNull() << std::endl;
            dlg->show();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            std::cout << "lpdialog: shown guardNull=" << guard.isNull()
                      << " visible=" << (guard ? guard->isVisible() : -1)
                      << std::endl;
            // 环境偶发: show 后对话框被自发 close 事件 + WA_DeleteOnClose
            // 提前删除 (guard 变空) —— 此时必须跳过, 不能对已释放 dlg 操作。
            if (!guard) {
                std::cout << "lpdialog: SKIPPED (dialog auto-closed on show)"
                          << std::endl;
            } else {
                auto* nameEdit = dlg->findChild<QLineEdit*>();
                if (nameEdit) {
                    std::cout << "lpdialog: editing name..." << std::endl;
                    nameEdit->setText(QStringLiteral("TEST"));
                    for (int i = 0; i < 10; ++i)
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                }
                std::cout << "lpdialog: clicking X..." << std::endl;
                bool xClicked = false;
                if (auto* bar = dlg->findChild<ElaAppBar*>()) {
                    for (auto* b : bar->findChildren<QAbstractButton*>()) {
                        if (auto* iconBtn = qobject_cast<ElaIconButton*>(b)) {
                            if (iconBtn->getAwesome() == ElaIconType::Xmark) {
                                emit iconBtn->clicked();
                                xClicked = true;
                                break;
                            }
                        }
                    }
                }
                std::cout << "lpdialog: X button "
                          << (xClicked ? "clicked" : "NOT FOUND") << std::endl;
                for (int i = 0; i < 20; ++i)
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                std::cout << "lpdialog: close OK (no crash)" << std::endl;
            }
        }
        // ---- TOOLSELECT SINGLE-CLICK → EDIT TARGET PROBE ----
        // Click a segment with the select tool → the edit-target callback
        // must fire once with the clicked segment id (status-bar strip).
        {
            cad::param::ParamDocument doc2;
            CanvasScene scene2(&doc2);
            const auto line2 = cad::test::makeLine(doc2, 120.0);
            doc2.resolveAll();
            CanvasView view2(&scene2);
            view2.resize(600, 400);
            view2.show();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            cad::tools::ToolSelect ts;
            QUuid gotBlock, gotSeg;
            int cbCount = 0;
            ts.setEditTargetCallback([&](const QUuid& b, const QUuid& s) {
                gotBlock = b; gotSeg = s; ++cbCount;
            });
            ts.activate(scene2, &doc2);
            const int itemCount = scene2.items().size();
            const int hitCount = scene2.items(QPointF(60.0, 0.0)).size();
            QString layerInfo;
            for (QGraphicsItem* it : scene2.items()) {
                if (auto* bi = dynamic_cast<BlockItem*>(it)) {
                    const auto* blk = doc2.findBlock(bi->blockId());
                    layerInfo += QStringLiteral(" blockLayer=%1 active=%2")
                        .arg(blk ? blk->layer.toString() : QStringLiteral("?"))
                        .arg(doc2.activeLayer().toString());
                }
            }
            std::cout << "selecttool: items=" << itemCount
                      << " hitAtMid=" << hitCount
                      << layerInfo.toStdString() << std::endl;
            QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
            press.setButton(Qt::LeftButton);
            press.setButtons(Qt::LeftButton);
            press.setScenePos(QPointF(60.0, 0.0));  // mid of (0,0)-(120,0)
            ts.mousePress(&press);
            std::cout << "selecttool: cbCount=" << cbCount
                      << " blockMatch=" << (gotBlock == line2.blockId)
                      << " segMatch=" << (gotSeg == line2.segId) << std::endl;
            ts.deactivate();
        }
        window.close();
        app.quit();
    });

    // ---- LAYOUT DEBUG DUMP ----
    QTimer::singleShot(800, [&]() {
        std::cout << "=== WIDGET TREE DUMP ===" << std::endl;
        std::function<void(QWidget*, int)> dump = [&](QWidget* w, int depth) {
            std::cout << std::string(depth * 2, ' ') << w->metaObject()->className()
                      << " '" << w->objectName().toStdString() << "'"
                      << " geo=" << w->geometry().x() << "," << w->geometry().y()
                      << " " << w->geometry().width() << "x" << w->geometry().height()
                      << (w->isVisible() ? " VISIBLE" : " HIDDEN")
                      << (w->isHidden() ? " HIDDEN2" : "")
                      << std::endl;
            for (auto* child : w->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
                dump(child, depth + 1);
        };
        dump(&window, 0);
        std::cout << "=== END DUMP ===" << std::endl;
    });

    // Crash probe: switch DIRECTLY to each page of the panel stack in
    // construction order (no settle phase) to bisect which page crashes.
    // The stack is the QStackedWidget with 3 pages 变量/图层/组 inside the
    // panelFloatingWindow (the main page stack now holds only the canvas).
    QStackedWidget* pageStack = nullptr;
    const auto stacks = window.findChildren<QStackedWidget*>();
    for (auto* s : stacks) {
        if (s->count() == 3) { pageStack = s; break; }
    }
    std::cout << "pageStack found: " << (pageStack ? "yes" : "NO")
              << " (stacks=" << stacks.size() << ")" << std::endl;

    // BISECT: single fast page-cycle timer (100ms). Is rapid switching the
    // trigger (vs. two interleaved timers)?
    // 2026-08: DISABLED — 该翻页定时器与 realPanelScan 并发运行, 每 200ms
    // 翻一次面板页, 污染所有面板抓图时机 (暗色阶段抓到空页/残影)。崩溃
    // bisect 已收敛, 保留代码以备复测, 用 #if 0 关闭。
#if 0
    QTimer probe;
    QObject::connect(&probe, &QTimer::timeout, [&]() {
        static int idx = 0;
        if (pageStack) {
            pageStack->setCurrentIndex(idx);
        }
        idx = (idx + 1) % 3;
    });
    probe.start(300);
#endif
    // 兜底退出: 探针流程异常挂起时 15s 后强制退出, 避免测试卡死。
    QTimer::singleShot(15000, [&]() {
        std::cout << "FALLBACK QUIT (15s timeout)" << std::endl;
        app.quit();
    });

    return app.exec();
}
