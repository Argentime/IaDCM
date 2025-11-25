#include "BluetoothWindow.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QDataStream>

BluetoothWindow::BluetoothWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Bluetooth: Прямая передача (Socket)");
    setMinimumSize(600, 450);

    initUI();

    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
        this, &BluetoothWindow::deviceDiscovered);
    connect(m_discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
        this, &BluetoothWindow::deviceScanFinished);

    // Инициализация сокета (RFCOMM)
    m_socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);

    connect(m_socket, &QBluetoothSocket::connected, this, &BluetoothWindow::socketConnected);
    connect(m_socket, &QBluetoothSocket::disconnected, this, &BluetoothWindow::socketDisconnected);
    connect(m_socket, &QBluetoothSocket::readyRead, this, &BluetoothWindow::socketReadyRead);
    connect(m_socket, &QBluetoothSocket::errorOccurred, this, &BluetoothWindow::socketErrorOccurred);

    startDiscovery();
}

BluetoothWindow::~BluetoothWindow()
{
    if (m_socket->isOpen()) m_socket->close();
}

void BluetoothWindow::initUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_statusLabel = new QLabel("Готов к работе", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    m_deviceList = new QListWidget(this);

    m_scanButton = new QPushButton("Сканировать", this);
    m_sendButton = new QPushButton("Отправить файл (Raw Socket)", this);
    m_sendButton->setEnabled(false);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);

    m_mainLayout->addWidget(m_statusLabel);
    m_mainLayout->addWidget(m_deviceList);
    m_mainLayout->addWidget(m_progressBar);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addWidget(m_scanButton);
    btnLayout->addWidget(m_sendButton);
    m_mainLayout->addLayout(btnLayout);

    connect(m_scanButton, &QPushButton::clicked, this, &BluetoothWindow::startDiscovery);
    connect(m_sendButton, &QPushButton::clicked, this, &BluetoothWindow::startFileTransfer);

    connect(m_deviceList, &QListWidget::itemSelectionChanged, [=]() {
        m_sendButton->setEnabled(!m_deviceList->selectedItems().isEmpty());
        });

    // Стили (можно оставить те же)
    this->setStyleSheet("QWidget { background: #1a202c; color: white; } QListWidget { background: #2d3748; } QPushButton { background: #2b6cb0; padding: 5px; }");
}

void BluetoothWindow::startDiscovery()
{
    m_deviceList->clear();
    m_foundDevices.clear();
    m_statusLabel->setText("Поиск устройств...");
    m_scanButton->setEnabled(false);
    m_discoveryAgent->start();
}

void BluetoothWindow::deviceDiscovered(const QBluetoothDeviceInfo& device)
{
    // Фильтр дубликатов
    for (const auto& d : m_foundDevices) {
        if (d.address() == device.address()) return;
    }

    // Нас интересуют телефоны (OPP service)
    // Но часто UUID не видны сразу, поэтому добавляем все, фильтруем по классу
    QBluetoothDeviceInfo::MajorDeviceClass major = device.majorDeviceClass();

    m_foundDevices.append(device);
    QString label = QString("%1 (%2)").arg(device.name(), device.address().toString());

    if (major == QBluetoothDeviceInfo::PhoneDevice) label += " [ТЕЛЕФОН]";
    if (major == QBluetoothDeviceInfo::ComputerDevice) label += " [ПК]";

    QListWidgetItem* item = new QListWidgetItem(label);
    item->setData(Qt::UserRole, m_foundDevices.size() - 1);
    m_deviceList->addItem(item);
}

void BluetoothWindow::deviceScanFinished()
{
    m_statusLabel->setText("Сканирование завершено.");
    m_scanButton->setEnabled(true);
}

// --- НАЧАЛО ЛОГИКИ ПЕРЕДАЧИ ---

