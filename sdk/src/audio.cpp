#include "opentalk/audio.h"

#include <QVector>

// Linux packages ship opus/opus.h; vcpkg and the Windows binaries ship opus.h.
#if defined(__has_include)
#  if __has_include(<opus/opus.h>)
#    include <opus/opus.h>
#  else
#    include <opus.h>
#  endif
#else
#  include <opus.h>
#endif

namespace opentalk {

AudioEncoder::AudioEncoder(int bitrateBps)
{
    int error = OPUS_OK;
    m_state = opus_encoder_create(kAudioSampleRate, kAudioChannels,
                                  OPUS_APPLICATION_VOIP, &error);
    if (m_state == nullptr || error != OPUS_OK) {
        m_error = QStringLiteral("Opus encoder init failed: %1")
                      .arg(QString::fromLatin1(opus_strerror(error)));
        m_state = nullptr;
        return;
    }

    auto *encoder = static_cast<::OpusEncoder *>(m_state);
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(bitrateBps));
    // Voice tuning: favour intelligibility, spend a little budget on FEC so a
    // single lost packet is recoverable, and keep the complexity modest for
    // low-power clients.
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(encoder, OPUS_SET_DTX(1));
}

AudioEncoder::~AudioEncoder()
{
    if (m_state != nullptr) {
        opus_encoder_destroy(static_cast<::OpusEncoder *>(m_state));
    }
}

QByteArray AudioEncoder::encode(const QByteArray &pcm)
{
    if (m_state == nullptr) {
        return {};
    }
    if (pcm.size() != kAudioBytesPerFrame) {
        m_error = QStringLiteral("Expected %1 bytes of PCM, got %2")
                      .arg(kAudioBytesPerFrame)
                      .arg(pcm.size());
        return {};
    }

    // 1275 bytes is the largest possible Opus packet; a 20 ms frame at 28 kbps
    // uses roughly 70, so this bound is generous.
    QByteArray packet(1275, Qt::Uninitialized);
    const int encoded = opus_encode(
        static_cast<::OpusEncoder *>(m_state),
        reinterpret_cast<const opus_int16 *>(pcm.constData()),
        kAudioSamplesPerFrame,
        reinterpret_cast<unsigned char *>(packet.data()),
        packet.size());

    if (encoded < 0) {
        m_error = QStringLiteral("Opus encode failed: %1")
                      .arg(QString::fromLatin1(opus_strerror(encoded)));
        return {};
    }
    packet.resize(encoded);
    return packet;
}

AudioDecoder::AudioDecoder()
{
    int error = OPUS_OK;
    m_state = opus_decoder_create(kAudioSampleRate, kAudioChannels, &error);
    if (error != OPUS_OK) {
        m_state = nullptr;
    }
}

AudioDecoder::~AudioDecoder()
{
    if (m_state != nullptr) {
        opus_decoder_destroy(static_cast<::OpusDecoder *>(m_state));
    }
}

QByteArray AudioDecoder::decode(const QByteArray &packet)
{
    if (m_state == nullptr || packet.isEmpty()) {
        return {};
    }

    QByteArray pcm(kAudioBytesPerFrame, Qt::Uninitialized);
    const int samples = opus_decode(
        static_cast<::OpusDecoder *>(m_state),
        reinterpret_cast<const unsigned char *>(packet.constData()),
        packet.size(),
        reinterpret_cast<opus_int16 *>(pcm.data()),
        kAudioSamplesPerFrame,
        0);

    if (samples < 0) {
        return {};
    }
    pcm.resize(samples * 2);
    return pcm;
}

QByteArray AudioDecoder::concealPacketLoss()
{
    if (m_state == nullptr) {
        return {};
    }

    QByteArray pcm(kAudioBytesPerFrame, Qt::Uninitialized);
    const int samples = opus_decode(
        static_cast<::OpusDecoder *>(m_state),
        nullptr, 0,
        reinterpret_cast<opus_int16 *>(pcm.data()),
        kAudioSamplesPerFrame,
        0);

    if (samples < 0) {
        return {};
    }
    pcm.resize(samples * 2);
    return pcm;
}

JitterBuffer::JitterBuffer(int maxPackets)
    : m_maxPackets(maxPackets > 0 ? maxPackets : 1)
{
}

