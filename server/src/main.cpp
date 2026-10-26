#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include "Server.h"

#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_UNIX
int g_signalFd[2] = {-1, -1};

/// Signal handlers may only touch async-signal-safe functions, so the signal
/// just wakes the event loop through a socket pair.
void handleShutdownSignal(int)
{
    const char byte = 1;
    const ssize_t written = ::write(g_signalFd[1], &byte, 1);
    Q_UNUSED(written)
}
#endif

#ifdef Q_OS_WIN
BOOL WINAPI handleConsoleEvent(DWORD)
{
    QCoreApplication::quit();
    return TRUE;
}
#endif

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("OpenTalkServer"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("OpenTalk voice, text and file-transfer server."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Port to listen on (default: %1).").arg(opentalk::kDefaultPort),
        QStringLiteral("port"), QString::number(opentalk::kDefaultPort));
    const QCommandLineOption nameOption(
        {QStringLiteral("n"), QStringLiteral("name")},
        QStringLiteral("Server name shown to clients."),
        QStringLiteral("name"), QStringLiteral("OpenTalk Server"));
    const QCommandLineOption maxUsersOption(
        QStringLiteral("max-users"),
        QStringLiteral("Maximum simultaneous users."),
        QStringLiteral("count"), QStringLiteral("200"));
    const QCommandLineOption idleTimeoutOption(
        QStringLiteral("idle-timeout"),
        QStringLiteral("Disconnect sessions idle for this many seconds."),
        QStringLiteral("seconds"), QStringLiteral("90"));

    parser.addOption(portOption);
    parser.addOption(nameOption);
    parser.addOption(maxUsersOption);
    parser.addOption(idleTimeoutOption);
    parser.process(app);

    opentalk::ServerConfig config;

    bool ok = false;
    const int port = parser.value(portOption).toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        QTextStream(stderr) << "Invalid --port value: " << parser.value(portOption) << '\n';
        return 1;
    }
    config.port = static_cast<quint16>(port);
    config.name = parser.value(nameOption);

    const int maxUsers = parser.value(maxUsersOption).toInt(&ok);
    if (!ok || maxUsers < 1) {
        QTextStream(stderr) << "Invalid --max-users value: "
                            << parser.value(maxUsersOption) << '\n';
        return 1;
    }
    config.maxUsers = maxUsers;

    const int idleSeconds = parser.value(idleTimeoutOption).toInt(&ok);
    if (!ok || idleSeconds < 1) {
        QTextStream(stderr) << "Invalid --idle-timeout value: "
                            << parser.value(idleTimeoutOption) << '\n';
        return 1;
    }
    config.idleTimeoutMs = idleSeconds * 1000;
    config.heartbeatIntervalMs = qMin(15000, qMax(1000, config.idleTimeoutMs / 3));

    opentalk::Server server(config);
    if (!server.start()) {
        return 1;
    }

    QTextStream(stdout) << "OpenTalk server \"" << config.name << "\" listening on port "
                        << server.port() << " (max " << config.maxUsers << " users, idle timeout "
                        << idleSeconds << "s)\n";
    QTextStream(stdout).flush();

#ifdef Q_OS_UNIX
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFd) == 0) {
        auto *notifier = new QSocketNotifier(g_signalFd[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [&server] {
            char byte = 0;
            const ssize_t readBytes = ::read(g_signalFd[0], &byte, 1);
            Q_UNUSED(readBytes)
            server.stop(QStringLiteral("Server received a shutdown signal."));
            QCoreApplication::quit();
        });
        std::signal(SIGINT, handleShutdownSignal);
        std::signal(SIGTERM, handleShutdownSignal);
    }
#elif defined(Q_OS_WIN)
    SetConsoleCtrlHandler(handleConsoleEvent, TRUE);
#endif

    QTextStream(stdout) << "Press Ctrl+C to stop.\n";
    QTextStream(stdout).flush();

    return app.exec();
}
