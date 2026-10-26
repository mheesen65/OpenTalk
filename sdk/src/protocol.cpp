#include "opentalk/protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

namespace opentalk {
namespace {

// JSON keys, kept in one place so encode and decode cannot drift apart.
namespace key {
constexpr auto type = "t";
constexpr auto body = "b";
constexpr auto version = "version";
constexpr auto nickname = "nickname";
constexpr auto userId = "userId";
constexpr auto serverName = "serverName";
constexpr auto code = "code";
constexpr auto description = "description";
constexpr auto channelId = "channel";
constexpr auto channels = "channels";
constexpr auto users = "users";
constexpr auto id = "id";
constexpr auto name = "name";
constexpr auto userCount = "userCount";
constexpr auto senderId = "senderId";
constexpr auto sender = "sender";
constexpr auto text = "text";
constexpr auto timestamp = "timestamp";
constexpr auto nonce = "nonce";
} // namespace key

quint32 toUInt(const QJsonValue &value)
{
    return static_cast<quint32>(value.toInteger(0));
}

} // namespace

QString messageTypeName(MessageType type)
{
    switch (type) {
    case MessageType::Hello: return QStringLiteral("Hello");
    case MessageType::Welcome: return QStringLiteral("Welcome");
    case MessageType::Error: return QStringLiteral("Error");
    case MessageType::Ping: return QStringLiteral("Ping");
    case MessageType::Pong: return QStringLiteral("Pong");
    case MessageType::ChannelList: return QStringLiteral("ChannelList");
    case MessageType::JoinChannel: return QStringLiteral("JoinChannel");
    case MessageType::LeaveChannel: return QStringLiteral("LeaveChannel");
    case MessageType::UserList: return QStringLiteral("UserList");
    case MessageType::UserJoined: return QStringLiteral("UserJoined");
    case MessageType::UserLeft: return QStringLiteral("UserLeft");
    case MessageType::TextMessage: return QStringLiteral("TextMessage");
    case MessageType::VoiceFrame: return QStringLiteral("VoiceFrame");
    case MessageType::FileOffer: return QStringLiteral("FileOffer");
    case MessageType::FileResponse: return QStringLiteral("FileResponse");
    case MessageType::FileChunk: return QStringLiteral("FileChunk");
    case MessageType::FileComplete: return QStringLiteral("FileComplete");
    case MessageType::Invalid: break;
    }
    return QStringLiteral("Invalid");
}

QJsonObject Message::toJson() const
{
    return QJsonObject{{QLatin1String(key::type), static_cast<int>(type)},
                       {QLatin1String(key::body), body}};
}

Message Message::fromJson(const QJsonObject &json)
{
    Message message;
    const int rawType = json.value(QLatin1String(key::type)).toInt(-1);
    if (rawType < 0 || rawType > 0xFF) {
        return message;
    }
    message.type = static_cast<MessageType>(static_cast<quint8>(rawType));
    message.body = json.value(QLatin1String(key::body)).toObject();
    return message;
}

QJsonObject UserInfo::toJson() const
{
    return QJsonObject{{QLatin1String(key::id), static_cast<qint64>(id)},
                       {QLatin1String(key::nickname), nickname},
                       {QLatin1String(key::channelId), static_cast<qint64>(channelId)}};
}

UserInfo UserInfo::fromJson(const QJsonObject &json)
{
    UserInfo user;
    user.id = toUInt(json.value(QLatin1String(key::id)));
    user.nickname = json.value(QLatin1String(key::nickname)).toString();
    user.channelId = toUInt(json.value(QLatin1String(key::channelId)));
    return user;
}

QJsonObject ChannelInfo::toJson() const
{
    return QJsonObject{{QLatin1String(key::id), static_cast<qint64>(id)},
                       {QLatin1String(key::name), name},
                       {QLatin1String(key::userCount), static_cast<qint64>(userCount)}};
}

ChannelInfo ChannelInfo::fromJson(const QJsonObject &json)
{
    ChannelInfo channel;
    channel.id = toUInt(json.value(QLatin1String(key::id)));
    channel.name = json.value(QLatin1String(key::name)).toString();
    channel.userCount = toUInt(json.value(QLatin1String(key::userCount)));
    return channel;
}

QJsonObject TextEvent::toJson() const
{
    return QJsonObject{{QLatin1String(key::channelId), static_cast<qint64>(channelId)},
                       {QLatin1String(key::senderId), static_cast<qint64>(senderId)},
                       {QLatin1String(key::sender), sender},
                       {QLatin1String(key::text), text},
                       {QLatin1String(key::timestamp), timestampMs}};
}

TextEvent TextEvent::fromJson(const QJsonObject &json)
{
    TextEvent event;
    event.channelId = toUInt(json.value(QLatin1String(key::channelId)));
    event.senderId = toUInt(json.value(QLatin1String(key::senderId)));
    event.sender = json.value(QLatin1String(key::sender)).toString();
    event.text = json.value(QLatin1String(key::text)).toString();
    event.timestampMs = json.value(QLatin1String(key::timestamp)).toInteger(0);
    return event;
}

QByteArray encodeFrame(const Message &message)
{
    const QByteArray json = QJsonDocument(message.body).toJson(QJsonDocument::Compact);

    // length covers the type byte plus the JSON body.
    const quint32 length = static_cast<quint32>(json.size()) + 1u;
    QByteArray frame;
    frame.reserve(static_cast<int>(length) + 4);

    char prefix[4];
    qToBigEndian<quint32>(length, prefix);
    frame.append(prefix, 4);
    frame.append(static_cast<char>(message.type));
    frame.append(json);
    return frame;
}

bool decodeFrame(const QByteArray &payload, Message &out)
{
    // One type byte plus at least "{}" is the smallest viable frame.
    if (payload.size() < 3) {
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.mid(1), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    out.type = static_cast<MessageType>(static_cast<quint8>(payload.at(0)));
    out.body = document.object();
    return true;
}

namespace messages {

Message hello(const QString &nickname)
{
    Message message;
    message.type = MessageType::Hello;
    message.body = QJsonObject{
        {QLatin1String(key::nickname), nickname},
        {QLatin1String(key::version), static_cast<int>(kProtocolVersion)}};
    return message;
}

Message welcome(quint32 userId, const QString &nickname, const QString &serverName)
{
    Message message;
    message.type = MessageType::Welcome;
    message.body = QJsonObject{
        {QLatin1String(key::userId), static_cast<qint64>(userId)},
        {QLatin1String(key::nickname), nickname},
        {QLatin1String(key::serverName), serverName},
        {QLatin1String(key::version), static_cast<int>(kProtocolVersion)}};
    return message;
}

Message error(const QString &code, const QString &description)
{
    Message message;
    message.type = MessageType::Error;
    message.body = QJsonObject{{QLatin1String(key::code), code},
                               {QLatin1String(key::description), description}};
    return message;
}

Message joinChannel(quint32 channelId)
{
    Message message;
    message.type = MessageType::JoinChannel;
    message.body = QJsonObject{{QLatin1String(key::channelId), static_cast<qint64>(channelId)}};
    return message;
}

Message leaveChannel()
{
    Message message;
    message.type = MessageType::LeaveChannel;
    return message;
}

Message channelList(const QVector<ChannelInfo> &channels)
{
    QJsonArray array;
    for (const ChannelInfo &channel : channels) {
        array.append(channel.toJson());
    }
    Message message;
    message.type = MessageType::ChannelList;
    message.body = QJsonObject{{QLatin1String(key::channels), array}};
    return message;
}

Message userList(quint32 channelId, const QVector<UserInfo> &users)
{
    QJsonArray array;
    for (const UserInfo &user : users) {
        array.append(user.toJson());
    }
    Message message;
    message.type = MessageType::UserList;
    message.body = QJsonObject{{QLatin1String(key::channelId), static_cast<qint64>(channelId)},
                               {QLatin1String(key::users), array}};
    return message;
}

Message userJoined(const UserInfo &user)
{
    Message message;
    message.type = MessageType::UserJoined;
    message.body = user.toJson();
    return message;
}

Message userLeft(quint32 userId, quint32 channelId)
{
    Message message;
    message.type = MessageType::UserLeft;
    message.body = QJsonObject{{QLatin1String(key::userId), static_cast<qint64>(userId)},
                               {QLatin1String(key::channelId), static_cast<qint64>(channelId)}};
    return message;
}

Message text(quint32 channelId, quint32 senderId, const QString &sender,
             const QString &text, qint64 timestampMs)
{
    TextEvent event;
    event.channelId = channelId;
    event.senderId = senderId;
    event.sender = sender;
    event.text = text;
    event.timestampMs = timestampMs > 0 ? timestampMs : QDateTime::currentMSecsSinceEpoch();

    Message message;
    message.type = MessageType::TextMessage;
    message.body = event.toJson();
    return message;
}

Message ping(quint32 nonce)
{
    Message message;
    message.type = MessageType::Ping;
    message.body = QJsonObject{{QLatin1String(key::nonce), static_cast<qint64>(nonce)}};
    return message;
}

Message pong(quint32 nonce)
{
    Message message;
    message.type = MessageType::Pong;
    message.body = QJsonObject{{QLatin1String(key::nonce), static_cast<qint64>(nonce)}};
    return message;
}

} // namespace messages
} // namespace opentalk
