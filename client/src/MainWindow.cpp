#include "MainWindow.h"

#include <QDateTime>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonValue>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

#include "opentalk/connection.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_connection(new opentalk::Connection(this))
{
    buildUi();

    connect(m_connection, &opentalk::Connection::connected,
            this, &MainWindow::handleConnected);
    connect(m_connection, &opentalk::Connection::disconnected,
            this, &MainWindow::handleDisconnected);
    connect(m_connection, &opentalk::Connection::socketError,
            this, &MainWindow::handleSocketError);
    connect(m_connection, &opentalk::Connection::messageReceived,
            this, &MainWindow::handleMessage);

    setConnectedUi(false);
    statusBar()->showMessage(tr("Not connected"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);

    // --- Connection bar -----------------------------------------------------
    auto *connectionBar = new QHBoxLayout;

    m_hostEdit = new QLineEdit(QStringLiteral("127.0.0.1"), central);
    m_hostEdit->setPlaceholderText(tr("Server address"));
    m_hostEdit->setMinimumWidth(160);

    m_portSpin = new QSpinBox(central);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(opentalk::kDefaultPort);

    m_nicknameEdit = new QLineEdit(central);
    m_nicknameEdit->setPlaceholderText(tr("Your nickname"));
    m_nicknameEdit->setMaxLength(32);
    m_nicknameEdit->setMinimumWidth(140);

    m_connectButton = new QPushButton(tr("Connect"), central);

    connectionBar->addWidget(new QLabel(tr("Server"), central));
    connectionBar->addWidget(m_hostEdit);
    connectionBar->addWidget(new QLabel(tr("Port"), central));
    connectionBar->addWidget(m_portSpin);
    connectionBar->addSpacing(12);
    connectionBar->addWidget(new QLabel(tr("Nickname"), central));
    connectionBar->addWidget(m_nicknameEdit);
    connectionBar->addWidget(m_connectButton);
    rootLayout->addLayout(connectionBar);

    // --- Body: channels | chat | users -------------------------------------
    auto *splitter = new QSplitter(Qt::Horizontal, central);

    auto *channelBox = new QGroupBox(tr("Channels"), splitter);
    auto *channelLayout = new QVBoxLayout(channelBox);
    m_channelList = new QListWidget(channelBox);
    m_joinButton = new QPushButton(tr("Join selected channel"), channelBox);
    channelLayout->addWidget(m_channelList);
    channelLayout->addWidget(m_joinButton);

    auto *chatBox = new QGroupBox(tr("Channel chat"), splitter);
    auto *chatLayout = new QVBoxLayout(chatBox);
    m_log = new QTextBrowser(chatBox);
    m_log->setOpenExternalLinks(false);

    auto *composerRow = new QHBoxLayout;
    m_messageEdit = new QLineEdit(chatBox);
    m_messageEdit->setPlaceholderText(tr("Message the channel..."));
    m_sendButton = new QPushButton(tr("Send"), chatBox);
    m_voiceButton = new QPushButton(tr("Voice (not available yet)"), chatBox);
    m_voiceButton->setEnabled(false);
    m_voiceButton->setToolTip(
        tr("Audio capture and playback are not implemented yet.\n"
           "This build relays text and file metadata only."));
    composerRow->addWidget(m_messageEdit, 1);
    composerRow->addWidget(m_sendButton);
    composerRow->addWidget(m_voiceButton);

    chatLayout->addWidget(m_log);
    chatLayout->addLayout(composerRow);

    auto *userBox = new QGroupBox(tr("In this channel"), splitter);
    auto *userLayout = new QVBoxLayout(userBox);
    m_userList = new QListWidget(userBox);
    userLayout->addWidget(m_userList);

    splitter->addWidget(channelBox);
    splitter->addWidget(chatBox);
    splitter->addWidget(userBox);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({200, 560, 200});

    rootLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(m_joinButton, &QPushButton::clicked, this, &MainWindow::joinSelectedChannel);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendText);
    connect(m_messageEdit, &QLineEdit::returnPressed, this, &MainWindow::sendText);
    connect(m_channelList, &QListWidget::itemDoubleClicked,
            this, &MainWindow::joinSelectedChannel);
}

