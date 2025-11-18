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
            if (!hdr || hdr->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE) {
                return QWidget::nativeEvent(eventType, message, result);
            }
            PDEV_BROADCAST_DEVICEINTERFACE pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)hdr;
            QString devicePath = QString::fromWCharArray(pDevInf->dbcc_name);

            switch (msg->wParam)
            {
            case DBT_DEVICEARRIVAL:
                logMessage(QString("[+] Подключено новое устройство: %1").arg(devicePath));
                populateDeviceList();
                break;
            case DBT_DEVICEREMOVECOMPLETE:
                if (devicePath == m_pendingEjectDeviceName) {
                    logMessage(QString("[OK] Устройство было безопасно извлечено: %1").arg(devicePath));
                    m_pendingEjectDeviceName.clear();
                }
                else {
                    logMessage(QString("[!!!] НЕБЕЗОПАСНОЕ ИЗВЛЕЧЕНИЕ: Устройство было отключено внезапно: %1").arg(devicePath));
                }
                populateDeviceList();
                break;
            case DBT_DEVICEQUERYREMOVE:
                logMessage(QString("[?] Запрос на безопасное извлечение устройства: %1").arg(devicePath));
                m_pendingEjectDeviceName = devicePath;
                break;
            case DBT_DEVICEQUERYREMOVEFAILED:
                logMessage(QString("[ОТКАЗ] Запрос на безопасное извлечение был отклонен: %1").arg(devicePath));
                m_pendingEjectDeviceName.clear();
                break;
            }
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void UsbMonitorWindow::populateDeviceList()
{
    m_deviceTree->clear();
    logMessage("Сканирование подключенных USB-устройств...");
    buildDriveMap(); // Обновляем нашу карту дисков

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
        item->setText(2, m_driveMap.value(deviceInfo.instanceId, "")); // Ищем букву диска в карте
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

        // Корректная логика для Windows 10/11:
        // Устройство можно извлекать, если оно физически съемное И не помечено как "ok для внезапного извлечения".
        // Это ловит устройства со старой политикой "Повышение производительности".
        if (isRemovable && !isSurpriseRemovable) {
            device.isEjectable = true;
        }

        // Для флешек с новой политикой "Быстрое удаление" isSurpriseRemovable=true.
        // Мы определим их извлекаемость по наличию буквы диска.
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
            // Используем логику из предыдущего ответа, чтобы найти InstanceID по букве диска
            // (Код функции getDriveInfo из предыдущего ответа, адаптированный)
            // ... Этот код остается таким же сложным, как и раньше, и находит InstanceID родительского USB-узла ...
            // В целях краткости, представим, что он возвращает нам ID:
            // QString instanceId = findInstanceIdForDrive(driveLetter);
            // m_driveMap[instanceId] = driveLetter;

            // Реализация поиска InstanceID по букве диска (из предыдущего ответа)
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

    DEVINST devInst;
    if (CM_Locate_DevNode(&devInst, (DEVINSTID_W)instanceId.utf16(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        logMessage("[ОШИБКА] Не удалось найти узел устройства для извлечения.");
        return;
    }

    PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
    WCHAR vetoName[MAX_PATH];
    CONFIGRET cr = CM_Request_Device_Eject(devInst, &vetoType, vetoName, MAX_PATH, 0);

    // Эта функция не блокирующая, она лишь отправляет запрос.
    // Результат мы увидим в nativeEvent, когда придет DBT_DEVICEREMOVECOMPLETE или DBT_DEVICEQUERYREMOVEFAILED.
    if (cr == CR_SUCCESS) {
        logMessage(QString("[>] Запрос на безопасное извлечение '%1' отправлен...").arg(currentItem->text(0)));
    }
    else {
        logMessage(QString("[ОТКАЗ] Не удалось отправить запрос на извлечение '%1'.").arg(currentItem->text(0)));
        QMessageBox::critical(this, "Ошибка извлечения", "Не удалось отправить запрос на извлечение устройства.");
    }
}