#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

#include <map>
#include <memory>

#include "opentalk/protocol.h"

namespace opentalk {

/// Thin wrapper around the Opus encoder for one 20 ms mono frame at a time.
/// The Opus state is kept opaque so the public headers do not leak the C API.
class AudioEncoder
{
public:
    explicit AudioEncoder(int bitrateBps = kDefaultOpusBitrate);
    ~AudioEncoder();

    AudioEncoder(const AudioEncoder &) = delete;
    AudioEncoder &operator=(const AudioEncoder &) = delete;

    bool isValid() const { return m_state != nullptr; }
    QString lastError() const { return m_error; }

    /// Encodes exactly `kAudioBytesPerFrame` bytes of 16-bit little-endian
    /// mono PCM. Returns an empty array on error or wrong-sized input.
    QByteArray encode(const QByteArray &pcm);

private:
    void *m_state{nullptr};
    QString m_error;
};

/// Mirrors AudioEncoder. Also produces concealment audio so a dropped packet
/// becomes a short repeat instead of a click.
class AudioDecoder
{
public:
    AudioDecoder();
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder &) = delete;
    AudioDecoder &operator=(const AudioDecoder &) = delete;

    bool isValid() const { return m_state != nullptr; }

    /// Decodes one Opus packet into 16-bit mono PCM. Empty on failure.
    QByteArray decode(const QByteArray &packet);

    /// Concealment frame used when a packet never arrived.
    QByteArray concealPacketLoss();

private:
    void *m_state{nullptr};
};

/// Reorders voice packets by sequence number and reports gaps.
///
/// The socket delivers packets in order, but the server relays from several
/// senders and a slow decoder can starve; treating this as a real jitter
/// buffer keeps the client code simple and makes the ordering testable
/// without audio hardware.
class JitterBuffer
{
public:
    explicit JitterBuffer(int maxPackets = 12);

    /// Late packets (older than what was already played) are discarded.
    void push(quint32 sequence, const QByteArray &packet);

    /// Pops the next packet in order.
    /// @param packet empty when the gap must be concealed
    /// @param lost   number of missing packets to conceal before this one
    /// @return false when the buffer is empty
    bool pop(QByteArray &packet, int &lost);

    int size() const { return static_cast<int>(m_packets.size()); }
    void clear();

private:
    QMap<quint32, QByteArray> m_packets;
    quint32 m_next{0};
    bool m_started{false};
    int m_maxPackets{12};
};

/// Turns the packets of several speakers into a single playout stream.
///
/// Opus decoders are stateful per stream, so each sender needs its own decoder
/// and jitter buffer; this class owns that bookkeeping and mixes the decoded
/// frames into one 20 ms PCM frame suitable for a single audio sink.
class VoiceReceiver
{
public:
    VoiceReceiver();
    ~VoiceReceiver();

    VoiceReceiver(const VoiceReceiver &) = delete;
    VoiceReceiver &operator=(const VoiceReceiver &) = delete;

    void pushPacket(quint32 senderId, quint32 sequence, const QByteArray &payload);

    /// Produces exactly `kAudioBytesPerFrame` bytes of 16-bit mono PCM.
    /// Silence is returned when no sender has audio queued.
    QByteArray nextFrame();

    /// Drops all state for one sender (they left the channel).
    void removeSender(quint32 senderId);

    /// Drops all senders, e.g. on disconnect.
    void clear();

    QList<quint32> activeSenders() const;

private:
    struct SenderState;
    std::unique_ptr<SenderState> takeSender(quint32 senderId);

    std::map<quint32, std::unique_ptr<SenderState>> m_senders;
};

} // namespace opentalk
