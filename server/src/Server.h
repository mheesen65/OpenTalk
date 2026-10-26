#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "opentalk/protocol.h"

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace opentalk {

struct ServerConfig {
    quint16 port{kDefaultPort};
    QString name{QStringLiteral("OpenTalk Server")};
    int maxUsers{200};

    /// Caps for relayed media. Voice payloads are a single Opus packet, so a
    /// few kilobytes is generous.
    int maxVoicePayloadBytes{8192};
    int maxFileChunkBytes{256 * 1024};
    int maxTextLength{4000};

    /// Per-session abuse limits. A legitimate client sends ~50 voice frames a
    /// second plus the occasional chat line, so these are deliberately loose.
    int maxFramesPerSecond{400};
    qint64 maxBytesPerSecond{2 * 1024 * 1024};

    /// An unauthenticated socket must send Hello promptly.
    int handshakeTimeoutMs{15000};

    /// Sessions are pinged this often and dropped after idleTimeoutMs without
    /// any inbound frame, which cleans up half-open TCP connections.
    int heartbeatIntervalMs{15000};
    int idleTimeoutMs{90000};
};

class ClientSession;

/// The server is intentionally single-threaded. One event loop handling all
/// sockets is enough for the 50-200 user target; per-channel media threads are
/// a later concern (see docs/architecture.md).
class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(ServerConfig config, QObject *parent = nullptr);

    bool start();
    quint16 port() const;
    const ServerConfig &config() const { return m_config; }

    QStringList channelNames() const { return m_channelNames; }
    int sessionCount() const { return static_cast<int>(m_sessions.size()); }

    /// Closes the listener and every session, telling clients why.
    void stop(const QString &reason = QString());

    // Called by ClientSession.
    void handleMessage(ClientSession *session, const Message &message);
    void removeSession(ClientSession *session);

private:
    void sendChannelList(ClientSession *session);
    void sendChannelListToAll();
    void sendUserList(ClientSession *session, quint32 channelId);
    void broadcastChannel(quint32 channelId, const Message &message,
                          ClientSession *except = nullptr);
    void announceDeparture(ClientSession *session);
    QVector<UserInfo> usersInChannel(quint32 channelId) const;
    UserInfo userInfoFor(const ClientSession *session) const;
    void reject(ClientSession *session, const QString &code, const QString &description);
    quint32 nextUserId();
    void tick();

    ServerConfig m_config;
    QTcpServer *m_tcp{nullptr};
    QTimer *m_heartbeat{nullptr};
    QHash<quint32, ClientSession *> m_sessions;
    QStringList m_channelNames;
    quint32 m_nextUserId{1};
    quint32 m_pingNonce{1};
    bool m_stopping{false};
};

/// One accepted TCP connection. Holds identity and channel state, enforces the
/// per-session budgets, and turns frames into `messageReceived` signals.
class ClientSession : public QObject
{
    Q_OBJECT

public:
    ClientSession(QTcpSocket *socket, Server *server, quint32 id,
                  QObject *parent = nullptr);
    ~ClientSession() override;

    quint32 id() const { return m_id; }
    QString nickname() const { return m_nickname; }
    quint32 channelId() const { return m_channelId; }
    bool authenticated() const { return m_authenticated; }
    qint64 lastActivityMs() const { return m_lastActivityMs; }
    qint64 ageMs() const { return m_lifetime.elapsed(); }

    void setId(quint32 id) { m_id = id; }
    void setNickname(const QString &nickname) { m_nickname = nickname; }
    void setChannelId(quint32 channelId) { m_channelId = channelId; }
    void setAuthenticated(bool authenticated) { m_authenticated = authenticated; }

    void send(const Message &message);
    /// Closes after the current write buffer has been flushed.
    void closeAfterFlush();
    QAbstractSocket::SocketState socketState() const;

signals:
    void messageReceived(opentalk::ClientSession *session, const opentalk::Message &message);
    void closed(opentalk::ClientSession *session);

private slots:
    void handleReadyRead();
    void handleDisconnected();

private:
    void processBuffer();
    /// Returns false when the session exceeded its frame or byte budget.
    bool consumeBudget(int bytes);

    Server *m_server{nullptr};
    QTcpSocket *m_socket{nullptr};
    QByteArray m_buffer;
    quint32 m_id{0};
    QString m_nickname;
    quint32 m_channelId{0};
    bool m_authenticated{false};

    QElapsedTimer m_lifetime;
    QElapsedTimer m_rateWindow;
    int m_framesInWindow{0};
    qint64 m_bytesInWindow{0};
    qint64 m_lastActivityMs{0};
};

} // namespace opentalk
