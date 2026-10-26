#include <QJsonArray>
#include <QtEndian>
#include <QtTest>

#include "opentalk/protocol.h"

using namespace opentalk;

class ProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void framePrefixIsBigEndianLength();
    void roundTripsTextMessage();
    void roundTripsChannelList();
    void preservesUnknownMessageTypes();
    void helloCarriesProtocolVersion();
    void rejectsShortPayload();
    void rejectsMalformedJson();
    void rejectsNonObjectJson();
};

void ProtocolTest::framePrefixIsBigEndianLength()
{
    const Message message = messages::text(1, 7, QStringLiteral("ada"), QStringLiteral("hi"));
    const QByteArray frame = encodeFrame(message);

    QVERIFY(frame.size() > 5);
    const quint32 declaredLength = qFromBigEndian<quint32>(frame.constData());

    // The prefix counts the type byte plus the JSON body, not itself.
    QCOMPARE(static_cast<int>(declaredLength), frame.size() - 4);
    QCOMPARE(static_cast<quint8>(frame.at(4)), static_cast<quint8>(MessageType::TextMessage));
}

void ProtocolTest::roundTripsTextMessage()
{
    const Message original =
        messages::text(3, 42, QStringLiteral("grace"), QStringLiteral("hello, world"), 1700000000000);
    const QByteArray frame = encodeFrame(original);

    Message decoded;
    QVERIFY(decodeFrame(frame.mid(4), decoded));
    QCOMPARE(decoded.type, MessageType::TextMessage);

    const TextEvent event = TextEvent::fromJson(decoded.body);
    QCOMPARE(event.channelId, 3u);
    QCOMPARE(event.senderId, 42u);
    QCOMPARE(event.sender, QStringLiteral("grace"));
    QCOMPARE(event.text, QStringLiteral("hello, world"));
    QCOMPARE(event.timestampMs, Q_INT64_C(1700000000000));
}

void ProtocolTest::roundTripsChannelList()
{
    ChannelInfo lobby;
    lobby.id = 1;
    lobby.name = QStringLiteral("Lobby");
    lobby.userCount = 4;

    ChannelInfo support;
    support.id = 3;
    support.name = QStringLiteral("Support");

    Message decoded;
    QVERIFY(decodeFrame(encodeFrame(messages::channelList({lobby, support})).mid(4), decoded));
    QCOMPARE(decoded.type, MessageType::ChannelList);

    const QJsonArray array = decoded.body.value(QLatin1String("channels")).toArray();
    QCOMPARE(array.size(), 2);

    const ChannelInfo first = ChannelInfo::fromJson(array.at(0).toObject());
    QCOMPARE(first.id, 1u);
    QCOMPARE(first.name, QStringLiteral("Lobby"));
    QCOMPARE(first.userCount, 4u);
}

void ProtocolTest::preservesUnknownMessageTypes()
{
    // Forward compatibility: a future message type must survive a decode so an
    // older relay can pass it along instead of dropping the session.
    Message future;
    future.type = static_cast<MessageType>(0x7E);
    future.body = QJsonObject{{QLatin1String("payload"), 99}};

    Message decoded;
    QVERIFY(decodeFrame(encodeFrame(future).mid(4), decoded));
    QCOMPARE(static_cast<int>(decoded.type), 0x7E);
    QCOMPARE(decoded.body.value(QLatin1String("payload")).toInt(), 99);
}

void ProtocolTest::helloCarriesProtocolVersion()
{
    Message decoded;
    QVERIFY(decodeFrame(encodeFrame(messages::hello(QStringLiteral("linus"))).mid(4), decoded));
    QCOMPARE(decoded.type, MessageType::Hello);
    QCOMPARE(decoded.body.value(QLatin1String("nickname")).toString(), QStringLiteral("linus"));
    QCOMPARE(decoded.body.value(QLatin1String("version")).toInt(),
             static_cast<int>(kProtocolVersion));
}

void ProtocolTest::rejectsShortPayload()
{
    Message decoded;
    QVERIFY(!decodeFrame(QByteArray(), decoded));
    QVERIFY(!decodeFrame(QByteArray("\x01", 1), decoded));
    QVERIFY(!decodeFrame(QByteArray("\x01{", 2), decoded));
}

void ProtocolTest::rejectsMalformedJson()
{
    Message decoded;
    const QByteArray payload = QByteArray("\x20", 1) + QByteArray("this is not json");
    QVERIFY(!decodeFrame(payload, decoded));
}

void ProtocolTest::rejectsNonObjectJson()
{
    Message decoded;
    const QByteArray payload = QByteArray("\x20", 1) + QByteArray("[1,2,3]");
    QVERIFY(!decodeFrame(payload, decoded));
}

QTEST_GUILESS_MAIN(ProtocolTest)

#include "protocol_test.moc"
