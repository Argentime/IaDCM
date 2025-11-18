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

// Необходимая библиотека для компоновщика (альтернатива настройке в VS)
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
    // Очень важно отменить регистрацию уведомлений при закрытии окна
    if (m_hDeviceNotify) {
        UnregisterDeviceNotification(m_hDeviceNotify);
    }
}

void UsbMonitorWindow::initUI()
{
    // --- Создание виджетов ---
    m_deviceTree = new QTreeWidget(this);
    m_deviceTree->setColumnCount(3);
    m_deviceTree->setHeaderLabels({ "Устройство", "Тип", "Извлекаемое" });
    m_deviceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_consoleLog = new QTextEdit(this);
    m_consoleLog->setReadOnly(true);
    m_consoleLog->setFixedHeight(150);

    m_refreshButton = new QPushButton("Обновить список", this);
    m_ejectButton = new QPushButton("Безопасно извлечь", this);

    // --- Компоновка ---
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QHBoxLayout* buttonsLayout = new QHBoxLayout();

    buttonsLayout->addWidget(m_refreshButton);
    buttonsLayout->addWidget(m_ejectButton);

    mainLayout->addWidget(m_deviceTree);
    mainLayout->addLayout(buttonsLayout);
    mainLayout->addWidget(m_consoleLog);

    // --- Соединение сигналов и слотов ---
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
    // GUID_DEVINTERFACE_USB_DEVICE - стандартный GUID для всех USB-устройств
    NotificationFilter.dbcc_classguid = GUID_DEVINTERFACE_USB_DEVICE;

    m_hDeviceNotify = RegisterDeviceNotification(
        (HANDLE)this->winId(),      // Сообщения будут приходить этому окну
        &NotificationFilter,
        DEVICE_NOTIFY_WINDOW_HANDLE
    );

    if (!m_hDeviceNotify) {
        logMessage("[ОШИБКА] Не удалось зарегистрировать получение уведомлений об устройствах.");
    }
}

bool UsbMonitorWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);

        if (msg->message == WM_DEVICECHANGE) {
            // Как только мы поймали нужное сообщение, обрабатываем его
            switch (msg->wParam)
            {
            case DBT_DEVICEARRIVAL:
                logMessage("[+] Обнаружено новое USB-устройство. Обновление списка...");
                populateDeviceList();
                break;

            case DBT_DEVICEREMOVECOMPLETE:
                // Мы не можем точно знать, было ли извлечение безопасным,
                // но можем зафиксировать сам факт.
                logMessage("[-] USB-устройство было извлечено. Обновление списка...");
                populateDeviceList();
                break;

            case DBT_DEVICEQUERYREMOVE:
                logMessage("[?] Система запрашивает разрешение на безопасное извлечение устройства.");
                // Мы не должны блокировать извлечение, поэтому просто разрешаем
                *result = TRUE;
                return true;

            case DBT_DEVICEQUERYREMOVEFAILED:
                logMessage("[ОТКАЗ] Запрос на безопасное извлечение был отклонен системой (возможно, файлы заняты).");
                *result = BROADCAST_QUERY_DENY;
                return true;
            }
        }
    }
    // Передаем все остальные сообщения для стандартной обработки
    return QWidget::nativeEvent(eventType, message, result);
}

