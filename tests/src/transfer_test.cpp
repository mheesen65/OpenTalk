#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "opentalk/transfer.h"

using namespace opentalk;

class TransferTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTripPreservesContent();
    void detectsCorruption();
    void rejectsOutOfOrderChunks();
    void rejectsOversizedChunks();
    void rejectsWrongHash();

private:
    QString writeSourceFile(const QTemporaryDir &dir, int size);
};

QString TransferTest::writeSourceFile(const QTemporaryDir &dir, int size)
{
    const QString path = dir.filePath(QStringLiteral("source.bin"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    QByteArray payload(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) {
        payload[i] = static_cast<char>((i * 31 + 7) % 251);
    }
    file.write(payload);
    file.close();
    return path;
}

void TransferTest::roundTripPreservesContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString sourcePath = writeSourceFile(dir, 150000);
    QVERIFY(!sourcePath.isEmpty());

    OutgoingTransfer outgoing(QStringLiteral("t1"), sourcePath, 64 * 1024);
    QString error;
    QVERIFY2(outgoing.open(error), qPrintable(error));
    QCOMPARE(outgoing.size(), qint64(150000));
    QCOMPARE(outgoing.sha256().size(), 64);

    const QString targetPath = dir.filePath(QStringLiteral("received.bin"));
    IncomingTransfer incoming(QStringLiteral("t1"), QStringLiteral("source.bin"),
                              outgoing.size(), outgoing.sha256(), targetPath);
    QVERIFY2(incoming.open(error), qPrintable(error));

    quint32 index = 0;
    QByteArray chunk;
    do {
        bool last = false;
        chunk = outgoing.nextChunk(last);
        QVERIFY(!chunk.isEmpty());
        QVERIFY2(incoming.writeChunk(index++, last, chunk, error), qPrintable(error));
        if (last) {
            break;
        }
    } while (true);

    QVERIFY2(incoming.finalise(error), qPrintable(error));
    QCOMPARE(incoming.bytesReceived(), qint64(150000));

    QString hashError;
    const QString sourceHash = fileSha256(sourcePath, &hashError);
    const QString targetHash = fileSha256(targetPath, &hashError);
    QCOMPARE(targetHash, sourceHash);
}

void TransferTest::detectsCorruption()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString sourcePath = writeSourceFile(dir, 40000);
    OutgoingTransfer outgoing(QStringLiteral("t2"), sourcePath, 16 * 1024);
    QString error;
    QVERIFY2(outgoing.open(error), qPrintable(error));

    const QString targetPath = dir.filePath(QStringLiteral("corrupt.bin"));
    IncomingTransfer incoming(QStringLiteral("t2"), QStringLiteral("source.bin"),
                              outgoing.size(), outgoing.sha256(), targetPath);
    QVERIFY2(incoming.open(error), qPrintable(error));

    quint32 index = 0;
    bool last = false;
    while (true) {
        const QByteArray chunk = outgoing.nextChunk(last);
        QVERIFY(!chunk.isEmpty());
        QVERIFY(incoming.writeChunk(index++, last, chunk, error));
        if (last) {
            break;
        }
    }

    // Flip a byte behind the transfer's back: the hash must catch it.
    QFile file(targetPath);
    QVERIFY(file.open(QIODevice::ReadWrite));
    file.seek(100);
    char byte = 0;
    QVERIFY(file.getChar(&byte));
    file.seek(100);
    QVERIFY(file.putChar(static_cast<char>(byte ^ 0xFF)));
    file.close();

    QVERIFY(!incoming.finalise(error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(targetPath));
}

void TransferTest::rejectsOutOfOrderChunks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString targetPath = dir.filePath(QStringLiteral("ordered.bin"));
    IncomingTransfer incoming(QStringLiteral("t3"), QStringLiteral("x.bin"), 10,
                              QString(64, QLatin1Char('a')), targetPath);
    QString error;
    QVERIFY(incoming.open(error));

    QVERIFY(!incoming.writeChunk(1, false, QByteArray(5, 'x'), error));
    QVERIFY(error.contains(QStringLiteral("out of order")));
    QVERIFY(incoming.writeChunk(0, false, QByteArray(5, 'x'), error));
}

void TransferTest::rejectsOversizedChunks()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString targetPath = dir.filePath(QStringLiteral("oversize.bin"));
    IncomingTransfer incoming(QStringLiteral("t4"), QStringLiteral("x.bin"), 10,
                              QString(64, QLatin1Char('a')), targetPath);
    QString error;
    QVERIFY(incoming.open(error));

    QVERIFY(!incoming.writeChunk(0, false, QByteArray(20, 'x'), error));
    QVERIFY(error.contains(QStringLiteral("declared file size")));
}

void TransferTest::rejectsWrongHash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString sourcePath = writeSourceFile(dir, 8000);
    OutgoingTransfer outgoing(QStringLiteral("t5"), sourcePath, 4096);
    QString error;
    QVERIFY2(outgoing.open(error), qPrintable(error));

    const QString targetPath = dir.filePath(QStringLiteral("badhash.bin"));
    IncomingTransfer incoming(QStringLiteral("t5"), QStringLiteral("source.bin"),
                              outgoing.size(), QString(64, QLatin1Char('0')),
                              targetPath);
    QVERIFY(incoming.open(error));

    quint32 index = 0;
    bool last = false;
    while (true) {
        const QByteArray chunk = outgoing.nextChunk(last);
        QVERIFY(!chunk.isEmpty());
        QVERIFY(incoming.writeChunk(index++, last, chunk, error));
        if (last) {
            break;
        }
    }

    QVERIFY(!incoming.finalise(error));
    QVERIFY(error.contains(QStringLiteral("SHA-256 mismatch")));
    QVERIFY(!QFileInfo::exists(targetPath));
}

QTEST_GUILESS_MAIN(TransferTest)

#include "transfer_test.moc"
