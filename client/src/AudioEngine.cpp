#include "AudioEngine.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QDateTime>
#include <QIODevice>
#include <QMediaDevices>
#include <QtMath>

using namespace opentalk;

namespace {

constexpr int kPlaybackQueueBytes = kAudioBytesPerFrame * 25; // ~500 ms
constexpr int kCaptureBacklogBytes = kAudioBytesPerFrame * 20;

} // namespace

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine()
{
    stop();
}

bool AudioEngine::isSupported()
{
    return !QMediaDevices::audioInputs().isEmpty()
        && !QMediaDevices::audioOutputs().isEmpty();
}

QString AudioEngine::unsupportedReason()
{
    if (QMediaDevices::audioInputs().isEmpty()) {
        return QObject::tr("No microphone was found.");
    }
    if (QMediaDevices::audioOutputs().isEmpty()) {
        return QObject::tr("No speaker or headphone output was found.");
    }
    return {};
}

bool AudioEngine::start(QString &error)
{
    if (m_running) {
        return true;
    }
    if (!m_encoder.isValid()) {
        error = m_encoder.lastError();
        return false;
    }

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        error = tr("No microphone is available.");
        return false;
    }
    const QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (outputDevice.isNull()) {
        error = tr("No audio output is available.");
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(kAudioSampleRate);
    format.setChannelCount(kAudioChannels);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!inputDevice.isFormatSupported(format) || !outputDevice.isFormatSupported(format)) {
        error = tr("48 kHz mono audio is not supported by the system devices.");
        return false;
    }

    m_source = new QAudioSource(inputDevice, format, this);
    m_source->setBufferSize(kAudioBytesPerFrame * 6);
    m_input = m_source->start();
    if (m_input == nullptr) {
        error = tr("Cannot open the microphone (error %1).")
                    .arg(static_cast<int>(m_source->error()));
        stop();
        return false;
    }
    connect(m_input, &QIODevice::readyRead, this, &AudioEngine::onCaptureReady);

    m_sink = new QAudioSink(outputDevice, format, this);
    m_sink->setBufferSize(kAudioBytesPerFrame * 12);
    m_output = m_sink->start();
    if (m_output == nullptr) {
        error = tr("Cannot open the speaker (error %1).")
                    .arg(static_cast<int>(m_sink->error()));
        stop();
        return false;
    }
    connect(m_output, &QIODevice::readyRead, this, &AudioEngine::onPlaybackReady);

    m_captureBuffer.clear();
    m_playbackBuffer.clear();
    m_sequence = 0;
    m_running = true;
    return true;
}

void AudioEngine::stop()
{
    m_running = false;
    m_transmitting = false;

    if (m_input != nullptr) {
        disconnect(m_input, nullptr, this, nullptr);
        m_input = nullptr;
    }
    if (m_output != nullptr) {
        disconnect(m_output, nullptr, this, nullptr);
        m_output = nullptr;
    }
    if (m_source != nullptr) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    if (m_sink != nullptr) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    m_captureBuffer.clear();
    m_playbackBuffer.clear();
}

void AudioEngine::setMuted(bool muted)
{
    m_muted = muted;
    // Do not let pre-mute audio escape when unmuting.
    m_captureBuffer.clear();
}

void AudioEngine::setTransmitting(bool transmitting)
{
    m_transmitting = transmitting;
    if (!transmitting) {
        m_captureBuffer.clear();
    }
}

void AudioEngine::onCaptureReady()
{
    if (m_input == nullptr) {
        return;
    }
    m_captureBuffer.append(m_input->readAll());

    while (m_captureBuffer.size() >= kAudioBytesPerFrame) {
        const QByteArray frame = m_captureBuffer.left(kAudioBytesPerFrame);
        m_captureBuffer.remove(0, kAudioBytesPerFrame);

        const auto *samples = reinterpret_cast<const qint16 *>(frame.constData());
        double energy = 0.0;
        for (int i = 0; i < kAudioSamplesPerFrame; ++i) {
            const double value = samples[i] / 32768.0;
            energy += value * value;
        }
        emit inputLevel(qSqrt(energy / kAudioSamplesPerFrame));

        if (!m_transmitting || m_muted) {
            continue;
        }

        const QByteArray payload = m_encoder.encode(frame);
        if (payload.isEmpty()) {
            emit error(m_encoder.lastError());
            continue;
        }
        emit frameEncoded(payload, m_sequence++, QDateTime::currentMSecsSinceEpoch());
    }

    // If encoding falls behind, drop the oldest audio rather than send a burst
    // of stale speech.
    if (m_captureBuffer.size() > kCaptureBacklogBytes) {
        m_captureBuffer.remove(0, m_captureBuffer.size() - kCaptureBacklogBytes / 2);
    }
}

void AudioEngine::onPlaybackReady()
{
    pumpPlayback();
}

void AudioEngine::queuePcm(const QByteArray &pcm)
{
    if (pcm.isEmpty()) {
        return;
    }
    m_playbackBuffer.append(pcm);
    if (m_playbackBuffer.size() > kPlaybackQueueBytes) {
        m_playbackBuffer.remove(0, m_playbackBuffer.size() - kPlaybackQueueBytes);
    }
    pumpPlayback();
}

void AudioEngine::pumpPlayback()
{
    if (m_sink == nullptr || m_output == nullptr || !m_running) {
        return;
    }
    const qint64 freeBytes = m_sink->bytesFree();
    if (freeBytes <= 0 || m_playbackBuffer.isEmpty()) {
        return;
    }
    const qint64 toWrite = qMin<qint64>(freeBytes, m_playbackBuffer.size());
    const qint64 written = m_output->write(m_playbackBuffer.constData(), toWrite);
    if (written > 0) {
        m_playbackBuffer.remove(0, static_cast<int>(written));
    }
}