void MainWindow::toggleConnection()
{
    if (m_connection->isConnected()
        || m_connection->state() == QAbstractSocket::ConnectingState) {
        m_connection->disconnectFromServer();
        return;
    }

    const QString nickname = m_nicknameEdit->text().trimmed();
    if (nickname.isEmpty()) {
        statusBar()->showMessage(tr("Choose a nickname before connecting."));
        m_nicknameEdit->setFocus();
        return;
    }
    const QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty()) {
        statusBar()->showMessage(tr("Enter a server address."));
        m_hostEdit->setFocus();
        return;
    }

    m_nickname = nickname;
    resetSessionState();
    statusBar()->showMessage(tr("Connecting to %1:%2...").arg(host).arg(m_portSpin->value()));
    m_connectButton->setEnabled(false);
    m_connection->connectToServer(host, m_portSpin->value());
}

void MainWindow::handleConnected()
{
    m_connectButton->setText(tr("Disconnect"));
    m_connectButton->setEnabled(true);
    m_connection->send(opentalk::messages::hello(m_nickname));
    statusBar()->showMessage(tr("Connected, completing handshake..."));
}

void MainWindow::handleDisconnected()
{
    m_connectButton->setText(tr("Connect"));
    m_connectButton->setEnabled(true);
    setConnectedUi(false);
    resetSessionState();
    appendLine(QStringLiteral("<i>Disconnected.</i>"));
    statusBar()->showMessage(tr("Not connected"));
}

void MainWindow::handleSocketError(const QString &message)
{
    m_connectButton->setEnabled(true);
    if (!m_connection->isConnected()) {
        m_connectButton->setText(tr("Connect"));
    }
    appendLine(QStringLiteral("<span style=\"color:#b00020;\">Network error: %1</span>")
                   .arg(message.toHtmlEscaped()));
    statusBar()->showMessage(message);
}

void MainWindow::handleMessage(const opentalk::Message &message)
{
    switch (message.type) {
    case opentalk::MessageType::Welcome:
        handleWelcome(message);
        break;
    case opentalk::MessageType::ChannelList:
        handleChannelList(message);
        break;
    case opentalk::MessageType::UserList:
        handleUserList(message);
        break;
    case opentalk::MessageType::UserJoined: {
        const opentalk::UserInfo user = opentalk::UserInfo::fromJson(message.body);
        appendLine(QStringLiteral("<i>%1 joined the channel.</i>")
                       .arg(user.nickname.toHtmlEscaped()));
        break;
    }
    case opentalk::MessageType::UserLeft: {
        const quint32 userId =
            static_cast<quint32>(message.body.value(QLatin1String("userId")).toInteger(0));
        if (userId != 0) {
            appendLine(QStringLiteral("<i>User %1 left the channel.</i>").arg(userId));
        }
        break;
    }
    case opentalk::MessageType::TextMessage: {
        const opentalk::TextEvent event = opentalk::TextEvent::fromJson(message.body);
        const QString when = event.timestampMs > 0
            ? QDateTime::fromMSecsSinceEpoch(event.timestampMs).toString(QStringLiteral("hh:mm:ss"))
            : QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
        appendLine(QStringLiteral("<span style=\"color:#777;\">[%1]</span> <b>%2</b>: %3")
                       .arg(when,
                            event.sender.isEmpty()
                                ? tr("user %1").arg(event.senderId)
                                : event.sender.toHtmlEscaped(),
                            event.text.toHtmlEscaped()));
        break;
    }
    case opentalk::MessageType::Error: {
        const QString code = message.body.value(QLatin1String("code")).toString();
        const QString description =
            message.body.value(QLatin1String("description")).toString();
        appendLine(QStringLiteral("<span style=\"color:#b00020;\">Server error [%1]: %2</span>")
                       .arg(code.toHtmlEscaped(), description.toHtmlEscaped()));
        statusBar()->showMessage(description);
        break;
    }
    case opentalk::MessageType::Ping:
        m_connection->send(opentalk::messages::pong(
            static_cast<quint32>(message.body.value(QLatin1String("nonce")).toInteger(0))));
        break;
    case opentalk::MessageType::Pong:
        break;
    default:
        break;
    }
}

