#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpSocket>
#include <QtEndian>
#include <QtTest>

#include "Server.h"
#include "opentalk/protocol.h"

using namespace opentalk;

namespace {

/// Minimal protocol client used to drive the real server over a socket.
class TestClient
{
public:
    explicit TestClient(quint16 port)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        m_connected = m_socket.waitForConnected(3000);
    }

    bool isConnected() const { return m_connected; }

    void send(const Message &message)
    {
        m_socket.write(encodeFrame(message));
        m_socket.flush();
    }

    void disconnectFromServer() { m_socket.disconnectFromHost(); }

    /// Waits for a message of `type`, discarding nothing: unmatched messages
    /// stay queued so later assertions can still see them.
    bool waitFor(MessageType type, Message &out, int timeoutMs = 3000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            drain();
            for (int i = 0; i < m_inbox.size(); ++i) {
                if (m_inbox.at(i).type == type) {
                    out = m_inbox.takeAt(i);
                    return true;
                }
            }
            QTest::qWait(10);
        }
        drain();
        for (int i = 0; i < m_inbox.size(); ++i) {
            if (m_inbox.at(i).type == type) {
                out = m_inbox.takeAt(i);
                return true;
            }
        }
        return false;
    }

    bool sawType(MessageType type) const
    {
        for (const Message &message : m_inbox) {
            if (message.type == type) {
                return true;
            }
        }
        return false;
    }

private:
    void drain()
    {
        m_buffer.append(m_socket.readAll());
        while (m_buffer.size() >= 4) {
            const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());
            if (length == 0 || length > kMaxFrameSize) {
                m_buffer.clear();
                return;
            }
            if (static_cast<quint32>(m_buffer.size()) < length + 4) {
                return;
            }
            const QByteArray payload = m_buffer.mid(4, static_cast<int>(length));
            m_buffer.remove(0, static_cast<int>(length) + 4);
            Message message;
            if (decodeFrame(payload, message)) {
                m_inbox.append(message);
            }
        }
    }

    QTcpSocket m_socket;
    QByteArray m_buffer;
    QList<Message> m_inbox;
    bool m_connected{false};
};

} // namespace

class ServerTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void handshakeReturnsWelcomeAndChannels();
    void textIsRelayedToEveryoneInChannel();
    void senderIdentityCannotBeSpoofed();
    void unauthenticatedTrafficIsRefused();
    void wrongProtocolVersionIsRefused();
    void duplicateNicknamesAreDisambiguated();
    void fileOfferNameIsSanitised();
    void oversizedFileChunkIsRejected();
    void voiceFramesAreRelayedWithSenderStamp();
    void fileChunksAreRelayed();
    void serverPingsConnectedSessions();

private:
    void login(TestClient &client, const QString &nickname);
    void joinChannel(TestClient &client, quint32 channelId);

    opentalk::Server *m_server{nullptr};
    quint16 m_port{0};
};

void ServerTest::init()
{
    opentalk::ServerConfig config;
    config.port = 0; // let the OS pick a free port
    config.name = QStringLiteral("Test Server");
    // Ping quickly so the heartbeat test does not wait 15 seconds, while
    // keeping the idle timeout long enough not to interfere with other tests.
    config.heartbeatIntervalMs = 100;
    config.idleTimeoutMs = 60000;
    m_server = new opentalk::Server(config);
    QVERIFY(m_server->start());
    m_port = m_server->port();
    QVERIFY(m_port != 0);
}

void ServerTest::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

void ServerTest::login(TestClient &client, const QString &nickname)
{
    if (!client.isConnected()) {
        return;
    }
    client.send(messages::hello(nickname));
    Message welcome;
    if (client.waitFor(MessageType::Welcome, welcome)) {
        return;
    }
}

void ServerTest::joinChannel(TestClient &client, quint32 channelId)
{
    client.send(messages::joinChannel(channelId));
    Message list;
    if (client.waitFor(MessageType::UserList, list)) {
        return;
    }
}

