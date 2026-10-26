#include "MainWindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

#include "opentalk/connection.h"
#include "opentalk/transfer.h"

#ifdef OPENTALK_AUDIO_DEVICES
#include "AudioEngine.h"
#endif

using namespace opentalk;

namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes < 1024) {
        return QObject::tr("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QObject::tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QObject::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QObject::tr("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_connection(new Connection(this))
{
    buildUi();
    loadSettings();

    connect(m_connection, &Connection::connected, this, &MainWindow::handleConnected);
    connect(m_connection, &Connection::disconnected, this, &MainWindow::handleDisconnected);
    connect(m_connection, &Connection::socketError, this, &MainWindow::handleSocketError);
    connect(m_connection, &Connection::messageReceived, this, &MainWindow::handleMessage);

    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setInterval(kAudioFrameMs);
    connect(m_playbackTimer, &QTimer::timeout, this, &MainWindow::pumpPlayback);
    m_playbackTimer->start();

    m_sendTimer = new QTimer(this);
    m_sendTimer->setInterval(10);
    connect(m_sendTimer, &QTimer::timeout, this, &MainWindow::pumpOutgoingTransfers);

#ifdef OPENTALK_AUDIO_DEVICES
    m_audio = new AudioEngine(this);
    connect(m_audio, &AudioEngine::frameEncoded, this,
            [this](const QByteArray &payload, quint32 sequence, qint64 timestampMs) {
                if (!m_connection->isConnected() || m_channelId == 0) {
                    return;
                }
                VoicePacket packet;
                packet.channelId = m_channelId;
                packet.sequence = sequence;
                packet.timestampMs = timestampMs;
                packet.payload = payload;
                m_connection->send(messages::voice(packet));
            });
    connect(m_audio, &AudioEngine::inputLevel, this, [this](double level) {
        m_levelBar->setValue(qBound(0, static_cast<int>(level * 320.0), 100));
    });
    connect(m_audio, &AudioEngine::error, this, [this](const QString &message) {
        appendLine(QStringLiteral("<span style=\"color:#b00020;\">Audio: %1</span>")
                       .arg(message.toHtmlEscaped()));
    });

    if (!AudioEngine::isSupported()) {
        m_talkButton->setEnabled(false);
        m_muteCheck->setEnabled(false);
        m_voiceStatus->setText(AudioEngine::unsupportedReason());
    } else {
        m_voiceStatus->setText(tr("Connect to talk."));
    }
#else
    m_talkButton->setEnabled(false);
    m_muteCheck->setEnabled(false);
    m_voiceStatus->setText(tr("Built without audio support."));
#endif

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

    m_hostEdit = new QLineEdit(central);
    m_hostEdit->setPlaceholderText(tr("Server address"));
    m_hostEdit->setMinimumWidth(160);

    m_portSpin = new QSpinBox(central);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(kDefaultPort);

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

    auto *chatBox = new QGroupBox(tr("Channel"), splitter);
    auto *chatLayout = new QVBoxLayout(chatBox);
    m_log = new QTextBrowser(chatBox);
    m_log->setOpenExternalLinks(false);

    auto *composerRow = new QHBoxLayout;
    m_messageEdit = new QLineEdit(chatBox);
    m_messageEdit->setPlaceholderText(tr("Message the channel..."));
    m_sendButton = new QPushButton(tr("Send"), chatBox);
    composerRow->addWidget(m_messageEdit, 1);
    composerRow->addWidget(m_sendButton);

    auto *voiceRow = new QHBoxLayout;
    m_talkButton = new QPushButton(tr("Talk"), chatBox);
    m_talkButton->setCheckable(true);
    m_talkButton->setToolTip(tr("Toggle transmission. Audio is only sent while this is on."));
    m_muteCheck = new QCheckBox(tr("Mute"), chatBox);
    m_levelBar = new QProgressBar(chatBox);
    m_levelBar->setRange(0, 100);
    m_levelBar->setValue(0);
    m_levelBar->setTextVisible(false);
    m_levelBar->setMaximumWidth(120);
    m_sendFileButton = new QPushButton(tr("Send file..."), chatBox);
    m_voiceStatus = new QLabel(chatBox);
    m_voiceStatus->setStyleSheet(QStringLiteral("color: #666;"));

    voiceRow->addWidget(m_talkButton);
    voiceRow->addWidget(m_muteCheck);
    voiceRow->addWidget(new QLabel(tr("Mic"), chatBox));
    voiceRow->addWidget(m_levelBar);
    voiceRow->addWidget(m_sendFileButton);
    voiceRow->addWidget(m_voiceStatus, 1);

    m_transferList = new QListWidget(chatBox);
    m_transferList->setMaximumHeight(90);

    chatLayout->addWidget(m_log, 1);
    chatLayout->addLayout(composerRow);
    chatLayout->addLayout(voiceRow);
    chatLayout->addWidget(new QLabel(tr("File transfers"), chatBox));
    chatLayout->addWidget(m_transferList);

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
    splitter->setSizes({200, 620, 200});

    rootLayout->addWidget(splitter, 1);
    setCentralWidget(central);

    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(m_joinButton, &QPushButton::clicked, this, &MainWindow::joinSelectedChannel);
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendText);
    connect(m_messageEdit, &QLineEdit::returnPressed, this, &MainWindow::sendText);
    connect(m_channelList, &QListWidget::itemDoubleClicked,
            this, &MainWindow::joinSelectedChannel);
    connect(m_talkButton, &QPushButton::toggled, this, &MainWindow::toggleTransmit);
    connect(m_muteCheck, &QCheckBox::toggled, this, &MainWindow::toggleMute);
    connect(m_sendFileButton, &QPushButton::clicked, this, &MainWindow::chooseFileToSend);
}

void MainWindow::loadSettings()
{
    QSettings settings;
    m_hostEdit->setText(settings.value(QStringLiteral("connection/host"),
                                       QStringLiteral("127.0.0.1")).toString());
    m_portSpin->setValue(settings.value(QStringLiteral("connection/port"), kDefaultPort).toInt());
    m_nicknameEdit->setText(settings.value(QStringLiteral("connection/nickname")).toString());
}

void MainWindow::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("connection/host"), m_hostEdit->text().trimmed());
    settings.setValue(QStringLiteral("connection/port"), m_portSpin->value());
    settings.setValue(QStringLiteral("connection/nickname"), m_nicknameEdit->text().trimmed());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    stopAudio();
    m_connection->disconnectFromServer();
    QMainWindow::closeEvent(event);
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

    saveSettings();
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
    m_connection->send(messages::hello(m_nickname));
    statusBar()->showMessage(tr("Connected, completing handshake..."));
}

