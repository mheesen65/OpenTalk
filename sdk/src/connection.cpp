#include "opentalk/connection.h"

#include <QTcpSocket>
#include <QtEndian>

namespace opentalk {

Connection::Connection(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &Connection::connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &Connection::disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &Connection::handleReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &Connection::handleSocketError);
}

Connection::~Connection() = default;

void Connection::connectToServer(const QString &host, quint16 port)
{
    m_buffer.clear();
    m_socket->abort();
    m_socket->connectToHost(host, port);
}

void Connection::disconnectFromServer()
{
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        return;
    }
    m_socket->disconnectFromHost();
}

void Connection::abort()
{
    m_buffer.clear();
    m_socket->abort();
}

bool Connection::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QAbstractSocket::SocketState Connection::state() const
{
    return m_socket->state();
}

QString Connection::errorString() const
{
    return m_socket->errorString();
}

bool Connection::send(const Message &message)
{
    if (!isConnected()) {
        return false;
    }
    const QByteArray frame = encodeFrame(message);
    return m_socket->write(frame) == frame.size();
}

void Connection::handleReadyRead()
{
    m_buffer.append(m_socket->readAll());
    processBuffer();
}

void Connection::handleSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    emit socketError(m_socket->errorString());
}

void Connection::processBuffer()
{
    while (m_buffer.size() >= 4) {
        const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());

        // A frame either fits the protocol limit or the stream is unusable:
        // there is no way to resynchronise mid-frame, so drop the connection.
        if (length == 0 || length > kMaxFrameSize) {
            emit socketError(tr("Received an invalid frame length (%1 bytes).").arg(length));
            m_buffer.clear();
            m_socket->abort();
            return;
        }

        const int totalSize = static_cast<int>(length) + 4;
        if (m_buffer.size() < totalSize) {
            return; // wait for the rest of the frame
        }

        const QByteArray payload = m_buffer.mid(4, static_cast<int>(length));
        m_buffer.remove(0, totalSize);

        Message message;
        if (!decodeFrame(payload, message)) {
            emit socketError(tr("Received a malformed frame."));
            continue;
        }
        emit messageReceived(message);
    }
}

} // namespace opentalk
