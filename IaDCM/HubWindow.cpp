#include "HubWindow.h"
#include "QtLab1Window.h"
#include "WebcamWindow.h" // Подключаем заголовок нового окна
#include "UsbMonitorWindow.h"
#include "BluetoothWindow.h"

HubWindow::HubWindow(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    movie = new QMovie(":/HubWindow/Pen-Pen-eat.gif");
    ui.label_2->setScaledContents(true); 
    ui.label_2->setMovie(movie);
    movie->start();

    // --- Кнопка для открытия окна с информацией о питании ---
    connect(ui.pushButton, &QPushButton::clicked, this, [=]() {
        QtLab1Window* lab1Window = new QtLab1Window();
        lab1Window->setAttribute(Qt::WA_DeleteOnClose);

        this->hide();
        lab1Window->show();

        connect(lab1Window, &QObject::destroyed, this, [=]() {
            this->show();
            });
        });

    // --- Кнопка для открытия окна с веб-камерой ---
    connect(ui.pushButton_3, &QPushButton::clicked, this, [=]() {
        WebcamWindow* webcamWindow = new WebcamWindow();
        webcamWindow->setAttribute(Qt::WA_DeleteOnClose);

        this->hide();
        webcamWindow->show();

        connect(webcamWindow, &QObject::destroyed, this, [=]() {
            this->show();
            });
        });
    connect(ui.pushButton_4, &QPushButton::clicked, this, [=]() {
        UsbMonitorWindow* usbWindow = new UsbMonitorWindow();
        usbWindow->setAttribute(Qt::WA_DeleteOnClose);

        this->hide();
        usbWindow->show();

        connect(usbWindow, &QObject::destroyed, this, [=]() {
            this->show();
            });
        });
    // HubWindow.cpp внутри конструктора

// Кнопка Bluetooth
// Предположим, кнопка называется pushButton_Bluetooth или создайте новую
    connect(ui.pushButton_5, &QPushButton::clicked, this, [=]() {
        BluetoothWindow* btWindow = new BluetoothWindow();
        btWindow->setAttribute(Qt::WA_DeleteOnClose);

        this->hide();
        btWindow->show();

        connect(btWindow, &QObject::destroyed, this, [=]() {
            this->show();
            });
        });
}

HubWindow::~HubWindow() {}