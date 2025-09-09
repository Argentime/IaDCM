#include "QtLab1Window.h"
#include <QDebug>
#include <QMessageBox>
#include <powrprof.h>  // для SetSuspendState
#pragma comment(lib, "PowrProf.lib")

QtLab1Window::QtLab1Window(QWidget* parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    // Таймер для обновления данных
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &QtLab1Window::updatePowerInfo);
    timer->start(2000); // обновляем раз в 2 секунды

    // Кнопки "Сон" и "Гибернация"
    connect(ui.pushButton, &QPushButton::clicked, this, &QtLab1Window::goSleep);
    connect(ui.pushButton_2, &QPushButton::clicked, this, &QtLab1Window::goHibernate);

    // Место для анимации Пен-Пена
    movie = new QMovie(":/HubWindow/Pen-Pen-eat.gif");
    ui.label_2->setScaledContents(true);

    updatePowerInfo();
}

QtLab1Window::~QtLab1Window() {}

void QtLab1Window::updatePowerInfo()
{
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps))
    {
        // Тип питания
        QString type = (sps.ACLineStatus == 1) ? "AC Power (Сеть)" :
            (sps.ACLineStatus == 0) ? "Battery" : "Unknown";
        ui.label_5->setText(type);

        // Тип батареи (условно, т.к. напрямую WinAPI не даёт)
        ui.label_6->setText(QString::number(sps.BatteryFlag));

        // Режим энергосбережения
        QString mode = (sps.SystemStatusFlag == 1) ? "Battery Saver ON" : "Normal";
        ui.label_7->setText(mode);

        // Время работы (в минутах)
        if (sps.BatteryLifeTime != (DWORD)-1)
            ui.label_8->setText(QString::number(sps.BatteryLifeTime / 60) + " min");
        else
            ui.label_8->setText("Unknown");

        // Уровень заряда
        int percent = (sps.BatteryLifePercent == 255) ? 0 : sps.BatteryLifePercent;
        ui.progressBar->setValue(percent);

        // Анимация Пен-Пена: ест, если подключено питание
        if (sps.ACLineStatus == 1) {
            ui.label_2->setMovie(movie);
            movie->start();
        }
        else {
            movie->stop();
            ui.label_2->setPixmap(QPixmap(":/HubWindow/Слой 2.png")); // обычная картинка
        }
    }
}

void QtLab1Window::goSleep()
{
    auto reply = QMessageBox::question(this, "Подтверждение",
        "Вы действительно хотите перевести компьютер в спящий режим?",
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        SetSuspendState(FALSE, FALSE, FALSE); // Сон
    }
}

void QtLab1Window::goHibernate()
{
    auto reply = QMessageBox::question(this, "Подтверждение",
        "Вы действительно хотите перевести компьютер в режим гибернации?",
        QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        SetSuspendState(TRUE, FALSE, FALSE); // Гибернация
    }
}
