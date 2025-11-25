#pragma once

#include <QWidget>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothSocket> // <-- Сырой сокет
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QFile>
#include <QVBoxLayout>
#include <QMessageBox>

class BluetoothWindow : public QWidget
{
    Q_OBJECT

public:
    BluetoothWindow(QWidget* parent = nullptr);
    ~BluetoothWindow();

private slots:
    void startDiscovery();
    void deviceDiscovered(const QBluetoothDeviceInfo& device);
    void deviceScanFinished();

    // Слоты для сокета
    void startFileTransfer();
    void socketConnected();
    void socketDisconnected();
    void socketErrorOccurred(QBluetoothSocket::SocketError error);
    void socketReadyRead(); // Чтение ответов от телефона

private:
    QBluetoothDeviceDiscoveryAgent* m_discoveryAgent = nullptr;
    QBluetoothSocket* m_socket = nullptr;
    QFile* m_file = nullptr;

    // --- UI ---
    QVBoxLayout* m_mainLayout = nullptr;
    QListWidget* m_deviceList = nullptr;
    QPushButton* m_scanButton = nullptr;
    QPushButton* m_sendButton = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;

    QList<QBluetoothDeviceInfo> m_foundDevices;

    // --- Состояние передачи OBEX ---
    enum TransferState {
        Idle,
        ConnectingObex, // Отправка 0x80
        SendingMetadata, // Отправка имени и размера
        SendingBody,     // Отправка данных
        DisconnectingObex // Отправка 0x81
    };
    TransferState m_state = Idle;

    static const int MAX_PACKET_SIZE = 1024; // Ограничим размер пакета для надежности

    void initUI();
    void sendObexConnect();
    void sendObexPut(); // Главная функция отправки кусков
    void sendObexDisconnect();
};