#pragma once

#include <QWidget>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QProcess> // <-- Добавлено для запуска системной утилиты

class BluetoothWindow : public QWidget
{
    Q_OBJECT

public:
    BluetoothWindow(QWidget* parent = nullptr);
    ~BluetoothWindow();

private slots:
    void startDiscovery();
    void deviceDiscovered(const QBluetoothDeviceInfo& device);
    void discoveryFinished();
    void sendFileToSelectedDevice(); // Измененная логика

private:
    QBluetoothDeviceDiscoveryAgent* m_discoveryAgent = nullptr;

    // --- UI Элементы ---
    QVBoxLayout* m_mainLayout = nullptr;
    QListWidget* m_deviceList = nullptr;
    QPushButton* m_scanButton = nullptr;
    QPushButton* m_sendButton = nullptr;
    QLabel* m_statusLabel = nullptr;

    QList<QBluetoothDeviceInfo> m_foundDevices;

    void initUI();
};