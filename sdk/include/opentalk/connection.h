#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QString>

#include "opentalk/protocol.h"

class QTcpSocket;

namespace opentalk {

/// A framed TCP connection to an OpenTalk server.
///
/// Owns the socket and the read buffer, and re-emits complete messages. The
/// client and the server's per-connection session both build on it, so framing
/// bugs are fixed once.
class Connection : public QObject
{
    Q_OBJECT

public:
    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;

    /// Starts a non-blocking connect. Watch `connected` and `socketError`.
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    void abort();

    bool isConnected() const;
    QAbstractSocket::SocketState state() const;
    QString errorString() const;

    /// Queues a frame. Returns false if the socket is not connected.
    bool send(const Message &message);

signals:
    void connected();
    void disconnected();
    void socketError(const QString &message);
    void messageReceived(const opentalk::Message &message);

private slots:
    void handleReadyRead();
    void handleSocketError(QAbstractSocket::SocketError error);

private:
    void processBuffer();

    QTcpSocket *m_socket{nullptr};
    QByteArray m_buffer;
};

} // namespace opentalk
