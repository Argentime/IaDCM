#pragma once

#include <QWidget>
#include <QTimer>
#include <QMovie>
#include "ui_QtLab1Window.h"
#include <windows.h> // для GetSystemPowerStatus

class QtLab1Window : public QWidget
{
    Q_OBJECT

public:
    QtLab1Window(QWidget* parent = nullptr);
    ~QtLab1Window();

private slots:
    void updatePowerInfo();
    void goSleep();
    void goHibernate();

private:
    Ui::QtLab1WindowClass ui;
    QTimer* timer;
    QMovie* movie;
};