void MainWindow::handleDisconnected()
{
    m_connectButton->setText(tr("Connect"));
    m_connectButton->setEnabled(true);
    setConnectedUi(false);
    resetSessionState();
    stopAudio();
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

void MainWindow::handleMessage(const Message &message)
{
    switch (message.type) {
    case MessageType::Welcome:
        handleWelcome(message);
        break;
    case MessageType::ChannelList:
        handleChannelList(message);
        break;
    case MessageType::UserList:
        handleUserList(message);
        break;
    case MessageType::UserJoined: {
        const UserInfo user = UserInfo::fromJson(message.body);
        appendLine(QStringLiteral("<i>%1 joined the channel.</i>")
                       .arg(user.nickname.toHtmlEscaped()));
        break;
    }
    case MessageType::UserLeft: {
        const quint32 userId =
            static_cast<quint32>(message.body.value(QLatin1String("userId")).toInteger(0));
        m_receiver.removeSender(userId);
        if (userId != 0) {
            appendLine(QStringLiteral("<i>User %1 left the channel.</i>").arg(userId));
        }
        break;
    }
    case MessageType::TextMessage: {
        const TextEvent event = TextEvent::fromJson(message.body);
        const QString when = event.timestampMs > 0
            ? QDateTime::fromMSecsSinceEpoch(event.timestampMs)
                  .toString(QStringLiteral("hh:mm:ss"))
            : QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
        appendLine(QStringLiteral("<span style=\"color:#777;\">[%1]</span> <b>%2</b>: %3")
                       .arg(when,
                            event.sender.isEmpty()
                                ? tr("user %1").arg(event.senderId)
                                : event.sender.toHtmlEscaped(),
                            event.text.toHtmlEscaped()));
        break;
    }
    case MessageType::VoiceFrame:
        handleVoice(message);
        break;
    case MessageType::FileOffer:
        handleFileOffer(message);
        break;
    case MessageType::FileResponse:
        handleFileResponse(message);
        break;
    case MessageType::FileChunk:
        handleFileChunk(message);
        break;
    case MessageType::FileComplete:
        handleFileComplete(message);
        break;
    case MessageType::Error: {
        const QString code = message.body.value(QLatin1String("code")).toString();
        const QString description =
            message.body.value(QLatin1String("description")).toString();
        appendLine(QStringLiteral("<span style=\"color:#b00020;\">Server error [%1]: %2</span>")
                       .arg(code.toHtmlEscaped(), description.toHtmlEscaped()));
        statusBar()->showMessage(description);
        break;
    }
    case MessageType::Ping:
        m_connection->send(messages::pong(
            static_cast<quint32>(message.body.value(QLatin1String("nonce")).toInteger(0))));
        break;
    case MessageType::Pong:
        break;
    default:
        break;
    }
}

void MainWindow::handleWelcome(const Message &message)
{
    m_selfId = static_cast<quint32>(message.body.value(QLatin1String("userId")).toInteger(0));
    const QString serverName = message.body.value(QLatin1String("serverName")).toString();
    m_nickname = message.body.value(QLatin1String("nickname")).toString(m_nickname);

    setConnectedUi(true);
    startAudio();
    appendLine(QStringLiteral("<i>Connected to %1 as %2.</i>")
                   .arg(serverName.toHtmlEscaped(), m_nickname.toHtmlEscaped()));
    statusBar()->showMessage(tr("Connected to %1").arg(serverName));
}

void MainWindow::handleChannelList(const Message &message)
{
    const QJsonArray channels = message.body.value(QLatin1String("channels")).toArray();
    const quint32 previous = m_channelId;

    m_channelList->clear();
    for (const QJsonValue &value : channels) {
        const ChannelInfo channel = ChannelInfo::fromJson(value.toObject());
        auto *item = new QListWidgetItem(
            tr("%1 (%2)").arg(channel.name, QString::number(channel.userCount)),
            m_channelList);
        item->setData(Qt::UserRole, QVariant::fromValue(channel.id));
        if (channel.id == previous) {
            m_channelList->setCurrentItem(item);
        }
    }
}

void MainWindow::handleUserList(const Message &message)
{
    const QJsonArray users = message.body.value(QLatin1String("users")).toArray();
    m_userList->clear();
    for (const QJsonValue &value : users) {
        const UserInfo user = UserInfo::fromJson(value.toObject());
        QString label = user.nickname;
        if (user.id == m_selfId) {
            label += tr(" (you)");
        }
        m_userList->addItem(label);
    }
}

void MainWindow::handleVoice(const Message &message)
{
    const VoicePacket packet = VoicePacket::fromJson(message.body);
    if (packet.senderId == 0 || packet.senderId == m_selfId || packet.payload.isEmpty()) {
        return;
    }
    m_receiver.pushPacket(packet.senderId, packet.sequence, packet.payload);
}

void MainWindow::pumpPlayback()
{
#ifdef OPENTALK_AUDIO_DEVICES
    if (!m_audioReady || m_audio == nullptr) {
        return;
    }
    if (m_receiver.activeSenders().isEmpty()) {
        return;
    }
    const QByteArray frame = m_receiver.nextFrame();
    if (!frame.isEmpty()) {
        m_audio->queuePcm(frame);
    }
#endif
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
    m_receiver.clear();
    m_connection->send(messages::joinChannel(channelId));
    m_messageEdit->setEnabled(true);
    m_sendButton->setEnabled(true);
    m_log->clear();
    statusBar()->showMessage(tr("Joining %1...").arg(item->text()));
    setConnectedUi(true);
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

    m_connection->send(messages::text(m_channelId, m_selfId, m_nickname, text));
    m_messageEdit->clear();
}

void MainWindow::toggleTransmit(bool transmitting)
{
#ifdef OPENTALK_AUDIO_DEVICES
    if (transmitting && m_channelId == 0) {
        m_talkButton->setChecked(false);
        statusBar()->showMessage(tr("Join a channel before talking."));
        return;
    }
    if (m_audio != nullptr) {
        m_audio->setTransmitting(transmitting);
    }
    m_talkButton->setText(transmitting ? tr("Talking...") : tr("Talk"));
    m_talkButton->setStyleSheet(transmitting
                                    ? QStringLiteral("background:#c62828; color:white;")
                                    : QString());
#else
    Q_UNUSED(transmitting)
#endif
}

void MainWindow::toggleMute(bool muted)
{
#ifdef OPENTALK_AUDIO_DEVICES
    if (m_audio != nullptr) {
        m_audio->setMuted(muted);
    }
#else
    Q_UNUSED(muted)
#endif
}

void MainWindow::chooseFileToSend()
{
    if (!m_connection->isConnected() || m_channelId == 0) {
        statusBar()->showMessage(tr("Join a channel before sending files."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("Send file"));
    if (path.isEmpty()) {
        return;
    }
    beginSendingFile(path);
}

void MainWindow::beginSendingFile(const QString &filePath)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *transfer = new OutgoingTransfer(id, filePath, kDefaultChunkSize, this);

    QString error;
    if (!transfer->open(error)) {
        appendLine(QStringLiteral("<span style=\"color:#b00020;\">Cannot send file: %1</span>")
                       .arg(error.toHtmlEscaped()));
        transfer->deleteLater();
        return;
    }
    if (transfer->size() == 0) {
        appendLine(QStringLiteral("<i>Empty files cannot be sent.</i>"));
        transfer->deleteLater();
        return;
    }

    m_outgoing.insert(id, transfer);
    m_transferNames.insert(id, transfer->fileName());
    updateTransferRow(id, tr("%1 (%2) - waiting for recipient")
                              .arg(transfer->fileName(), formatBytes(transfer->size())));

    FileOffer offer;
    offer.transferId = id;
    offer.channelId = m_channelId;
    offer.senderId = m_selfId;
    offer.sender = m_nickname;
    offer.fileName = transfer->fileName();
    offer.fileSize = transfer->size();
    offer.sha256 = transfer->sha256();
    offer.chunkSize = transfer->chunkSize();

    m_connection->send(messages::fileOffer(offer));
}

void MainWindow::handleFileResponse(const Message &message)
{
    const FileResponse response = FileResponse::fromJson(message.body);
    auto it = m_outgoing.find(response.transferId);
    if (it == m_outgoing.end()) {
        return;
    }

    if (!response.accepted) {
        finishTransfer(response.transferId,
                       tr("%1 - declined (%2)")
                           .arg(m_transferNames.value(response.transferId),
                                response.reason.isEmpty() ? tr("no reason given")
                                                          : response.reason));
        return;
    }

    m_acceptedTransfers.insert(response.transferId);
    updateTransferRow(response.transferId,
                      tr("%1 - sending").arg(m_transferNames.value(response.transferId)));
    if (!m_sendTimer->isActive()) {
        m_sendTimer->start();
    }
}

void MainWindow::pumpOutgoingTransfers()
{
    bool remaining = false;

    for (auto it = m_outgoing.begin(); it != m_outgoing.end(); ++it) {
        const QString id = it.key();
        if (!m_acceptedTransfers.contains(id)) {
            remaining = true;
            continue;
        }

        OutgoingTransfer *transfer = it.value();
        bool finished = false;

        for (int burst = 0; burst < 4 && !finished; ++burst) {
            bool last = false;
            const QByteArray chunk = transfer->nextChunk(last);

            if (chunk.isEmpty() && !last) {
                // Already complete: the final chunk carried `last`.
                finishTransfer(id, tr("%1 - sent").arg(transfer->fileName()));
                finished = true;
                break;
            }

            FileChunk payload;
            payload.transferId = id;
            payload.senderId = m_selfId;
            payload.index = static_cast<quint32>((transfer->bytesSent() - 1) / transfer->chunkSize());
            payload.last = last;
            payload.data = chunk;
            m_connection->send(messages::fileChunk(payload));

            updateTransferRow(id, tr("%1 - sending %2 / %3")
                                      .arg(transfer->fileName(),
                                           formatBytes(transfer->bytesSent()),
                                           formatBytes(transfer->size())));

            if (last) {
                FileComplete complete;
                complete.transferId = id;
                complete.success = true;
                m_connection->send(messages::fileComplete(complete));
                finishTransfer(id, tr("%1 - sent").arg(transfer->fileName()));
                finished = true;
            }
        }

        if (!finished) {
            remaining = true;
        }
    }

    if (!remaining) {
        m_sendTimer->stop();
    }
}

void MainWindow::handleFileOffer(const Message &message)
{
    const FileOffer offer = FileOffer::fromJson(message.body);
    if (offer.senderId == m_selfId || !m_connection->isConnected()) {
        return;
    }

    const auto answer = QMessageBox::question(
        this, tr("Incoming file"),
        tr("%1 wants to send \"%2\" (%3).\n\nSave it to your Downloads folder?")
            .arg(offer.sender.isEmpty() ? tr("A user") : offer.sender,
                 offer.fileName, formatBytes(offer.fileSize)));

    FileResponse response;
    response.transferId = offer.transferId;

    if (answer != QMessageBox::Yes) {
        response.accepted = false;
        response.reason = QStringLiteral("declined");
        m_connection->send(messages::fileResponse(response));
        return;
    }

    const QString targetPath = uniqueDownloadPath(offer.fileName);
    auto *incoming = new IncomingTransfer(offer.transferId, offer.fileName, offer.fileSize,
                                          offer.sha256, targetPath, this);
    QString error;
    if (!incoming->open(error)) {
        incoming->deleteLater();
        response.accepted = false;
        response.reason = error;
        m_connection->send(messages::fileResponse(response));
        appendLine(QStringLiteral("<span style=\"color:#b00020;\">Cannot receive file: %1</span>")
                       .arg(error.toHtmlEscaped()));
        return;
    }

    connect(incoming, &IncomingTransfer::progress, this,
            [this, id = offer.transferId](qint64 received, qint64 total) {
                updateTransferRow(id, tr("%1 - receiving %2 / %3")
                                          .arg(m_transferNames.value(id),
                                               formatBytes(received),
                                               formatBytes(total)));
            });

    m_incoming.insert(offer.transferId, incoming);
    m_transferNames.insert(offer.transferId, offer.fileName);
    updateTransferRow(offer.transferId,
                      tr("%1 - receiving 0 / %2").arg(offer.fileName,
                                                      formatBytes(offer.fileSize)));

    response.accepted = true;
    m_connection->send(messages::fileResponse(response));
}

void MainWindow::handleFileChunk(const Message &message)
{
    const FileChunk chunk = FileChunk::fromJson(message.body);
    auto it = m_incoming.find(chunk.transferId);
    if (it == m_incoming.end()) {
        return;
    }

    QString error;
    if (!it.value()->writeChunk(chunk.index, chunk.last, chunk.data, error)) {
        FileComplete complete;
        complete.transferId = chunk.transferId;
        complete.success = false;
        complete.reason = error;
        m_connection->send(messages::fileComplete(complete));
        finishTransfer(chunk.transferId,
                       tr("%1 - failed: %2").arg(m_transferNames.value(chunk.transferId), error));
        return;
    }

    if (!chunk.last) {
        return;
    }

    QString finalError;
    FileComplete complete;
    complete.transferId = chunk.transferId;
    if (!it.value()->finalise(finalError)) {
        complete.success = false;
        complete.reason = finalError;
        m_connection->send(messages::fileComplete(complete));
        finishTransfer(chunk.transferId,
                       tr("%1 - failed: %2")
                           .arg(m_transferNames.value(chunk.transferId), finalError));
        return;
    }

    complete.success = true;
    m_connection->send(messages::fileComplete(complete));
    finishTransfer(chunk.transferId,
                   tr("%1 - saved to %2")
                       .arg(m_transferNames.value(chunk.transferId),
                            QDir::toNativeSeparators(it.value()->targetPath())));
}

void MainWindow::handleFileComplete(const Message &message)
{
    const FileComplete complete = FileComplete::fromJson(message.body);
    if (!complete.success) {
        appendLine(QStringLiteral("<span style=\"color:#b00020;\">Transfer failed: %1</span>")
                       .arg(complete.reason.toHtmlEscaped()));
    }
    finishTransfer(complete.transferId,
                   complete.success
                       ? tr("%1 - complete").arg(m_transferNames.value(complete.transferId))
                       : tr("%1 - failed").arg(m_transferNames.value(complete.transferId)));
}

void MainWindow::finishTransfer(const QString &transferId, const QString &text)
{
    if (auto *outgoing = m_outgoing.take(transferId)) {
        outgoing->deleteLater();
    }
    if (auto *incoming = m_incoming.take(transferId)) {
        incoming->deleteLater();
    }
    m_acceptedTransfers.remove(transferId);
    updateTransferRow(transferId, text);
    appendLine(QStringLiteral("<i>%1</i>").arg(text.toHtmlEscaped()));
}

void MainWindow::updateTransferRow(const QString &transferId, const QString &text)
{
    if (auto *row = m_transferRows.value(transferId, nullptr)) {
        row->setText(text);
        return;
    }
    auto *row = new QListWidgetItem(text, m_transferList);
    m_transferRows.insert(transferId, row);
}

QString MainWindow::uniqueDownloadPath(const QString &fileName) const
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    QDir().mkpath(directory);

    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();

    QString candidate = QDir(directory).filePath(fileName);
    int counter = 1;
    while (QFileInfo::exists(candidate)) {
        candidate = QDir(directory).filePath(
            QStringLiteral("%1 (%2)%3").arg(base).arg(counter++).arg(suffix));
    }
    return candidate;
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
    m_sendFileButton->setEnabled(connected && m_channelId != 0);
    m_connectButton->setText(connected ? tr("Disconnect") : tr("Connect"));
    m_connectButton->setEnabled(true);

#ifdef OPENTALK_AUDIO_DEVICES
    const bool canTalk = connected && m_channelId != 0 && m_audioReady;
    m_talkButton->setEnabled(canTalk);
    m_muteCheck->setEnabled(m_audioReady);
    if (!canTalk && m_talkButton->isChecked()) {
        m_talkButton->setChecked(false);
    }
#else
    m_talkButton->setEnabled(false);
    m_muteCheck->setEnabled(false);
#endif

    if (!connected) {
        m_channelList->clear();
        m_userList->clear();
        m_levelBar->setValue(0);
    }
}

void MainWindow::resetSessionState()
{
    m_selfId = 0;
    m_channelId = 0;
    m_userList->clear();
    m_messageEdit->clear();
    m_receiver.clear();
    m_levelBar->setValue(0);
    m_transferRows.clear();
    m_transferList->clear();
    m_transferNames.clear();
    m_acceptedTransfers.clear();

    for (auto *transfer : m_outgoing) {
        transfer->deleteLater();
    }
    m_outgoing.clear();
    for (auto *transfer : m_incoming) {
        transfer->abort();
        transfer->deleteLater();
    }
    m_incoming.clear();
    if (m_sendTimer != nullptr) {
        m_sendTimer->stop();
    }
}

void MainWindow::startAudio()
{
#ifdef OPENTALK_AUDIO_DEVICES
    if (m_audio == nullptr || !AudioEngine::isSupported()) {
        return;
    }
    QString error;
    if (m_audio->start(error)) {
        m_audioReady = true;
        m_voiceStatus->setText(tr("Ready to talk."));
    } else {
        m_audioReady = false;
        m_voiceStatus->setText(tr("Audio unavailable."));
        appendLine(QStringLiteral("<span style=\"color:#b00020;\">Audio: %1</span>")
                       .arg(error.toHtmlEscaped()));
    }
    setConnectedUi(true);
#endif
}

void MainWindow::stopAudio()
{
#ifdef OPENTALK_AUDIO_DEVICES
    if (m_audio != nullptr) {
        m_audio->stop();
    }
#endif
    m_audioReady = false;
    if (m_talkButton->isChecked()) {
        m_talkButton->setChecked(false);
    }
}

void MainWindow::appendLine(const QString &html)
{
    m_log->append(html);
}
