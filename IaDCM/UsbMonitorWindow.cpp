#include "UsbMonitorWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <qtreewidget.h>
#include <QApplication>
#include <QTextEdit>
#include <QPushButton>
#include <QTime>

#pragma comment (lib, "Setupapi.lib")
#pragma comment (lib, "Cfgmgr32.lib")

UsbMonitorWindow::UsbMonitorWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Мониторинг USB-портов");
    setMinimumSize(800, 600);
    initUI();
    registerDeviceNotifications();
    populateDeviceList();
}

UsbMonitorWindow::~UsbMonitorWindow()
{
    if (m_hDeviceNotify) {
        UnregisterDeviceNotification(m_hDeviceNotify);
    }
}

void UsbMonitorWindow::initUI()
{
    m_deviceTree = new QTreeWidget(this);
    m_deviceTree->setColumnCount(4);
    m_deviceTree->setHeaderLabels({ "Устройство", "Класс", "Диск", "Извлекаемое" });
    m_deviceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_consoleLog = new QTextEdit(this);
    m_consoleLog->setReadOnly(true);
    m_consoleLog->setFixedHeight(150);

    m_refreshButton = new QPushButton("Обновить список", this);
    m_ejectButton = new QPushButton("Безопасно извлечь", this);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* buttonsLayout = new QHBoxLayout();
    buttonsLayout->addWidget(m_refreshButton);
    buttonsLayout->addWidget(m_ejectButton);
    mainLayout->addWidget(m_deviceTree);
    mainLayout->addLayout(buttonsLayout);
    mainLayout->addWidget(m_consoleLog);

    connect(m_refreshButton, &QPushButton::clicked, this, &UsbMonitorWindow::populateDeviceList);
    connect(m_ejectButton, &QPushButton::clicked, this, &UsbMonitorWindow::ejectSelectedDevice);
}

void UsbMonitorWindow::logMessage(const QString& message)
{
    m_consoleLog->append(QTime::currentTime().toString("[hh:mm:ss] ") + message);
}

void UsbMonitorWindow::registerDeviceNotifications()
{
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE; // Слушаем ВСЕ USB-события

    m_hDeviceNotify = RegisterDeviceNotification((HANDLE)this->winId(), &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!m_hDeviceNotify) {
        logMessage("[ОШИБКА] Не удалось зарегистрировать получение уведомлений.");
    }
}

bool UsbMonitorWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_DEVICECHANGE) {
            PDEV_BROADCAST_HDR hdr = (PDEV_BROADCAST_HDR)msg->lParam;
            if (!hdr) {
                return QWidget::nativeEvent(eventType, message, result);
            }

            if (hdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) {
                return QWidget::nativeEvent(eventType, message, result);
            }

            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)hdr;
            QString deviceName = QString::fromWCharArray(pDevInf->dbcc_name).toLower();

            switch (msg->wParam)
            {
            case DBT_DEVICEARRIVAL:
                logMessage(QString("[+] Подключено новое устройство. Обновляю список..."));
                Sleep(5);
                populateDeviceList(); 

                break;

            case DBT_DEVICEREMOVECOMPLETE:
                if (!m_pendingEjectDeviceName.isEmpty() && deviceName.contains(m_pendingEjectDeviceName)) {
                    logMessage(QString("[OK] Устройство '%1' было БЕЗОПАСНО извлечено.").arg(m_pendingEjectDeviceName.toUpper()));
                    m_pendingEjectDeviceName.clear();
                }
                else {
                    logMessage(QString("[!!!] НЕБЕЗОПАСНОЕ ИЗВЛЕЧЕНИЕ: Устройство было отключено внезапно."));
                }
                populateDeviceList(); 
                break;

            case DBT_DEVICEQUERYREMOVE:
                break;

            case DBT_DEVICEQUERYREMOVEFAILED:
                logMessage(QString("[ОТКАЗ] Безопасное извлечение устройства '%1' было отклонено системой.").arg(m_pendingEjectDeviceName.toUpper()));
                m_pendingEjectDeviceName.clear();
                break;
            }
            *result = TRUE;
            return true;
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void UsbMonitorWindow::populateDeviceList()
{
    m_deviceTree->clear();
    logMessage("Сканирование подключенных USB-устройств...");
    buildDriveMap(); 

    HDEVINFO hDevInfo = SetupDiGetClassDevs(NULL, L"USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) return;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i)
    {
        UsbDevice deviceInfo = getUsbDeviceInfo(hDevInfo, devInfoData);
        if (deviceInfo.name.isEmpty()) continue;

        QTreeWidgetItem* item = new QTreeWidgetItem(m_deviceTree);
        item->setText(0, deviceInfo.name);
        item->setText(1, deviceInfo.type);
        item->setText(2, m_driveMap.value(deviceInfo.instanceId, "")); 
        item->setText(3, deviceInfo.isEjectable ? "Да" : "Нет");
        item->setData(0, Qt::UserRole, deviceInfo.instanceId);
        item->setData(1, Qt::UserRole, deviceInfo.isEjectable);
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
}

UsbDevice UsbMonitorWindow::getUsbDeviceInfo(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfoData)
{
    UsbDevice device;
    TCHAR buffer[512];

    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
        device.name = QString::fromWCharArray(buffer);
    }
    else if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
        device.name = QString::fromWCharArray(buffer);
    }

    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_CLASS, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
        device.type = QString::fromWCharArray(buffer);
    }

    if (CM_Get_Device_ID(devInfoData.DevInst, buffer, MAX_PATH, 0) == CR_SUCCESS) {
        device.instanceId = QString::fromWCharArray(buffer);
    }

    DWORD capabilities = 0;
    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_CAPABILITIES, NULL, (PBYTE)&capabilities, sizeof(capabilities), NULL))
    {
        bool isRemovable = (capabilities & CM_DEVCAP_REMOVABLE);
        bool isSurpriseRemovable = (capabilities & CM_DEVCAP_SURPRISEREMOVALOK);

        if (isRemovable && !isSurpriseRemovable) {
            device.isEjectable = true;
        }

        if (m_driveMap.contains(device.instanceId)) {
            device.isEjectable = true;
        }
    }
    return device;
}

