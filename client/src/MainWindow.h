#pragma once

#include <QMainWindow>
#include <QString>

#include "opentalk/protocol.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTextBrowser;

namespace opentalk {
class Connection;
}

/// Chat and channel shell. Audio capture is deliberately absent: the voice
/// button is inert until the audio engine lands, so the UI never pretends to
/// transmit anything.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void toggleConnection();
    void handleConnected();
    void handleDisconnected();
    void handleSocketError(const QString &message);
    void handleMessage(const opentalk::Message &message);
    void joinSelectedChannel();
    void sendText();

private:
    void buildUi();
    void appendLine(const QString &html);
    void setConnectedUi(bool connected);
    void resetSessionState();
    void handleWelcome(const opentalk::Message &message);
    void handleChannelList(const opentalk::Message &message);
    void handleUserList(const opentalk::Message &message);

    opentalk::Connection *m_connection{nullptr};

    QLineEdit *m_hostEdit{nullptr};
    QSpinBox *m_portSpin{nullptr};
    QLineEdit *m_nicknameEdit{nullptr};
    QPushButton *m_connectButton{nullptr};
    QPushButton *m_joinButton{nullptr};
    QPushButton *m_sendButton{nullptr};
    QPushButton *m_voiceButton{nullptr};
    QListWidget *m_channelList{nullptr};
    QListWidget *m_userList{nullptr};
    QTextBrowser *m_log{nullptr};
    QLineEdit *m_messageEdit{nullptr};

    quint32 m_selfId{0};
    quint32 m_channelId{0};
    QString m_nickname;
};
