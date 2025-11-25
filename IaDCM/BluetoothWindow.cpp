#include "BluetoothWindow.h"
#include <QFileDialog>

BluetoothWindow::BluetoothWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Bluetooth: Передача файлов");
    setMinimumSize(600, 400);

    initUI();

    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);

    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
        this, &BluetoothWindow::deviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
        this, &BluetoothWindow::discoveryFinished);

    startDiscovery();
}

BluetoothWindow::~BluetoothWindow()
{
    if (m_discoveryAgent->isActive()) {
        m_discoveryAgent->stop();
    }
}

void BluetoothWindow::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_statusLabel = new QLabel("Инициализация...", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    m_deviceList = new QListWidget(this);

    m_scanButton = new QPushButton("Сканировать устройства", this);
    m_sendButton = new QPushButton("Отправить файл (Мастер Windows)", this);
    m_sendButton->setEnabled(false);

    m_mainLayout->addWidget(m_statusLabel);
    m_mainLayout->addWidget(m_deviceList);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addWidget(m_scanButton);
    btnLayout->addWidget(m_sendButton);
    m_mainLayout->addLayout(btnLayout);

    connect(m_scanButton, &QPushButton::clicked, this, &BluetoothWindow::startDiscovery);
    connect(m_sendButton, &QPushButton::clicked, this, &BluetoothWindow::sendFileToSelectedDevice);

    connect(m_deviceList, &QListWidget::itemSelectionChanged, [=]() {
        m_sendButton->setEnabled(!m_deviceList->selectedItems().isEmpty());
        });

    QString qss = R"(
        QWidget { background-color: #1a202c; color: #e2e8f0; font-family: "Segoe UI"; }
        QListWidget { background-color: #2d3748; border: 1px solid #2b6cb0; border-radius: 4px; }
        QPushButton { background-color: #2b6cb0; color: white; border: none; padding: 8px; border-radius: 4px; }
        QPushButton:hover { background-color: #3182ce; }
        QPushButton:disabled { background-color: #4a5568; }
    )";
    this->setStyleSheet(qss);
}

void BluetoothWindow::startDiscovery()
{
    m_deviceList->clear();
    m_foundDevices.clear();
    m_statusLabel->setText("Поиск устройств...");
    m_discoveryAgent->start();
    m_scanButton->setEnabled(false);
}

void BluetoothWindow::deviceDiscovered(const QBluetoothDeviceInfo& device)
{
    for (const auto& d : m_foundDevices) {
        if (d.address() == device.address()) return;
    }

    m_foundDevices.append(device);
    QString label = QString("%1 (%2)").arg(device.name(), device.address().toString());

    // Определяем тип устройства
    QBluetoothDeviceInfo::MajorDeviceClass major = device.majorDeviceClass();
    if (major == QBluetoothDeviceInfo::PhoneDevice) {
        label += " [ТЕЛЕФОН]";
    }
    else if (major == QBluetoothDeviceInfo::ComputerDevice) {
        label += " [ПК]";
    }

    m_deviceList->addItem(label);
}

void BluetoothWindow::discoveryFinished()
{
    m_statusLabel->setText("Сканирование завершено.");
    m_scanButton->setEnabled(true);
}

void BluetoothWindow::sendFileToSelectedDevice()
{
    // В Qt 6 классы для прямой отправки файлов удалены.
    // Мы используем системную интеграцию (System Call) для запуска 
    // штатного мастера передачи файлов Windows (fsquirt.exe).

    m_statusLabel->setText("Запуск мастера передачи файлов Windows...");

    // fsquirt.exe - это родная утилита Windows для Bluetooth передачи
    bool started = QProcess::startDetached("fsquirt.exe");

    if (started) {
        QMessageBox::information(this, "Инструкция",
            "Запущен системный мастер Bluetooth.\n\n"
            "1. Выберите 'Отправить файлы'.\n"
            "2. Выберите устройство из списка (тот же телефон).\n"
            "3. Выберите файл для отправки.\n\n"
            "После завершения нажмите на уведомление на телефоне для воспроизведения!");
    }
    else {
        QMessageBox::critical(this, "Ошибка", "Не удалось запустить мастер Bluetooth (fsquirt.exe).");
    }
}