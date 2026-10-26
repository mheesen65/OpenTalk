#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include "opentalk/audio.h"

class QAudioSink;
class QAudioSource;
class QIODevice;

/// Microphone and speaker pipeline.
///
/// Capture is gated by push-to-talk and mute; encoded packets are emitted as
/// 20 ms Opus frames. Playback accepts decoded packets and writes them to the
/// speaker with a bounded queue so a slow device cannot grow memory without
/// limit.
///
/// Only built when Qt Multimedia is available; the client falls back to a
/// text-only build otherwise (see OPENTALK_ENABLE_AUDIO in CMakeLists.txt).
class AudioEngine : public QObject
{
    Q_OBJECT

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    /// True when a default input and output device both exist.
    static bool isSupported();
    static QString unsupportedReason();

    bool start(QString &error);
    void stop();
    bool isRunning() const { return m_running; }

    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

    /// Push-to-talk gate: frames are only sent while this is true.
    void setTransmitting(bool transmitting);
    bool isTransmitting() const { return m_transmitting; }

    /// Queues one decoded 20 ms PCM frame for playback. Decoding and mixing
    /// belong to opentalk::VoiceReceiver, not to the device layer.
    void queuePcm(const QByteArray &pcm);

signals:
    void frameEncoded(const QByteArray &payload, quint32 sequence, qint64 timestampMs);
    void error(const QString &message);
    /// Normalised 0..1 input level, for the UI meter.
    void inputLevel(double level);

private slots:
    void onCaptureReady();
    void onPlaybackReady();

private:
    void pumpPlayback();

    opentalk::AudioEncoder m_encoder;

    QAudioSource *m_source{nullptr};
    QAudioSink *m_sink{nullptr};
    QIODevice *m_input{nullptr};
    QIODevice *m_output{nullptr};

    QByteArray m_captureBuffer;
    QByteArray m_playbackBuffer;
    quint32 m_sequence{0};
    bool m_running{false};
    bool m_muted{false};
    bool m_transmitting{false};
};
