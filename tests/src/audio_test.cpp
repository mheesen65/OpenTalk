#include <QtMath>
#include <QtTest>

#include "opentalk/audio.h"

using namespace opentalk;

namespace {

constexpr double kTwoPi = 6.283185307179586;

QByteArray tone(double frequency, int frameCount = 1, double amplitude = 0.35)
{
    QByteArray pcm(kAudioBytesPerFrame * frameCount, Qt::Uninitialized);
    auto *samples = reinterpret_cast<qint16 *>(pcm.data());
    for (int f = 0; f < frameCount; ++f) {
        for (int i = 0; i < kAudioSamplesPerFrame; ++i) {
            const double t = (f * kAudioSamplesPerFrame + i) / double(kAudioSampleRate);
            samples[f * kAudioSamplesPerFrame + i] =
                static_cast<qint16>(amplitude * 32767.0 * qSin(kTwoPi * frequency * t));
        }
    }
    return pcm;
}

double rms(const QByteArray &pcm)
{
    if (pcm.size() < 2) {
        return 0.0;
    }
    const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
    const int count = pcm.size() / 2;
    double energy = 0.0;
    for (int i = 0; i < count; ++i) {
        const double value = samples[i] / 32768.0;
        energy += value * value;
    }
    return qSqrt(energy / count);
}

QByteArray silence()
{
    return QByteArray(kAudioBytesPerFrame, '\0');
}

} // namespace

class AudioTest : public QObject
{
    Q_OBJECT

private slots:
    void encoderProducesFrames();
    void codecRoundTripPreservesEnergy();
    void decoderConcealsLoss();
    void jitterBufferReordersPackets();
    void jitterBufferReportsGap();
    void jitterBufferDropsLatePackets();
    void receiverMixesTwoSenders();
    void receiverReturnsSilenceWhenIdle();
    void receiverForgetsSenders();
};

void AudioTest::encoderProducesFrames()
{
    AudioEncoder encoder;
    QVERIFY(encoder.isValid());

    const QByteArray pcm = tone(440.0);
    const QByteArray packet = encoder.encode(pcm);

    QVERIFY(!packet.isEmpty());
    // 20 ms at 28 kbps is roughly 70 bytes; anything near the frame size would
    // mean the encoder is not compressing at all.
    QVERIFY2(packet.size() < 400, qPrintable(QString::number(packet.size())));
}

void AudioTest::codecRoundTripPreservesEnergy()
{
    AudioEncoder encoder;
    AudioDecoder decoder;
    QVERIFY(encoder.isValid());
    QVERIFY(decoder.isValid());

    const QByteArray pcm = tone(440.0);
    const QByteArray packet = encoder.encode(pcm);
    QVERIFY(!packet.isEmpty());

    const QByteArray decoded = decoder.decode(packet);
    QCOMPARE(decoded.size(), kAudioBytesPerFrame);

    // Lossy codec, so compare loudness rather than samples. Opus at 28 kbps
    // keeps a 440 Hz tone well within a few dB.
    const double originalLevel = rms(pcm);
    const double decodedLevel = rms(decoded);
    QVERIFY(originalLevel > 0.1);
    QVERIFY2(decodedLevel > originalLevel * 0.5,
             qPrintable(QStringLiteral("decoded %1 vs original %2")
                            .arg(decodedLevel)
                            .arg(originalLevel)));
}

void AudioTest::decoderConcealsLoss()
{
    AudioEncoder encoder;
    AudioDecoder decoder;
    QVERIFY(encoder.isValid());
    QVERIFY(decoder.isValid());

    QVERIFY(!encoder.encode(tone(300.0)).isEmpty());
    const QByteArray concealed = decoder.concealPacketLoss();
    QCOMPARE(concealed.size(), kAudioBytesPerFrame);
}

void AudioTest::jitterBufferReordersPackets()
{
    JitterBuffer buffer;
    buffer.push(1, QByteArrayLiteral("one"));
    buffer.push(0, QByteArrayLiteral("zero"));
    buffer.push(2, QByteArrayLiteral("two"));
    QCOMPARE(buffer.size(), 3);

    QByteArray packet;
    int lost = 0;

    QVERIFY(buffer.pop(packet, lost));
    QCOMPARE(lost, 0);
    QCOMPARE(packet, QByteArrayLiteral("zero"));

    QVERIFY(buffer.pop(packet, lost));
    QCOMPARE(lost, 0);
    QCOMPARE(packet, QByteArrayLiteral("one"));

    QVERIFY(buffer.pop(packet, lost));
    QCOMPARE(lost, 0);
    QCOMPARE(packet, QByteArrayLiteral("two"));

    QVERIFY(!buffer.pop(packet, lost));
}

void AudioTest::jitterBufferReportsGap()
{
    JitterBuffer buffer;
    buffer.push(0, QByteArrayLiteral("zero"));
    buffer.push(2, QByteArrayLiteral("two"));

    QByteArray packet;
    int lost = 0;

    QVERIFY(buffer.pop(packet, lost));
    QCOMPARE(packet, QByteArrayLiteral("zero"));

    QVERIFY(buffer.pop(packet, lost));
    QCOMPARE(lost, 1);
    QVERIFY(packet.isEmpty());

    QVERIFY(buffer.pop(packet, lost));
    QCOMPARE(lost, 0);
    QCOMPARE(packet, QByteArrayLiteral("two"));
}

void AudioTest::jitterBufferDropsLatePackets()
{
    JitterBuffer buffer;
    buffer.push(5, QByteArrayLiteral("five"));

    QByteArray packet;
    int lost = 0;
    QVERIFY(buffer.pop(packet, lost));
    QCOMPARE(packet, QByteArrayLiteral("five"));

    buffer.push(3, QByteArrayLiteral("late"));
    QCOMPARE(buffer.size(), 0);
}

void AudioTest::receiverMixesTwoSenders()
{
    AudioEncoder first;
    AudioEncoder second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());

    VoiceReceiver receiver;
    const QByteArray toneA = tone(440.0);
    const QByteArray toneB = tone(660.0);

    for (quint32 sequence = 0; sequence < 5; ++sequence) {
        // Encode a fresh frame each time so the encoders advance naturally.
        receiver.pushPacket(1, sequence, first.encode(toneA));
        receiver.pushPacket(2, sequence, second.encode(toneB));
    }

    QCOMPARE(receiver.activeSenders().size(), 2);

    bool heardAudio = false;
    for (int frame = 0; frame < 5; ++frame) {
        const QByteArray mixed = receiver.nextFrame();
        QCOMPARE(mixed.size(), kAudioBytesPerFrame);
        if (rms(mixed) > 0.01) {
            heardAudio = true;
        }
    }
    QVERIFY(heardAudio);
}

void AudioTest::receiverReturnsSilenceWhenIdle()
{
    VoiceReceiver receiver;
    QVERIFY(receiver.activeSenders().isEmpty());
    QCOMPARE(receiver.nextFrame(), silence());
}

void AudioTest::receiverForgetsSenders()
{
    AudioEncoder encoder;
    VoiceReceiver receiver;

    receiver.pushPacket(7, 0, encoder.encode(tone(500.0)));
    QCOMPARE(receiver.activeSenders().size(), 1);

    receiver.removeSender(7);
    QVERIFY(receiver.activeSenders().isEmpty());

    receiver.pushPacket(8, 0, encoder.encode(tone(500.0)));
    receiver.clear();
    QVERIFY(receiver.activeSenders().isEmpty());
}

QTEST_GUILESS_MAIN(AudioTest)

#include "audio_test.moc"
