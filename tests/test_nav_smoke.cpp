/// @file test_nav_smoke.cpp
/// Smoke test: construct MainWindow, switch the web-style tab page stack
/// (画布/变量/图层/组) repeatedly to reproduce the reported "点击图层=闪退"
/// crash (Ela page-switch animation grabs the QOpenGLWidget canvas — now
/// avoided both by StackSwitchMode::None and by the custom QStackedWidget).
/// Not registered with ctest — run manually:
///   build\out\Debug\test_nav_smoke.exe

#include <QApplication>
#include <QTimer>
#include <QStackedWidget>
#include <QMenuBar>
#include <QMenu>
#include <QLineEdit>
#include <QAbstractButton>
#include <QPointer>
#include <QGraphicsSceneMouseEvent>
#include <iostream>

#include "ElaMenuBar.h"
#include "ElaTheme.h"
#include "ElaAppBar.h"
#include "ElaIconButton.h"
#include "ui/CopyChip.h"
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
        QStackedWidget* pageStack = nullptr;
        for (auto* s : window.findChildren<QStackedWidget*>())
            if (s->count() == 4) { pageStack = s; break; }
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
        sample(pageStack, 1, &window);
        sample(pageStack, 2, &window);
        sample(pageStack, 3, &window);
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
        sample(pageStack, 3, &window);
        QImage wim = window.grab().toImage();
        std::cout << "  windowMid=" << wim.pixelColor(wim.width() / 2, wim.height() / 2).name().toStdString()
                  << " topLeft=" << wim.pixelColor(8, 60).name().toStdString() << std::endl;
        sample(pageStack, 1, &window);
        sample(pageStack, 2, &window);
        sample(pageStack, 3, &window);
        std::cout << "theme probe done" << std::endl;
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
            std::cout << "lpdialog: X button " << (xClicked ? "clicked" : "NOT FOUND")
                      << std::endl;
            for (int i = 0; i < 20; ++i)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            std::cout << "lpdialog: close OK (no crash)" << std::endl;
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

    // Crash probe: switch DIRECTLY to each page of the custom tab stack in
    // construction order (no settle phase) to bisect which page crashes.
    // The stack is the QStackedWidget with 4 pages (the only one after the
    // Ela navigation was replaced by the web-style tab bar).
    QStackedWidget* pageStack = nullptr;
    const auto stacks = window.findChildren<QStackedWidget*>();
    for (auto* s : stacks) {
        if (s->count() == 4) { pageStack = s; break; }
    }
    std::cout << "pageStack found: " << (pageStack ? "yes" : "NO")
              << " (stacks=" << stacks.size() << ")" << std::endl;

    // BISECT: single fast page-cycle timer (100ms). Is rapid switching the
    // trigger (vs. two interleaved timers)?
#if 1
    QTimer probe;
    QObject::connect(&probe, &QTimer::timeout, [&]() {
        static int idx = 0;
        if (pageStack) {
            pageStack->setCurrentIndex(idx);
        }
        idx = (idx + 1) % 4;
    });
    probe.start(300);
#endif
    (void)probe;
#if 1
    // Cycle the tab stack a few times through QTimer so the event loop
    // actually processes show/hide + polish between switches.
    int step = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        if (step < 4) {
            ++step;
            return;  // phase 1: just let the window settle
        }
        const int idx = step % 4;
        if (pageStack)
            pageStack->setCurrentIndex(idx);
        ++step;
        if (step > 16) {
            app.quit();
        }
    });
    timer.start(200);
#endif

    return app.exec();
}