void ServerTest::handshakeReturnsWelcomeAndChannels()
{
    TestClient client(m_port);
    QVERIFY(client.isConnected());

    client.send(messages::hello(QStringLiteral("alice")));

    Message welcome;
    QVERIFY(client.waitFor(MessageType::Welcome, welcome));
    QCOMPARE(welcome.body.value(QLatin1String("nickname")).toString(),
             QStringLiteral("alice"));
    QCOMPARE(welcome.body.value(QLatin1String("serverName")).toString(),
             QStringLiteral("Test Server"));
    QVERIFY(welcome.body.value(QLatin1String("userId")).toInteger(0) > 0);

    Message channels;
    QVERIFY(client.waitFor(MessageType::ChannelList, channels));
    const QJsonArray list = channels.body.value(QLatin1String("channels")).toArray();
    QCOMPARE(list.size(), 3);
}

void ServerTest::textIsRelayedToEveryoneInChannel()
{
    TestClient alice(m_port);
    TestClient bob(m_port);
    login(alice, QStringLiteral("alice"));
    login(bob, QStringLiteral("bob"));

    joinChannel(alice, 1);
    joinChannel(bob, 1);

    Message joined;
    QVERIFY(alice.waitFor(MessageType::UserJoined, joined));
    QCOMPARE(joined.body.value(QLatin1String("nickname")).toString(),
             QStringLiteral("bob"));

    alice.send(messages::text(1, 0, QStringLiteral("ignored"), QStringLiteral("hello")));

    Message aliceText;
    QVERIFY(alice.waitFor(MessageType::TextMessage, aliceText));
    QCOMPARE(aliceText.body.value(QLatin1String("text")).toString(),
             QStringLiteral("hello"));
    QCOMPARE(aliceText.body.value(QLatin1String("sender")).toString(),
             QStringLiteral("alice"));

    Message bobText;
    QVERIFY(bob.waitFor(MessageType::TextMessage, bobText));
    QCOMPARE(bobText.body.value(QLatin1String("text")).toString(),
             QStringLiteral("hello"));
    QVERIFY(bobText.body.value(QLatin1String("timestamp")).toInteger(0) > 0);
}

void ServerTest::senderIdentityCannotBeSpoofed()
{
    TestClient alice(m_port);
    TestClient bob(m_port);
    login(alice, QStringLiteral("alice"));
    login(bob, QStringLiteral("bob"));
    joinChannel(alice, 1);
    joinChannel(bob, 1);
    QTest::qWait(50);

    Message spoof = messages::text(1, 1, QStringLiteral("alice"), QStringLiteral("i am alice"));
    bob.send(spoof);

    Message received;
    QVERIFY(alice.waitFor(MessageType::TextMessage, received));
    QCOMPARE(received.body.value(QLatin1String("text")).toString(),
             QStringLiteral("i am alice"));
    // The server must overwrite the claimed identity with the real one.
    QCOMPARE(received.body.value(QLatin1String("sender")).toString(),
             QStringLiteral("bob"));
}

void ServerTest::unauthenticatedTrafficIsRefused()
{
    TestClient client(m_port);
    QVERIFY(client.isConnected());

    client.send(messages::joinChannel(1));

    Message error;
    QVERIFY(client.waitFor(MessageType::Error, error));
    QCOMPARE(error.body.value(QLatin1String("code")).toString(),
             QStringLiteral("not-authenticated"));
}

void ServerTest::wrongProtocolVersionIsRefused()
{
    TestClient client(m_port);
    QVERIFY(client.isConnected());

    Message hello = messages::hello(QStringLiteral("future"));
    hello.body.insert(QLatin1String("version"), 999);
    client.send(hello);

    Message error;
    QVERIFY(client.waitFor(MessageType::Error, error));
    QCOMPARE(error.body.value(QLatin1String("code")).toString(),
             QStringLiteral("unsupported-version"));
}

void ServerTest::duplicateNicknamesAreDisambiguated()
{
    TestClient first(m_port);
    TestClient second(m_port);
    first.send(messages::hello(QStringLiteral("alex")));
    second.send(messages::hello(QStringLiteral("alex")));

    Message firstWelcome;
    QVERIFY(first.waitFor(MessageType::Welcome, firstWelcome));
    QCOMPARE(firstWelcome.body.value(QLatin1String("nickname")).toString(),
             QStringLiteral("alex"));

    Message secondWelcome;
    QVERIFY(second.waitFor(MessageType::Welcome, secondWelcome));
    QCOMPARE(secondWelcome.body.value(QLatin1String("nickname")).toString(),
             QStringLiteral("alex-2"));
}