void MainWindow::handleWelcome(const opentalk::Message &message)
{
    m_selfId = static_cast<quint32>(
        message.body.value(QLatin1String("userId")).toInteger(0));
    const QString serverName =
        message.body.value(QLatin1String("serverName")).toString();
    m_nickname = message.body.value(QLatin1String("nickname")).toString(m_nickname);

    setConnectedUi(true);
    appendLine(QStringLiteral("<i>Connected to %1 as %2.</i>")
                   .arg(serverName.toHtmlEscaped(), m_nickname.toHtmlEscaped()));
    statusBar()->showMessage(tr("Connected to %1").arg(serverName));
}

void MainWindow::handleChannelList(const opentalk::Message &message)
{
    const QJsonArray channels = message.body.value(QLatin1String("channels")).toArray();
    const quint32 previous = m_channelId;

    m_channelList->clear();
    for (const QJsonValue &value : channels) {
        const opentalk::ChannelInfo channel =
            opentalk::ChannelInfo::fromJson(value.toObject());
        auto *item = new QListWidgetItem(
            tr("%1 (%2)").arg(channel.name, QString::number(channel.userCount)),
            m_channelList);
        item->setData(Qt::UserRole, QVariant::fromValue(channel.id));
        if (channel.id == previous) {
            m_channelList->setCurrentItem(item);
        }
    }
}

void MainWindow::handleUserList(const opentalk::Message &message)
{
    const QJsonArray users = message.body.value(QLatin1String("users")).toArray();
    m_userList->clear();
    for (const QJsonValue &value : users) {
        const opentalk::UserInfo user = opentalk::UserInfo::fromJson(value.toObject());
        QString label = user.nickname;
        if (user.id == m_selfId) {
            label += tr(" (you)");
        }
        m_userList->addItem(label);
    }
}

void MainWindow::joinSelectedChannel()
{
    QListWidgetItem *item = m_channelList->currentItem();
    if (item == nullptr) {
        statusBar()->showMessage(tr("Select a channel first."));
        return;
    }
    const quint32 channelId = item->data(Qt::UserRole).toUInt();
    if (channelId == 0) {
        return;
    }

    m_channelId = channelId;
    m_connection->send(opentalk::messages::joinChannel(channelId));
    m_messageEdit->setEnabled(true);
    m_sendButton->setEnabled(true);
    m_log->clear();
    statusBar()->showMessage(tr("Joining %1...").arg(item->text()));
}

void MainWindow::sendText()
{
    const QString text = m_messageEdit->text().trimmed();
    if (text.isEmpty() || !m_connection->isConnected()) {
        return;
    }
    if (m_channelId == 0) {
        statusBar()->showMessage(tr("Join a channel before sending messages."));
        return;
    }

    // The server stamps the real sender, so the client-side name here is only
    // for the local echo path.
    m_connection->send(opentalk::messages::text(m_channelId, m_selfId, m_nickname, text));
    m_messageEdit->clear();
}

void MainWindow::setConnectedUi(bool connected)
{
    m_hostEdit->setEnabled(!connected);
    m_portSpin->setEnabled(!connected);
    m_nicknameEdit->setEnabled(!connected);
    m_channelList->setEnabled(connected);
    m_joinButton->setEnabled(connected);
    m_messageEdit->setEnabled(connected && m_channelId != 0);
    m_sendButton->setEnabled(connected && m_channelId != 0);
    m_connectButton->setText(connected ? tr("Disconnect") : tr("Connect"));
    m_connectButton->setEnabled(true);

    if (!connected) {
        m_channelList->clear();
        m_userList->clear();
    }
}

void MainWindow::resetSessionState()
{
    m_selfId = 0;
    m_channelId = 0;
    m_userList->clear();
    m_messageEdit->clear();
}

void MainWindow::appendLine(const QString &html)
{
    m_log->append(html);
}
