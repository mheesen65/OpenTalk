#pragma once

#include <QFile>
#include <QObject>
#include <QString>

#include "opentalk/protocol.h"

namespace opentalk {

/// Streams a local file out as chunks. The SHA-256 is computed when the
/// transfer is opened so the receiver can verify the reassembled result and
/// the sender can advertise it in the offer.
class OutgoingTransfer : public QObject
{
    Q_OBJECT

public:
    OutgoingTransfer(const QString &id, const QString &filePath,
                     int chunkSize = kDefaultChunkSize, QObject *parent = nullptr);
    ~OutgoingTransfer() override;

    /// Opens the file and hashes it. On failure `error` explains why.
    bool open(QString &error);

    QString id() const { return m_id; }
    QString fileName() const;
    QString sha256() const { return m_sha256; }
    qint64 size() const { return m_size; }
    int chunkSize() const { return m_chunkSize; }
    qint64 bytesSent() const { return m_sent; }

    /// Reads the next chunk. `last` is true only when this chunk completes the
    /// file. Returns an empty array once the file is exhausted.
    QByteArray nextChunk(bool &last);

signals:
    void progress(qint64 sent, qint64 total);

private:
    QString m_id;
    QFile m_file;
    QString m_sha256;
    qint64 m_size{0};
    qint64 m_sent{0};
    int m_chunkSize{kDefaultChunkSize};
};

/// Receives chunks into a temporary file, enforcing order and bounds, then
/// verifies the size and SHA-256 before the file is kept.
class IncomingTransfer : public QObject
{
    Q_OBJECT

public:
    IncomingTransfer(const QString &id, const QString &fileName, qint64 expectedSize,
                     const QString &expectedSha256, const QString &targetPath,
                     QObject *parent = nullptr);
    ~IncomingTransfer() override;

    bool open(QString &error);

    /// Appends one chunk. Rejects out-of-order chunks, oversized data and
    /// writes past the declared size.
    bool writeChunk(quint32 index, bool last, const QByteArray &data, QString &error);

    /// Verifies length and hash. On mismatch the partial file is removed and
    /// `error` describes the failure.
    bool finalise(QString &error);

    /// Removes the partial file.
    void abort();

    QString id() const { return m_id; }
    QString fileName() const { return m_fileName; }
    QString targetPath() const { return m_targetPath; }
    qint64 bytesReceived() const { return m_received; }
    qint64 expectedSize() const { return m_expectedSize; }

signals:
    void progress(qint64 received, qint64 total);

private:
    QString m_id;
    QString m_fileName;
    qint64 m_expectedSize{0};
    QString m_expectedSha256;
    QString m_targetPath;
    QFile m_file;
    quint32 m_nextIndex{0};
    qint64 m_received{0};
    bool m_finalised{false};
};

/// Computes the SHA-256 of a file, streaming it rather than loading it whole.
QString fileSha256(const QString &path, QString *error = nullptr);

} // namespace opentalk