void ServerTest::fileOfferNameIsSanitised()
{
    TestClient alice(m_port);
    TestClient bob(m_port);
    login(alice, QStringLiteral("alice"));
    login(bob, QStringLiteral("bob"));
    joinChannel(alice, 1);
    joinChannel(bob, 1);
    QTest::qWait(50);

    FileOffer offer;
    offer.transferId = QStringLiteral("transfer-1");
    offer.fileName = QStringLiteral("../../evil.sh");
    offer.fileSize = 1024;
    offer.sha256 = QString(64, QLatin1Char('a'));
    alice.send(messages::fileOffer(offer));

    Message received;
    QVERIFY(bob.waitFor(MessageType::FileOffer, received));
    QCOMPARE(received.body.value(QLatin1String("fileName")).toString(),
             QStringLiteral("evil.sh"));
    QCOMPARE(received.body.value(QLatin1String("sender")).toString(),
             QStringLiteral("alice"));
}

void ServerTest::oversizedFileChunkIsRejected()
{
    TestClient alice(m_port);
    login(alice, QStringLiteral("alice"));
    joinChannel(alice, 1);
    QTest::qWait(50);

    FileChunk chunk;
    chunk.transferId = QStringLiteral("transfer-2");
    chunk.senderId = 1;
    chunk.index = 0;
    chunk.data = QByteArray(300 * 1024, 'x'); // above the 256 KiB cap
    alice.send(messages::fileChunk(chunk));

    Message error;
    QVERIFY(alice.waitFor(MessageType::Error, error));
    QCOMPARE(error.body.value(QLatin1String("code")).toString(),
             QStringLiteral("chunk-too-large"));
}

void ServerTest::voiceFramesAreRelayedWithSenderStamp()
{
    TestClient alice(m_port);
    TestClient bob(m_port);
    login(alice, QStringLiteral("alice"));
    login(bob, QStringLiteral("bob"));
    joinChannel(alice, 1);
    joinChannel(bob, 1);
    QTest::qWait(50);

    VoicePacket packet;
    packet.channelId = 1;
    packet.sequence = 42;
    packet.payload = QByteArrayLiteral("opus-bytes");
    alice.send(messages::voice(packet));

    Message received;
    QVERIFY(bob.waitFor(MessageType::VoiceFrame, received));
    const VoicePacket relayed = VoicePacket::fromJson(received.body);
    QCOMPARE(relayed.payload, QByteArrayLiteral("opus-bytes"));
    QCOMPARE(relayed.sequence, 42u);
    QCOMPARE(relayed.sender, QStringLiteral("alice"));
    QCOMPARE(relayed.channelId, 1u);
}

void ServerTest::fileChunksAreRelayed()
{
    TestClient alice(m_port);
    TestClient bob(m_port);
    login(alice, QStringLiteral("alice"));
    login(bob, QStringLiteral("bob"));
    joinChannel(alice, 1);
    joinChannel(bob, 1);
    QTest::qWait(50);

    FileOffer offer;
    offer.transferId = QStringLiteral("transfer-9");
    offer.fileName = QStringLiteral("notes.txt");
    offer.fileSize = 64;
    offer.sha256 = QString(64, QLatin1Char('b'));
    alice.send(messages::fileOffer(offer));

    FileChunk chunk;
    chunk.transferId = QStringLiteral("transfer-9");
    chunk.index = 0;
    chunk.last = true;
    chunk.data = QByteArray(64, 'z');
    alice.send(messages::fileChunk(chunk));

    Message relayedOffer;
    QVERIFY(bob.waitFor(MessageType::FileOffer, relayedOffer));
    QCOMPARE(relayedOffer.body.value(QLatin1String("fileName")).toString(),
             QStringLiteral("notes.txt"));

    Message relayedChunk;
    QVERIFY(bob.waitFor(MessageType::FileChunk, relayedChunk));
    const FileChunk received = FileChunk::fromJson(relayedChunk.body);
    QCOMPARE(received.data, QByteArray(64, 'z'));
    QVERIFY(received.last);
    QCOMPARE(received.transferId, QStringLiteral("transfer-9"));
}

void ServerTest::serverPingsConnectedSessions()
{
    TestClient client(m_port);
    login(client, QStringLiteral("alice"));

    Message ping;
    QVERIFY(client.waitFor(MessageType::Ping, ping, 5000));
}

QTEST_GUILESS_MAIN(ServerTest)

#include "server_test.moc"