void JitterBuffer::push(quint32 sequence, const QByteArray &packet)
{
    // Anything already played is worthless; keep the buffer from filling with
    // stale audio during a reconnect.
    if (m_started && sequence < m_next) {
        return;
    }
    m_packets.insert(sequence, packet);
    while (static_cast<int>(m_packets.size()) > m_maxPackets) {
        m_packets.erase(m_packets.begin());
    }
}

bool JitterBuffer::pop(QByteArray &packet, int &lost)
{
    packet.clear();
    lost = 0;

    if (m_packets.isEmpty()) {
        m_started = false;
        return false;
    }

    if (!m_started) {
        m_next = m_packets.firstKey();
        m_started = true;
    }

    // A long gap usually means the sender restarted its sequence. Concealing
    // hundreds of frames would just play noise, so jump forward instead.
    const quint32 oldest = m_packets.firstKey();
    if (oldest > m_next && oldest - m_next > static_cast<quint32>(m_maxPackets)) {
        m_next = oldest;
    }

    if (m_packets.contains(m_next)) {
        packet = m_packets.take(m_next);
        ++m_next;
        return true;
    }

    lost = 1;
    ++m_next;
    return true;
}

void JitterBuffer::clear()
{
    m_packets.clear();
    m_started = false;
    m_next = 0;
}

struct VoiceReceiver::SenderState {
    JitterBuffer buffer;
    AudioDecoder decoder;
    bool started{false};
};

VoiceReceiver::VoiceReceiver() = default;
VoiceReceiver::~VoiceReceiver() = default;

std::unique_ptr<VoiceReceiver::SenderState> VoiceReceiver::takeSender(quint32 senderId)
{
    auto it = m_senders.find(senderId);
    if (it == m_senders.end()) {
        return nullptr;
    }
    return std::move(it->second);
}

void VoiceReceiver::pushPacket(quint32 senderId, quint32 sequence,
                               const QByteArray &payload)
{
    auto it = m_senders.find(senderId);
    if (it == m_senders.end()) {
        // Keep the number of live decoders bounded even if a malformed relay
        // claims thousands of senders.
        if (m_senders.size() >= 32) {
            return;
        }
        it = m_senders.emplace(senderId, std::make_unique<SenderState>()).first;
    }
    it->second->buffer.push(sequence, payload);
    it->second->started = true;
}

QByteArray VoiceReceiver::nextFrame()
{
    const QByteArray silence(kAudioBytesPerFrame, '\0');
    if (m_senders.empty()) {
        return silence;
    }

    QVector<qint32> mix(kAudioSamplesPerFrame, 0);
    bool anyAudio = false;

    for (auto it = m_senders.begin(); it != m_senders.end(); ++it) {
        SenderState &state = *it->second;

        QByteArray pcm;
        bool contribute = false;

        QByteArray packet;
        int lost = 0;
        if (state.buffer.pop(packet, lost)) {
            if (lost > 0 || packet.isEmpty()) {
                pcm = state.decoder.concealPacketLoss();
            } else {
                pcm = state.decoder.decode(packet);
            }
            contribute = !pcm.isEmpty();
        } else if (state.started) {
            // Buffer momentarily empty: conceal so the stream does not click.
            pcm = state.decoder.concealPacketLoss();
            contribute = !pcm.isEmpty();
        }

        if (!contribute || pcm.size() < kAudioBytesPerFrame) {
            continue;
        }

        anyAudio = true;
        const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
        for (int i = 0; i < kAudioSamplesPerFrame; ++i) {
            mix[i] += samples[i];
        }
    }

    if (!anyAudio) {
        return silence;
    }

    QByteArray output(kAudioBytesPerFrame, Qt::Uninitialized);
    auto *out = reinterpret_cast<qint16 *>(output.data());
    for (int i = 0; i < kAudioSamplesPerFrame; ++i) {
        out[i] = static_cast<qint16>(qBound(-32768, mix[i], 32767));
    }
    return output;
}

void VoiceReceiver::removeSender(quint32 senderId)
{
    m_senders.erase(senderId);
}

void VoiceReceiver::clear()
{
    m_senders.clear();
}

QList<quint32> VoiceReceiver::activeSenders() const
{
    QList<quint32> senders;
    senders.reserve(static_cast<int>(m_senders.size()));
    for (const auto &entry : m_senders) {
        senders.append(entry.first);
    }
    return senders;
}

} // namespace opentalk
