#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_HubWindow.h"
#include "QtLab1Window.h"

class HubWindow : public QMainWindow
{
    Q_OBJECT

public:
    HubWindow(QWidget *parent = nullptr);
    ~HubWindow();

private:
    Ui::HubWindowClass ui;
    QtLab1Window* lab1Window; // окно для ЛР1
};

