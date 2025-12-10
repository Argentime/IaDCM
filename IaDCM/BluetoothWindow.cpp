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
    startDiscovery();
}

BluetoothWindow::~BluetoothWindow()
{
    if (m_socket) {
        if (m_socket->isOpen()) m_socket->close();
    }
    if (m_file) {
        delete m_file;
    }
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


void BluetoothWindow::startFileTransfer()
{
    if (m_deviceList->selectedItems().isEmpty()) return;

    int index = m_deviceList->selectedItems().first()->data(Qt::UserRole).toInt();
    QBluetoothDeviceInfo remoteDevice = m_foundDevices[index];

    QString fileName = QFileDialog::getOpenFileName(this, "Выберите файл", "", "Audio (*.mp3 *.wav);;All (*.*)");
    if (fileName.isEmpty()) return;

    // Сброс файла
    if (m_file) { delete m_file; m_file = nullptr; }
    m_file = new QFile(fileName);
    if (!m_file->open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Ошибка", "Не удалось открыть файл.");
        return;
    }

    if (m_socket) {
        if (m_socket->state() != QBluetoothSocket::SocketState::UnconnectedState) {
            m_socket->abort();
        }
        delete m_socket;
        m_socket = nullptr;
    }

    m_socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this);

    connect(m_socket, &QBluetoothSocket::connected, this, &BluetoothWindow::socketConnected);
    connect(m_socket, &QBluetoothSocket::disconnected, this, &BluetoothWindow::socketDisconnected);
    connect(m_socket, &QBluetoothSocket::readyRead, this, &BluetoothWindow::socketReadyRead);
    connect(m_socket, &QBluetoothSocket::errorOccurred, this, &BluetoothWindow::socketErrorOccurred);

    m_statusLabel->setText("Подключение к сокету OBEX...");
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_sendButton->setEnabled(false);

    m_state = Idle;

    m_socket->connectToService(remoteDevice.address(), QBluetoothUuid::ServiceClassUuid::ObexObjectPush);
}

void BluetoothWindow::socketConnected()
{
    m_statusLabel->setText("Сокет подключен. Инициализация OBEX...");
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


void BluetoothWindow::sendObexConnect()
{
    m_state = ConnectingObex;

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

    int chunkSize = 1024;
    QByteArray fileData = m_file->read(chunkSize);

    if (m_file->atEnd()) {
        finalPacket = true;
    }

    // 0x02 = PUT , 0x82 = PUT Final
    packet.append(finalPacket ? (char)0x82 : (char)0x02);

    packet.append((char)0x00); packet.append((char)0x00);

    // Заголовки 
    if (firstPacket) {
        m_state = SendingMetadata;

        // --- Header: NAME (0x01) ---
        QFileInfo fi(*m_file);
        QString fname = fi.fileName();
        QByteArray nameData;

        const ushort* utf16 = fname.utf16();
        int len = fname.length();
        for (int i = 0; i < len; ++i) {
            nameData.append((char)((utf16[i] >> 8) & 0xFF));
            nameData.append((char)(utf16[i] & 0xFF));
        }
        nameData.append((char)0x00); nameData.append((char)0x00);

        packet.append((char)0x01); // Header ID: Name
        int headerLen = 3 + nameData.size();
        packet.append((char)((headerLen >> 8) & 0xFF));
        packet.append((char)(headerLen & 0xFF));
        packet.append(nameData);

        // --- Header: LENGTH (0xC3) ---
        quint32 fileSize = (quint32)m_file->size();
        packet.append((char)0xC3); 
        packet.append((char)((fileSize >> 24) & 0xFF));
        packet.append((char)((fileSize >> 16) & 0xFF));
        packet.append((char)((fileSize >> 8) & 0xFF));
        packet.append((char)(fileSize & 0xFF));
    }
    else {
        m_state = SendingBody;
    }

    // --- Header: BODY (0x48) или END OF BODY (0x49) ---
    packet.append(finalPacket ? (char)0x49 : (char)0x48);

    int bodyHeaderLen = 3 + fileData.size();
    packet.append((char)((bodyHeaderLen >> 8) & 0xFF));
    packet.append((char)(bodyHeaderLen & 0xFF));
    packet.append(fileData);

    int totalLen = packet.size();
    packet[1] = (char)((totalLen >> 8) & 0xFF);
    packet[2] = (char)(totalLen & 0xFF);

    m_socket->write(packet);

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


void BluetoothWindow::socketReadyRead()
{
    QByteArray response = m_socket->readAll();
    if (response.size() < 3) return;

    unsigned char code = (unsigned char)response[0];

    switch (m_state) {
    case ConnectingObex:
        if (code == 0xA0) { // Connect OK
            m_statusLabel->setText("OBEX подключен. Отправка файла...");
            sendObexPut();
        }
        else {
            m_statusLabel->setText(QString("Ошибка Connect: 0x%1").arg(code, 0, 16));
        }
        break;

    case SendingMetadata:
    case SendingBody:
        if (code == 0x90) { // Continue 
            sendObexPut();
        }
        else if (code == 0xA0) { // Success 
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
            QMessageBox::information(this, "Успех", "Файл отправлен!,,");
            m_socket->disconnectFromService();
        }
        break;

    default: break;
    }
}