void BluetoothWindow::startFileTransfer()
{
    if (m_deviceList->selectedItems().isEmpty()) return;

    int index = m_deviceList->selectedItems().first()->data(Qt::UserRole).toInt();
    QBluetoothDeviceInfo remoteDevice = m_foundDevices[index];

    QString fileName = QFileDialog::getOpenFileName(this, "Выберите файл", "", "Audio (*.mp3 *.wav);;All (*.*)");
    if (fileName.isEmpty()) return;

    if (m_file) { delete m_file; m_file = nullptr; }
    m_file = new QFile(fileName);
    if (!m_file->open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Ошибка", "Не удалось открыть файл.");
        return;
    }

    m_statusLabel->setText("Подключение к сокету OBEX...");
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_sendButton->setEnabled(false);

    // Подключаемся к сервису Object Push (UUID: 00001105...)
    m_socket->connectToService(remoteDevice.address(), QBluetoothUuid::ServiceClassUuid::ObexObjectPush);
}

void BluetoothWindow::socketConnected()
{
    m_statusLabel->setText("Сокет подключен. Инициализация OBEX...");
    // 1. Отправляем OBEX CONNECT
    sendObexConnect();
}

void BluetoothWindow::socketErrorOccurred(QBluetoothSocket::SocketError error)
{
    m_statusLabel->setText("Ошибка сокета: " + m_socket->errorString());
    m_sendButton->setEnabled(true);
    if (m_file) m_file->close();
}

void BluetoothWindow::socketDisconnected()
{
    m_statusLabel->setText("Сокет закрыт.");
    m_sendButton->setEnabled(true);
    if (m_file) m_file->close();
}

// --- РЕАЛИЗАЦИЯ ПРОТОКОЛА OBEX (Ручная сборка пакетов) ---

void BluetoothWindow::sendObexConnect()
{
    m_state = ConnectingObex;

    // Структура пакета CONNECT:
    // [OpCode: 0x80] [Len: 2 bytes] [Ver: 0x10] [Flags: 0x00] [MaxPacket: 2 bytes]
    QByteArray packet;
    packet.append((char)0x80); // OpCode: Connect
    packet.append((char)0x00); packet.append((char)0x07); // Length: 7 bytes total
    packet.append((char)0x10); // Version 1.0
    packet.append((char)0x00); // Flags
    packet.append((char)0x20); packet.append((char)0x00); // Max packet size ~8k (0x2000)

    m_socket->write(packet);
}

