#include "opentalk/transfer.h"

#include <QCryptographicHash>
#include <QFileInfo>

namespace opentalk {
namespace {

constexpr qint64 kHashBlock = 256 * 1024;

} // namespace

QString fileSha256(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(kHashBlock);
        if (block.isEmpty() && file.error() != QFile::NoError) {
            if (error != nullptr) {
                *error = file.errorString();
            }
            return {};
        }
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
}

// -------------------------------------------------------- OutgoingTransfer ---

OutgoingTransfer::OutgoingTransfer(const QString &id, const QString &filePath,
                                   int chunkSize, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_file(filePath)
    , m_chunkSize(chunkSize > 0 ? chunkSize : kDefaultChunkSize)
{
}

OutgoingTransfer::~OutgoingTransfer() = default;

QString OutgoingTransfer::fileName() const
{
    return QFileInfo(m_file).fileName();
}

bool OutgoingTransfer::open(QString &error)
{
    if (!m_file.open(QIODevice::ReadOnly)) {
        error = m_file.errorString();
        return false;
    }

    m_size = m_file.size();
    m_sha256 = fileSha256(m_file.fileName(), &error);
    if (m_sha256.isEmpty()) {
        m_file.close();
        return false;
    }
    m_file.seek(0);
    m_sent = 0;
    return true;
}

QByteArray OutgoingTransfer::nextChunk(bool &last)
{
    last = false;
    if (!m_file.isOpen() || m_sent >= m_size) {
        return {};
    }

    const qint64 remaining = m_size - m_sent;
    const qint64 readSize = qMin<qint64>(m_chunkSize, remaining);
    const QByteArray chunk = m_file.read(readSize);
    if (chunk.size() != readSize) {
        // A short read mid-file means the file changed under us; abort rather
        // than hand the peer a corrupt stream.
        return {};
    }

    m_sent += chunk.size();
    last = m_sent >= m_size;
    emit progress(m_sent, m_size);
    return chunk;
}

// -------------------------------------------------------- IncomingTransfer ---

IncomingTransfer::IncomingTransfer(const QString &id, const QString &fileName,
                                   qint64 expectedSize, const QString &expectedSha256,
                                   const QString &targetPath, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_fileName(fileName)
    , m_expectedSize(expectedSize)
    , m_expectedSha256(expectedSha256)
    , m_targetPath(targetPath)
    , m_file(targetPath)
{
}

IncomingTransfer::~IncomingTransfer() = default;

bool IncomingTransfer::open(QString &error)
{
    if (m_expectedSize < 0) {
        error = QStringLiteral("Invalid file size.");
        return false;
    }
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = m_file.errorString();
        return false;
    }
    m_received = 0;
    m_nextIndex = 0;
    m_finalised = false;
    return true;
}

bool IncomingTransfer::writeChunk(quint32 index, bool last, const QByteArray &data,
                                  QString &error)
{
    if (!m_file.isOpen()) {
        error = QStringLiteral("Transfer is not open.");
        return false;
    }
    if (index != m_nextIndex) {
        error = QStringLiteral("Chunk %1 arrived out of order (expected %2).")
                    .arg(index)
                    .arg(m_nextIndex);
        return false;
    }
    if (m_received + data.size() > m_expectedSize) {
        error = QStringLiteral("Chunk would exceed the declared file size.");
        return false;
    }
    if (m_file.write(data) != data.size()) {
        error = m_file.errorString();
        return false;
    }

    m_nextIndex = index + 1;
    m_received += data.size();
    if (last && m_received != m_expectedSize) {
        error = QStringLiteral("Final chunk reports %1 bytes, expected %2.")
                    .arg(m_received)
                    .arg(m_expectedSize);
        return false;
    }
    emit progress(m_received, m_expectedSize);
    return true;
}

bool IncomingTransfer::finalise(QString &error)
{
    if (m_finalised) {
        return true;
    }
    if (!m_file.isOpen()) {
        error = QStringLiteral("Transfer is not open.");
        return false;
    }
    m_file.close();

    if (m_received != m_expectedSize) {
        error = QStringLiteral("Incomplete transfer: %1 of %2 bytes.")
                    .arg(m_received)
                    .arg(m_expectedSize);
        abort();
        return false;
    }

    QString hashError;
    const QString actual = fileSha256(m_targetPath, &hashError);
    if (actual.isEmpty()) {
        error = QStringLiteral("Cannot verify file: %1").arg(hashError);
        abort();
        return false;
    }
    if (actual.compare(m_expectedSha256, Qt::CaseInsensitive) != 0) {
        error = QStringLiteral("SHA-256 mismatch: expected %1, got %2.")
                    .arg(m_expectedSha256, actual);
        abort();
        return false;
    }

    m_finalised = true;
    return true;
}

void IncomingTransfer::abort()
{
    if (m_file.isOpen()) {
        m_file.close();
    }
    QFile::remove(m_targetPath);
    m_received = 0;
    m_nextIndex = 0;
}

} // namespace opentalk