void UsbMonitorWindow::populateDeviceList()
{
    m_deviceTree->clear();
    logMessage("Сканирование подключенных USB-устройств...");

    // Получаем список всех присутствующих в системе устройств
    HDEVINFO hDevInfo = SetupDiGetClassDevs(NULL, L"USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) return;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    // Перебираем все устройства в списке
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i)
    {
        UsbDevice deviceInfo = getDeviceInfo(hDevInfo, devInfoData);
        if (deviceInfo.name.isEmpty()) continue; // Пропускаем пустые

        QTreeWidgetItem* item = new QTreeWidgetItem(m_deviceTree);
        item->setText(0, deviceInfo.name);
        item->setText(1, deviceInfo.type);
        item->setText(2, deviceInfo.isEjectable ? "Да" : "Нет");

        // Сохраняем instanceId в данных элемента, он понадобится для извлечения
        item->setData(0, Qt::UserRole, deviceInfo.instanceId);
        item->setData(1, Qt::UserRole, deviceInfo.isEjectable);
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
}

UsbDevice UsbMonitorWindow::getDeviceInfo(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfoData)
{
    UsbDevice device;
    TCHAR buffer[512];

    // --- 1. Получаем базовую информацию о текущем узле (без изменений) ---
    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
        device.name = QString::fromWCharArray(buffer);
    } else if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
        device.name = QString::fromWCharArray(buffer);
    }

    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_CLASS, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
        device.type = QString::fromWCharArray(buffer);
    }
    
    if (CM_Get_Device_ID(devInfoData.DevInst, buffer, MAX_PATH, 0) == CR_SUCCESS) {
        device.instanceId = QString::fromWCharArray(buffer);
    }

    // --- 2. ФИНАЛЬНАЯ, НАИБОЛЕЕ НАДЕЖНАЯ ЛОГИКА ПРОВЕРКИ ---
    // Мы возвращаемся к проверке SPDRP_CAPABILITIES, но с более точными условиями.
    // Этот метод не требует спуска по дереву устройств, так как нужные флаги
    // обычно установлены на родительском узле, который мы и находим.

    DWORD capabilities = 0;
    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_CAPABILITIES, NULL, (PBYTE)&capabilities, sizeof(capabilities), NULL))
    {
        // Устройство считается извлекаемым, если оно:
        // 1. Помечено как REMOVABLE (физически отсоединяемое).
        // 2. И НЕ помечено как SURPRISEREMOVALOK (его НЕЛЬЗЯ выдергивать просто так).
        // Это и есть точное определение устройства, требующего "Безопасного извлечения".
        bool isRemovable = (capabilities & CM_DEVCAP_REMOVABLE);
        bool isSurpriseRemovable = (capabilities & CM_DEVCAP_SURPRISEREMOVALOK);

        if (isRemovable && isSurpriseRemovable) {
            device.isEjectable = true;
        }
    }

    return device;
}

void UsbMonitorWindow::ejectSelectedDevice()
{
    QTreeWidgetItem* currentItem = m_deviceTree->currentItem();
    if (!currentItem) {
        QMessageBox::warning(this, "Ошибка", "Пожалуйста, выберите устройство из списка.");
        return;
    }

    bool isEjectable = currentItem->data(1, Qt::UserRole).toBool();
    if (!isEjectable) {
        QMessageBox::information(this, "Информация", "Это устройство не поддерживает безопасное извлечение.");
        return;
    }

    QString instanceId = currentItem->data(0, Qt::UserRole).toString();

    DEVINST devInst;
    if (CM_Locate_DevNode(&devInst, (DEVINSTID_W)instanceId.utf16(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        logMessage("[ОШИБКА] Не удалось найти узел устройства для извлечения.");
        return;
    }

    // Отправляем запрос на извлечение
    PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
    WCHAR vetoName[MAX_PATH];
    CONFIGRET cr = CM_Request_Device_Eject(devInst, &vetoType, vetoName, MAX_PATH, 0);

    if (cr == CR_SUCCESS) {
        logMessage(QString("[OK] Запрос на безопасное извлечение устройства '%1' успешно отправлен.").arg(currentItem->text(0)));
        QMessageBox::information(this, "Успех", "Теперь устройство можно безопасно извлечь.");
    }
    else {
        logMessage(QString("[ОТКАЗ] Не удалось безопасно извлечь устройство '%1'.").arg(currentItem->text(0)));
        QMessageBox::critical(this, "Ошибка извлечения", "Не удалось безопасно извлечь устройство. Возможно, оно используется другой программой.");
    }
}