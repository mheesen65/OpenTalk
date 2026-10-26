#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "opentalk/protocol.h"

class QTcpServer;
class QTcpSocket;

namespace opentalk {

struct ServerConfig {
    quint16 port{kDefaultPort};
    QString name{QStringLiteral("OpenTalk Server")};
    int maxUsers{200};

    /// Caps for relayed media. Voice payloads are a single Opus packet in the
    /// intended design, so a few kilobytes is generous.
    int maxVoicePayloadBytes{8192};
    int maxFileChunkBytes{256 * 1024};

    /// Accepted chat message length, in characters.
    int maxTextLength{4000};
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

    ServerConfig m_config;
    QTcpServer *m_tcp{nullptr};
    QHash<quint32, ClientSession *> m_sessions;
    QStringList m_channelNames;
    quint32 m_nextUserId{1};
};

/// One accepted TCP connection. Holds identity and channel state, and turns
/// frames into `messageReceived` signals for the server to interpret.
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

    void setId(quint32 id) { m_id = id; }
    void setNickname(const QString &nickname) { m_nickname = nickname; }
    void setChannelId(quint32 channelId) { m_channelId = channelId; }
    void setAuthenticated(bool authenticated) { m_authenticated = authenticated; }

    void send(const Message &message);
    /// Closes after the current write buffer has been flushed.
    void closeAfterFlush();

signals:
    void messageReceived(opentalk::ClientSession *session, const opentalk::Message &message);
    void closed(opentalk::ClientSession *session);

private slots:
    void handleReadyRead();
    void handleDisconnected();

private:
    void processBuffer();

    Server *m_server{nullptr};
    QTcpSocket *m_socket{nullptr};
    QByteArray m_buffer;
    quint32 m_id{0};
    QString m_nickname;
    quint32 m_channelId{0};
    bool m_authenticated{false};
};

} // namespace opentalk
