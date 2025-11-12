#include "QtLab1Window.h"
#include <QDebug>
#include <QMessageBox>
#include <powrprof.h>
#pragma comment(lib, "PowrProf.lib")

QtLab1Window::QtLab1Window(QWidget* parent)
    : QWidget(parent)
{
    ui.setupUi(this);

    // --- Таймер для обновления данных ---
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updatePowerInfo()));
    timer->start(2000); // обновляем раз в 2 секунды

    // --- Кнопки ---
    connect(ui.pushButton, SIGNAL(clicked()), this, SLOT(goSleep()));
    connect(ui.pushButton_2, SIGNAL(clicked()), this, SLOT(goHibernate()));

    // --- Настройка анимации Пен-Пена ---
    movie = new QMovie(":/HubWindow/Pen-Pen-eat.gif");
    ui.label_2->setScaledContents(true);

    updatePowerInfo();
}

QtLab1Window::~QtLab1Window()
{
}

void QtLab1Window::updatePowerInfo()
{
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps))
    {
        // --- Тип питания ---
        QString type;
        if (sps.ACLineStatus == 1)
            type = "AC Power (Сеть)";
        else if (sps.ACLineStatus == 0)
            type = "Battery";
        else
            type = "Unknown";

        ui.label_5->setText(type);

        // --- Флаг батареи ---
        ui.label_6->setText(QString::number(sps.BatteryFlag));

        // --- Режим энергосбережения ---
        QString mode = (sps.SystemStatusFlag == 1) ? "Battery Saver ON" : "Normal";
        ui.label_7->setText(mode);

        // --- Время работы аккумулятора ---
        if (sps.BatteryLifeTime != (DWORD)-1)
            ui.label_8->setText(QString::number(sps.BatteryLifeTime / 60) + " min");
        else
            ui.label_8->setText("Unknown");

        // --- Уровень заряда ---
        int percent = (sps.BatteryLifePercent == 255) ? 0 : sps.BatteryLifePercent;
        ui.progressBar->setValue(percent);

        // --- Анимация Пен-Пена ---
        if (sps.ACLineStatus == 1) {
            ui.label_2->setMovie(movie);
            movie->start();
        }
        else {
            movie->stop();
            ui.label_2->setPixmap(QPixmap(":/HubWindow/Слой 2.png"));
        }
    }
}

void QtLab1Window::goSleep()
{
    int reply = QMessageBox::question(
        this,
        tr("Подтверждение"),
        tr("Вы действительно хотите перевести компьютер в спящий режим?"),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        SetSuspendState(FALSE, FALSE, FALSE); // Сон
    }
}

void QtLab1Window::goHibernate()
{
    int reply = QMessageBox::question(
        this,
        tr("Подтверждение"),
        tr("Вы действительно хотите перевести компьютер в режим гибернации?"),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        SetSuspendState(TRUE, FALSE, FALSE); // Гибернация
    }
}