void UsbMonitorWindow::buildDriveMap()
{
    m_driveMap.clear();
    WCHAR szDrives[256];
    if (!GetLogicalDriveStrings(256, szDrives)) return;

    WCHAR* pDrive = szDrives;
    while (*pDrive) {
        QString driveLetter = QString::fromWCharArray(pDrive);
        if (GetDriveType(pDrive) == DRIVE_REMOVABLE) {
            QString volumePath = "\\\\.\\" + driveLetter.left(2);
            HANDLE hVolume = CreateFile(volumePath.toStdWString().c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (hVolume == INVALID_HANDLE_VALUE) continue;

            STORAGE_DEVICE_NUMBER sdn;
            DWORD bytesReturned;
            if (!DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &bytesReturned, NULL)) {
                CloseHandle(hVolume); continue;
            }
            CloseHandle(hVolume);

            DWORD diskNumber = sdn.DeviceNumber;
            HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (hDevInfo == INVALID_HANDLE_VALUE) continue;

            SP_DEVICE_INTERFACE_DATA devInterfaceData;
            devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
            for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_DISK, i, &devInterfaceData); ++i) {
                DWORD requiredSize = 0;
                SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, NULL, 0, &requiredSize, NULL);
                PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) new BYTE[requiredSize];
                detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
                SP_DEVINFO_DATA devInfoData;
                devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
                if (!SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, detailData, requiredSize, NULL, &devInfoData)) {
                    delete[] detailData; continue;
                }

                HANDLE hDrive = CreateFile(detailData->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                if (hDrive == INVALID_HANDLE_VALUE) { delete[] detailData; continue; }

                STORAGE_DEVICE_NUMBER sdn_check;
                if (DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn_check, sizeof(sdn_check), &bytesReturned, NULL) && sdn_check.DeviceNumber == diskNumber) {
                    CloseHandle(hDrive);
                    DEVINST devInst = devInfoData.DevInst;
                    DEVINST parentInst;
                    while (CM_Get_Parent(&parentInst, devInst, 0) == CR_SUCCESS) {
                        TCHAR buffer[512];
                        ULONG ulDataType; ULONG ulDataSize = sizeof(buffer);
                        if (CM_Get_DevNode_Registry_Property(parentInst, CM_DRP_CLASS, &ulDataType, (PBYTE)buffer, &ulDataSize, 0) == CR_SUCCESS) {
                            if (QString::fromWCharArray(buffer).toUpper() == "USB") {
                                CM_Get_Device_ID(parentInst, (PWSTR)buffer, MAX_PATH, 0);
                                m_driveMap[QString::fromWCharArray(buffer)] = driveLetter.left(2);
                                break;
                            }
                        }
                        devInst = parentInst;
                    }
                }
                else { CloseHandle(hDrive); }
                delete[] detailData;
                if (m_driveMap.key(driveLetter.left(2), "") != "") break;
            }
            SetupDiDestroyDeviceInfoList(hDevInfo);
        }
        pDrive += wcslen(pDrive) + 1;
    }
}


void UsbMonitorWindow::ejectSelectedDevice()
{
    QTreeWidgetItem* currentItem = m_deviceTree->currentItem();
    if (!currentItem || !currentItem->data(1, Qt::UserRole).toBool()) {
        QMessageBox::warning(this, "Информация", "Выбранное устройство не поддерживает безопасное извлечение.");
        return;
    }

    QString instanceId = currentItem->data(0, Qt::UserRole).toString();

    m_pendingEjectDeviceName = instanceId.toLower().replace('\\', '#');

    DEVINST devInst;
    if (CM_Locate_DevNode(&devInst, (DEVINSTID_W)instanceId.utf16(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        logMessage("[ОШИБКА] Не удалось найти узел устройства для извлечения. ID: " + instanceId);
        m_pendingEjectDeviceName.clear();
        return;
    }

    PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
    WCHAR vetoName[MAX_PATH];
    CONFIGRET cr = CM_Request_Device_Eject(devInst, &vetoType, vetoName, MAX_PATH, 0);

    if (cr == CR_SUCCESS) {
        logMessage(QString("[>] Запрос на безопасное извлечение '%1' отправлен...").arg(currentItem->text(0)));
    }
    else {
        logMessage(QString("[ОТКАЗ] В извлечении устройства '%1' отказано системой.").arg(currentItem->text(0)));
        m_pendingEjectDeviceName.clear(); 
        QMessageBox::critical(this, "Ошибка извлечения", "В извлечении устройства отказано системой.");
    }
}