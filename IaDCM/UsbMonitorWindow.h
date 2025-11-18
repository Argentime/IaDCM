#pragma once

#include <QWidget>
#include <QMap> // <-- Для нашей карты дисков

// --- Подключаем все необходимые заголовки WinAPI ---
#include <windows.h>
#include <dbt.h>
#include <Setupapi.h>
#include <Cfgmgr32.h>
#include <initguid.h>
#include <usbiodef.h>
#include <winioctl.h>
#include <fileapi.h>

// Forward-декларации
class QTreeWidget;
class QTreeWidgetItem;
class QTextEdit;
class QPushButton;

// Универсальная структура для любого USB-устройства
struct UsbDevice {
    QString name;
    QString type;
    QString instanceId;
    bool isEjectable = false;
    QString driveLetter; // Будет заполнено только для накопителей
};

class UsbMonitorWindow : public QWidget
{
    Q_OBJECT

public:
    UsbMonitorWindow(QWidget* parent = nullptr);
    ~UsbMonitorWindow();

protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    void populateDeviceList();
    void ejectSelectedDevice();

private:
    QTreeWidget* m_deviceTree = nullptr;
    QTextEdit* m_consoleLog = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_ejectButton = nullptr;

    HDEVNOTIFY m_hDeviceNotify = nullptr;
    // Карта для связи InstanceID родительского USB-узла с буквой диска
    QMap<QString, QString> m_driveMap;
    // Для отслеживания безопасного извлечения
    QString m_pendingEjectDeviceName;

    void initUI();
    void registerDeviceNotifications();
    void logMessage(const QString& message);

    // Новые и переработанные вспомогательные функции
    UsbDevice getUsbDeviceInfo(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfoData);
    QString getDriveLetterFromUsbInstanceId(const QString& instanceId);
    void buildDriveMap();
};