void BluetoothWindow::sendObexPut()
{
    QByteArray packet;
    bool firstPacket = (m_file->pos() == 0);
    bool finalPacket = false;

    // Лимит данных = МаксПакет - Заголовки (грубо берем 1KB для безопасности)
    int chunkSize = 1024;
    QByteArray fileData = m_file->read(chunkSize);

    if (m_file->atEnd()) {
        finalPacket = true;
    }

    // 1. OpCode
    // 0x02 = PUT (есть еще данные), 0x82 = PUT Final (последний пакет)
    packet.append(finalPacket ? (char)0x82 : (char)0x02);

    // 2. Placeholder для длины пакета (2 байта), заполним в конце
    packet.append((char)0x00); packet.append((char)0x00);

    // 3. Заголовки (Добавляем только в первый пакет)
    if (firstPacket) {
        m_state = SendingMetadata;

        // --- Header: NAME (0x01) ---
        // Имя должно быть в кодировке UTF-16 Big Endian с завершающим нулем
        QFileInfo fi(*m_file);
        QString fname = fi.fileName();
        QByteArray nameData;

        // Конвертация в UTF-16BE
        const ushort* utf16 = fname.utf16();
        int len = fname.length();
        for (int i = 0; i < len; ++i) {
            nameData.append((char)((utf16[i] >> 8) & 0xFF));
            nameData.append((char)(utf16[i] & 0xFF));
        }
        // Null terminator (2 bytes)
        nameData.append((char)0x00); nameData.append((char)0x00);

        packet.append((char)0x01); // Header ID: Name
        // Длина заголовка (ID + 2 байта длины + данные)
        int headerLen = 3 + nameData.size();
        packet.append((char)((headerLen >> 8) & 0xFF));
        packet.append((char)(headerLen & 0xFF));
        packet.append(nameData);

        // --- Header: LENGTH (0xC3) ---
        // Общий размер файла (4 байта)
        quint32 fileSize = (quint32)m_file->size();
        packet.append((char)0xC3); // Header ID: Length
        packet.append((char)((fileSize >> 24) & 0xFF));
        packet.append((char)((fileSize >> 16) & 0xFF));
        packet.append((char)((fileSize >> 8) & 0xFF));
        packet.append((char)(fileSize & 0xFF));
    }
    else {
        m_state = SendingBody;
    }

    // --- Header: BODY (0x48) или END OF BODY (0x49) ---
    // Данные файла
    packet.append(finalPacket ? (char)0x49 : (char)0x48);

    // Длина заголовка тела (ID + 2 байта длины + данные)
    int bodyHeaderLen = 3 + fileData.size();
    packet.append((char)((bodyHeaderLen >> 8) & 0xFF));
    packet.append((char)(bodyHeaderLen & 0xFF));
    packet.append(fileData);

    // 4. Финализируем длину всего пакета
    int totalLen = packet.size();
    packet[1] = (char)((totalLen >> 8) & 0xFF);
    packet[2] = (char)(totalLen & 0xFF);

    m_socket->write(packet);

    // Обновляем прогресс
    if (m_file->size() > 0) {
        int percent = (int)((m_file->pos() * 100) / m_file->size());
        m_progressBar->setValue(percent);
    }
}

void BluetoothWindow::sendObexDisconnect()
{
    m_state = DisconnectingObex;
    // Пакет DISCONNECT: [0x81] [Len: 00 03]
    QByteArray packet;
    packet.append((char)0x81);
    packet.append((char)0x00); packet.append((char)0x03);
    m_socket->write(packet);
}

// --- ЧТЕНИЕ ОТВЕТОВ ОТ ТЕЛЕФОНА ---

void BluetoothWindow::socketReadyRead()
{
    QByteArray response = m_socket->readAll();
    if (response.size() < 3) return; // Слишком короткий ответ

    // Первый байт ответа - код ответа OBEX
    // 0xA0 = Success / OK
    // 0x90 = Continue (нужно слать еще данные)
    // 0xC0+ = Ошибки (Bad Request и т.д.)

    unsigned char code = (unsigned char)response[0];

    // Логика конечного автомата
    switch (m_state) {
    case ConnectingObex:
        if (code == 0xA0) { // Connect OK
            m_statusLabel->setText("OBEX подключен. Отправка файла...");
            sendObexPut(); // Начинаем слать файл
        }
        else {
            m_statusLabel->setText(QString("Ошибка Connect: 0x%1").arg(code, 0, 16));
        }
        break;

    case SendingMetadata:
    case SendingBody:
        if (code == 0x90) { // Continue (Телефон просит еще данные)
            sendObexPut();
        }
        else if (code == 0xA0) { // Success (Передача завершена!)
            m_statusLabel->setText("Файл передан!");
            sendObexDisconnect();
        }
        else {
            m_statusLabel->setText(QString("Ошибка отправки: 0x%1").arg(code, 0, 16));
        }
        break;

    case DisconnectingObex:
        if (code == 0xA0) {
            m_statusLabel->setText("Готово. Нажмите уведомление на телефоне!");
            m_progressBar->setValue(100);
            QMessageBox::information(this, "Успех", "Файл отправлен!\n\nПроверьте телефон: должно появиться уведомление 'Входящий файл' или 'Файл получен'. Нажмите на него для воспроизведения.");
            m_socket->disconnectFromService();
        }
        break;

    default: break;
    }
}