#pragma once

#include <QWidget>

// --- Подключаем все необходимые заголовки WinAPI ---
#include <windows.h>
#include <dbt.h>        // Для сообщений WM_DEVICECHANGE
#include <Setupapi.h>   // Для информации об устройствах
#include <Cfgmgr32.h>   // Для функции извлечения
#include <initguid.h>   // Для определения GUID_DEVINTERFACE_USB_DEVICE
#include <usbiodef.h>

// Forward-декларации для UI классов Qt
class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QPushButton;

// Структура для хранения информации о USB-устройстве
struct UsbDevice {
    QString name;
    QString type;
    QString instanceId; // Уникальный ID экземпляра устройства, нужен для извлечения
    bool isEjectable = false;
};

class UsbMonitorWindow : public QWidget
{
    Q_OBJECT

public:
    UsbMonitorWindow(QWidget* parent = nullptr);
    ~UsbMonitorWindow();

protected:
    // Переопределяем метод для перехвата системных событий Windows
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    void populateDeviceList();
    void ejectSelectedDevice();

private:
    // --- UI Элементы ---
    QTreeWidget* m_deviceTree = nullptr;
    QTextEdit* m_consoleLog = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_ejectButton = nullptr;

    // --- WinAPI ---
    HDEVNOTIFY m_hDeviceNotify = nullptr; // Дескриптор для регистрации уведомлений

    // --- Приватные методы ---
    void initUI();
    void registerDeviceNotifications();
    void logMessage(const QString& message);

    // Вспомогательная функция для получения информации о конкретном устройстве
    UsbDevice getDeviceInfo(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfoData);
};