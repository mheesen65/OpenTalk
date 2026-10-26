#include "Server.h"

#include <QDebug>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>

#include <utility>

namespace opentalk {
namespace {

constexpr int kNamedChannelCount = 3;

QString sanitiseNickname(const QString &raw, quint32 fallbackId)
{
    QString nickname = raw.trimmed();
    // Strip control characters so a nickname cannot corrupt a client's UI.
    nickname.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]")));
    if (nickname.isEmpty()) {
        nickname = QStringLiteral("guest-%1").arg(fallbackId);
    }
    if (nickname.size() > 32) {
        nickname.truncate(32);
    }
    return nickname;
}

} // namespace

// ---------------------------------------------------------------- Server ---

Server::Server(ServerConfig config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_channelNames{QStringLiteral("Lobby"), QStringLiteral("General"),
                     QStringLiteral("Support")}
{
    Q_ASSERT(m_channelNames.size() == kNamedChannelCount);
}

bool Server::start()
{
    m_tcp = new QTcpServer(this);
    if (!m_tcp->listen(QHostAddress::Any, m_config.port)) {
        qCritical() << "Cannot listen on port" << m_config.port << ":" << m_tcp->errorString();
        return false;
    }

    connect(m_tcp, &QTcpServer::newConnection, this, [this] {
        while (m_tcp->hasPendingConnections()) {
            QTcpSocket *socket = m_tcp->nextPendingConnection();
            if (m_sessions.size() >= m_config.maxUsers) {
                qInfo() << "Refusing connection from" << socket->peerAddress().toString()
                        << "- server is full";
                // Let the refusal flush before the socket goes away.
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                socket->write(encodeFrame(messages::error(QStringLiteral("server-full"),
                                                          QStringLiteral("This server is full."))));
                socket->disconnectFromHost();
                continue;
            }

            auto *session = new ClientSession(socket, this, nextUserId(), this);
            m_sessions.insert(session->id(), session);
            connect(session, &ClientSession::messageReceived, this, &Server::handleMessage);
            connect(session, &ClientSession::closed, this, &Server::removeSession);

            qInfo().noquote() << QStringLiteral("Connection from %1 (session %2)")
                                     .arg(socket->peerAddress().toString())
                                     .arg(session->id());
        }
    });

    return true;
}

quint16 Server::port() const
{
    return m_tcp ? m_tcp->serverPort() : 0;
}

quint32 Server::nextUserId()
{
    return m_nextUserId++;
}

UserInfo Server::userInfoFor(const ClientSession *session) const
{
    UserInfo user;
    user.id = session->id();
    user.nickname = session->nickname();
    user.channelId = session->channelId();
    return user;
}

QVector<UserInfo> Server::usersInChannel(quint32 channelId) const
{
    QVector<UserInfo> users;
    if (channelId == 0) {
        return users;
    }
    for (ClientSession *session : m_sessions) {
        if (session->authenticated() && session->channelId() == channelId) {
            users.append(userInfoFor(session));
        }
    }
    return users;
}

void Server::sendChannelList(ClientSession *session)
{
    QVector<ChannelInfo> channels;
    channels.reserve(m_channelNames.size());
    for (int i = 0; i < m_channelNames.size(); ++i) {
        ChannelInfo info;
        info.id = static_cast<quint32>(i + 1);
        info.name = m_channelNames.at(i);
        info.userCount = static_cast<quint32>(usersInChannel(info.id).size());
        channels.append(info);
    }
    session->send(messages::channelList(channels));
}

void Server::sendChannelListToAll()
{
    for (ClientSession *session : m_sessions) {
        if (session->authenticated()) {
            sendChannelList(session);
        }
    }
}

void Server::sendUserList(ClientSession *session, quint32 channelId)
{
    session->send(messages::userList(channelId, usersInChannel(channelId)));
}

void Server::broadcastChannel(quint32 channelId, const Message &message,
                              ClientSession *except)
{
    if (channelId == 0) {
        return;
    }
    for (ClientSession *session : m_sessions) {
        if (session == except || !session->authenticated()) {
            continue;
        }
        if (session->channelId() == channelId) {
            session->send(message);
        }
    }
}

void Server::announceDeparture(ClientSession *session)
{
    const quint32 channelId = session->channelId();
    if (channelId == 0) {
        return;
    }

    broadcastChannel(channelId, messages::userLeft(session->id(), channelId), session);

    // Everyone left behind needs the corrected roster for that channel.
    for (ClientSession *other : m_sessions) {
        if (other != session && other->authenticated() && other->channelId() == channelId) {
            sendUserList(other, channelId);
        }
    }
}

void Server::reject(ClientSession *session, const QString &code, const QString &description)
{
    session->send(messages::error(code, description));
}

void Server::removeSession(ClientSession *session)
{
    announceDeparture(session);
    m_sessions.remove(session->id());
    qInfo().noquote() << QStringLiteral("Session %1 disconnected").arg(session->id());
    session->deleteLater();
    sendChannelListToAll();
}

void Server::handleMessage(ClientSession *session, const Message &message)
{
    // Ping is answered before authentication so liveness probes work during
    // connection setup.
    if (message.type == MessageType::Ping) {
        session->send(messages::pong(
            static_cast<quint32>(message.body.value(QLatin1String("nonce")).toInteger(0))));
        return;
    }

    if (message.type == MessageType::Hello) {
        if (session->authenticated()) {
            reject(session, QStringLiteral("already-authenticated"),
                   QStringLiteral("This session has already completed the handshake."));
            return;
        }

        const int version = message.body.value(QLatin1String("version")).toInt(0);
        if (version != kProtocolVersion) {
            reject(session, QStringLiteral("unsupported-version"),
                   QStringLiteral("Server speaks protocol version %1, client sent %2.")
                       .arg(kProtocolVersion)
                       .arg(version));
            session->closeAfterFlush();
            return;
        }

        session->setNickname(sanitiseNickname(
            message.body.value(QLatin1String("nickname")).toString(), session->id()));
        session->setAuthenticated(true);
        session->setChannelId(0);

        session->send(messages::welcome(session->id(), session->nickname(), m_config.name));
        sendChannelList(session);
        qInfo().noquote() << QStringLiteral("Session %1 is now \"%2\"")
                                 .arg(session->id())
                                 .arg(session->nickname());
        return;
    }

    if (!session->authenticated()) {
        reject(session, QStringLiteral("not-authenticated"),
               QStringLiteral("Send a Hello message before anything else."));
        session->closeAfterFlush();
        return;
    }

    switch (message.type) {
    case MessageType::JoinChannel: {
        const auto channelId =
            static_cast<quint32>(message.body.value(QLatin1String("channel")).toInteger(0));
        if (channelId < 1 || channelId > static_cast<quint32>(m_channelNames.size())) {
            reject(session, QStringLiteral("unknown-channel"),
                   QStringLiteral("No such channel: %1").arg(channelId));
            return;
        }
        if (session->channelId() == channelId) {
            return;
        }

        announceDeparture(session);
        session->setChannelId(channelId);
        const UserInfo user = userInfoFor(session);

        // Tell the existing members, then give the joiner the full picture.
        broadcastChannel(channelId, messages::userJoined(user), session);
        sendUserList(session, channelId);
        for (ClientSession *other : m_sessions) {
            if (other != session && other->authenticated() && other->channelId() == channelId) {
                sendUserList(other, channelId);
            }
        }
        sendChannelListToAll();
        qInfo().noquote() << QStringLiteral("\"%1\" joined channel %2")
                                 .arg(session->nickname())
                                 .arg(m_channelNames.at(static_cast<int>(channelId) - 1));
        return;
    }

    case MessageType::LeaveChannel: {
        if (session->channelId() == 0) {
            return;
        }
        announceDeparture(session);
        session->setChannelId(0);
        sendChannelListToAll();
        return;
    }

    case MessageType::TextMessage: {
        const quint32 channelId = session->channelId();
        if (channelId == 0) {
            reject(session, QStringLiteral("not-in-channel"),
                   QStringLiteral("Join a channel before sending messages."));
            return;
        }

        QString text = message.body.value(QLatin1String("text")).toString();
        text = text.trimmed();
        if (text.isEmpty()) {
            return;
        }
        if (text.size() > m_config.maxTextLength) {
            text.truncate(m_config.maxTextLength);
        }

        // Identity is taken from the session, never from the client payload, so
        // a peer cannot speak as somebody else.
        broadcastChannel(channelId,
                         messages::text(channelId, session->id(), session->nickname(), text));
        return;
    }

    case MessageType::VoiceFrame:
    case MessageType::FileOffer:
    case MessageType::FileResponse:
    case MessageType::FileChunk:
    case MessageType::FileComplete: {
        const quint32 channelId = session->channelId();
        if (channelId == 0) {
            reject(session, QStringLiteral("not-in-channel"),
                   QStringLiteral("Join a channel before sending media."));
            return;
        }

        Message relay = message;
        // The server owns channel and sender identity for relayed payloads.
        relay.body.insert(QLatin1String("channel"), static_cast<qint64>(channelId));
        relay.body.insert(QLatin1String("senderId"), static_cast<qint64>(session->id()));
        relay.body.insert(QLatin1String("sender"), session->nickname());

        if (message.type == MessageType::VoiceFrame) {
            const int payloadSize =
                message.body.value(QLatin1String("data")).toString().size();
            if (payloadSize > m_config.maxVoicePayloadBytes * 4 / 3 + 8) {
                reject(session, QStringLiteral("voice-too-large"),
                       QStringLiteral("Voice payload exceeds the limit."));
                return;
            }
        } else if (message.type == MessageType::FileChunk) {
            const int payloadSize =
                message.body.value(QLatin1String("data")).toString().size();
            if (payloadSize > m_config.maxFileChunkBytes * 4 / 3 + 8) {
                reject(session, QStringLiteral("chunk-too-large"),
                       QStringLiteral("File chunk exceeds the limit."));
                return;
            }
        }

        broadcastChannel(channelId, relay, session);
        return;
    }

    case MessageType::Pong:
        return;

    case MessageType::Hello:
        return; // handled above

    default:
        reject(session, QStringLiteral("unknown-message"),
               QStringLiteral("Unsupported message type %1.").arg(static_cast<int>(message.type)));
        return;
    }
}

// --------------------------------------------------------- ClientSession ---

ClientSession::ClientSession(QTcpSocket *socket, Server *server, quint32 id, QObject *parent)
    : QObject(parent)
    , m_server(server)
    , m_socket(socket)
    , m_id(id)
{
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientSession::handleReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientSession::handleDisconnected);
}

ClientSession::~ClientSession() = default;

void ClientSession::send(const Message &message)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(encodeFrame(message));
    }
}

void ClientSession::closeAfterFlush()
{
    m_socket->disconnectFromHost();
}

void ClientSession::handleReadyRead()
{
    m_buffer.append(m_socket->readAll());
    processBuffer();
}

void ClientSession::handleDisconnected()
{
    emit closed(this);
}

void ClientSession::processBuffer()
{
    while (m_buffer.size() >= 4) {
        const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());

        if (length == 0 || length > kMaxFrameSize) {
            qWarning() << "Session" << m_id << "sent an invalid frame length:" << length;
            m_socket->abort();
            return;
        }

        const int totalSize = static_cast<int>(length) + 4;
        if (m_buffer.size() < totalSize) {
            return;
        }

        const QByteArray payload = m_buffer.mid(4, static_cast<int>(length));
        m_buffer.remove(0, totalSize);

        Message message;
        if (!decodeFrame(payload, message)) {
            qWarning() << "Session" << m_id << "sent a malformed frame";
            continue;
        }
        emit messageReceived(this, message);
    }
}

} // namespace opentalk
