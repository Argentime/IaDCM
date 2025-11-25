#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_HubWindow.h"
#include "QtLab1Window.h"
#include "WebcamWindow.h"
#include "UsbMonitorWindow.h"

// Прямое объявление класса, чтобы не подключать заголовок
class WebcamWindow;
class UsbMonitorWindow;
class BluetoothWindow;

class HubWindow : public QMainWindow
{
    Q_OBJECT

public:
    HubWindow(QWidget* parent = nullptr);
    ~HubWindow();

private:
    Ui::HubWindowClass ui;
    QMovie* movie;
    QtLab1Window* lab1Window; // окно для ЛР1
    WebcamWindow* webcamWindow; // окно для работы с камерой
    UsbMonitorWindow* usbWindow;
};