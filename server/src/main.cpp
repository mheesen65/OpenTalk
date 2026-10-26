#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include "Server.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("OpenTalkServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("OpenTalk voice, text and file-transfer server."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Port to listen on (default: %1).").arg(opentalk::kDefaultPort),
        QStringLiteral("port"),
        QString::number(opentalk::kDefaultPort));
    const QCommandLineOption nameOption(
        {QStringLiteral("n"), QStringLiteral("name")},
        QStringLiteral("Server name shown to clients."),
        QStringLiteral("name"),
        QStringLiteral("OpenTalk Server"));
    const QCommandLineOption maxUsersOption(
        QStringLiteral("max-users"),
        QStringLiteral("Maximum simultaneous users."),
        QStringLiteral("count"),
        QStringLiteral("200"));

    parser.addOption(portOption);
    parser.addOption(nameOption);
    parser.addOption(maxUsersOption);
    parser.process(app);

    opentalk::ServerConfig config;
    bool portOk = false;
    const int port = parser.value(portOption).toInt(&portOk);
    if (!portOk || port < 1 || port > 65535) {
        QTextStream(stderr) << "Invalid --port value: " << parser.value(portOption) << '\n';
        return 1;
    }
    config.port = static_cast<quint16>(port);
    config.name = parser.value(nameOption);

    bool maxUsersOk = false;
    const int maxUsers = parser.value(maxUsersOption).toInt(&maxUsersOk);
    if (!maxUsersOk || maxUsers < 1) {
        QTextStream(stderr) << "Invalid --max-users value: "
                            << parser.value(maxUsersOption) << '\n';
        return 1;
    }
    config.maxUsers = maxUsers;

    opentalk::Server server(config);
    if (!server.start()) {
        return 1;
    }

    QTextStream(stdout) << "OpenTalk server \"" << config.name << "\" listening on port "
                        << server.port() << " (max " << config.maxUsers << " users)\n";

    return app.exec();
}
