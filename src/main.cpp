#include <QApplication>
#include <QSettings>
#include <QTranslator>
#include <QLibraryInfo>
#include "ElaApplication.h"
#include "app/MainWindow.h"
#include "ui/Theme.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QString::fromUtf8("野风帖"));
    app.setApplicationVersion("0.1.0");

    // Load Qt built-in Chinese translation for standard buttons (Save/Cancel/etc.)
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale::Chinese, QStringLiteral("qt"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // ElaWidgetTools (Fluent UI kit) runtime bootstrap: loads the ElaAwesome
    // icon font + window shadow helpers, and sets the base UI font.
    ElaApplication::getInstance()->init();

    // Global design system: Fusion base style + token-driven palette, with
    // the ElaTheme mode switched in lockstep (Theme::apply drives both).
    // 用户偏好跨会话持久化 (QSettings view/darkMode); 默认使用暗色 (黑色) 模式.
    const bool dark = QSettings().value(QStringLiteral("view/darkMode"), true).toBool();
    cad::ui::Theme::apply(dark ? cad::ui::ThemeMode::Dark : cad::ui::ThemeMode::Light);

    MainWindow window;
    window.show();


    return app.exec();
}
