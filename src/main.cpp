#include <QApplication>
#include <QTranslator>
#include <QLibraryInfo>
#include "app/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QString::fromUtf8("服装CAD"));
    app.setApplicationVersion("0.1.0");

    // Load Qt built-in Chinese translation for standard buttons (Save/Cancel/etc.)
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale::Chinese, QStringLiteral("qt"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    MainWindow window;
    window.show();

    return app.exec();
}
