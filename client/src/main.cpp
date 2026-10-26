#include <QApplication>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("OpenTalk"));
    QApplication::setApplicationDisplayName(QStringLiteral("OpenTalk"));
    QApplication::setOrganizationName(QStringLiteral("OpenTalk"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    MainWindow window;
    window.resize(1000, 640);
    window.show();

    return app.exec();
}
