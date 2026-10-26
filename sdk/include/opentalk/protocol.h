#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace opentalk {

/// Bumped whenever a change breaks older peers. Peers with a different value
/// are refused at handshake rather than failing later mid-session.
inline constexpr quint16 kProtocolVersion = 1;

inline constexpr quint16 kDefaultPort = 10333;

/// Guards the server against a malicious length prefix allocating unbounded
/// memory. A file chunk is the largest legitimate frame.
inline constexpr quint32 kMaxFrameSize = 4u * 1024u * 1024u;

inline constexpr quint32 kDefaultChannelId = 1;

/// Every frame starts with a 4-byte big-endian payload length, then one type
/// byte, then the JSON body. See docs/architecture.md.
enum class MessageType : quint8 {
    Invalid = 0x00,

    // Session lifecycle
    Hello = 0x01,
    Welcome = 0x02,
    Error = 0x03,
    Ping = 0x04,
    Pong = 0x05,

    // Channels and membership
    ChannelList = 0x10,
    JoinChannel = 0x11,
    LeaveChannel = 0x12,
    UserList = 0x13,
    UserJoined = 0x14,
    UserLeft = 0x15,

    // Chat
    TextMessage = 0x20,

    // Media
    VoiceFrame = 0x30,

    // File transfer
    FileOffer = 0x40,
    FileResponse = 0x41,
    FileChunk = 0x42,
    FileComplete = 0x43,
};

QString messageTypeName(MessageType type);

/// A control frame. `body` is schema-free JSON so the SDK can carry message
/// types it does not understand yet without a protocol bump.
struct Message {
    MessageType type{MessageType::Invalid};
    QJsonObject body;

    QJsonObject toJson() const;
    static Message fromJson(const QJsonObject &json);
};

struct UserInfo {
    quint32 id{0};
    QString nickname;
    /// 0 means the user is connected but not in a channel.
    quint32 channelId{0};

    QJsonObject toJson() const;
    static UserInfo fromJson(const QJsonObject &json);
};

struct ChannelInfo {
    quint32 id{0};
    QString name;
    quint32 userCount{0};

    QJsonObject toJson() const;
    static ChannelInfo fromJson(const QJsonObject &json);
};

/// A chat line. The timestamp is server-assigned epoch milliseconds.
struct TextEvent {
    quint32 channelId{0};
    quint32 senderId{0};
    QString sender;
    QString text;
    qint64 timestampMs{0};

    QJsonObject toJson() const;
    static TextEvent fromJson(const QJsonObject &json);
};

/// Serialises a message into a length-prefixed frame ready for socket write.
QByteArray encodeFrame(const Message &message);

/// Parses a frame *without* its 4-byte length prefix. Returns false for
/// malformed JSON or an empty payload; the caller keeps the raw bytes when it
/// needs to relay them untouched.
bool decodeFrame(const QByteArray &payload, Message &out);

/// Factories for the messages the server and client exchange. Building them
/// in one place keeps field names from drifting between components.
namespace messages {

Message hello(const QString &nickname);
Message welcome(quint32 userId, const QString &nickname, const QString &serverName);
Message error(const QString &code, const QString &description);

Message joinChannel(quint32 channelId);
Message leaveChannel();
Message channelList(const QVector<ChannelInfo> &channels);
Message userList(quint32 channelId, const QVector<UserInfo> &users);
Message userJoined(const UserInfo &user);
Message userLeft(quint32 userId, quint32 channelId);

Message text(quint32 channelId, quint32 senderId, const QString &sender,
             const QString &text, qint64 timestampMs = 0);

Message ping(quint32 nonce);
Message pong(quint32 nonce);

} // namespace messages
} // namespace opentalk

Q_DECLARE_METATYPE(opentalk::Message)
Q_DECLARE_METATYPE(opentalk::UserInfo)
Q_DECLARE_METATYPE(opentalk::ChannelInfo)
