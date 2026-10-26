#pragma once

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QString>

#include "opentalk/audio.h"
#include "opentalk/protocol.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTextBrowser;
class QTimer;

#ifdef OPENTALK_AUDIO_DEVICES
class AudioEngine;
#endif

namespace opentalk {
class Connection;
class IncomingTransfer;
class OutgoingTransfer;
} // namespace opentalk

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void toggleConnection();
    void handleConnected();
    void handleDisconnected();
    void handleSocketError(const QString &message);
    void handleMessage(const opentalk::Message &message);
    void joinSelectedChannel();
    void sendText();

    void chooseFileToSend();
    void pumpOutgoingTransfers();
    void pumpPlayback();
    void toggleTransmit(bool transmitting);
    void toggleMute(bool muted);

private:
    void buildUi();
    void loadSettings();
    void saveSettings();

    void appendLine(const QString &html);
    void setConnectedUi(bool connected);
    void resetSessionState();

    void handleWelcome(const opentalk::Message &message);
    void handleChannelList(const opentalk::Message &message);
    void handleUserList(const opentalk::Message &message);
    void handleVoice(const opentalk::Message &message);
    void handleFileOffer(const opentalk::Message &message);
    void handleFileResponse(const opentalk::Message &message);
    void handleFileChunk(const opentalk::Message &message);
    void handleFileComplete(const opentalk::Message &message);

    void beginSendingFile(const QString &filePath);
    void finishTransfer(const QString &transferId, const QString &message);
    void updateTransferRow(const QString &transferId, const QString &text);
    QString uniqueDownloadPath(const QString &fileName) const;
    void startAudio();
    void stopAudio();

    opentalk::Connection *m_connection{nullptr};
    opentalk::VoiceReceiver m_receiver;

#ifdef OPENTALK_AUDIO_DEVICES
    AudioEngine *m_audio{nullptr};
#endif

    QTimer *m_playbackTimer{nullptr};
    QTimer *m_sendTimer{nullptr};

    QLineEdit *m_hostEdit{nullptr};
    QSpinBox *m_portSpin{nullptr};
    QLineEdit *m_nicknameEdit{nullptr};
    QPushButton *m_connectButton{nullptr};
    QPushButton *m_joinButton{nullptr};
    QPushButton *m_sendButton{nullptr};
    QPushButton *m_talkButton{nullptr};
    QPushButton *m_sendFileButton{nullptr};
    QCheckBox *m_muteCheck{nullptr};
    QProgressBar *m_levelBar{nullptr};
    QListWidget *m_channelList{nullptr};
    QListWidget *m_userList{nullptr};
    QListWidget *m_transferList{nullptr};
    QTextBrowser *m_log{nullptr};
    QLineEdit *m_messageEdit{nullptr};
    QLabel *m_voiceStatus{nullptr};

    QHash<QString, opentalk::OutgoingTransfer *> m_outgoing;
    QHash<QString, opentalk::IncomingTransfer *> m_incoming;
    QHash<QString, QListWidgetItem *> m_transferRows;
    QSet<QString> m_acceptedTransfers;
    QHash<QString, QString> m_transferNames;

    quint32 m_selfId{0};
    quint32 m_channelId{0};
    QString m_nickname;
    bool m_audioReady{false};
};
