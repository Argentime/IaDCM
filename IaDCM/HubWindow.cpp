#include "HubWindow.h"
#include "QtLab1Window.h"

HubWindow::HubWindow(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    // при клике на "Энергопитание"
    connect(ui.pushButton, &QPushButton::clicked, this, [=]() {
        QtLab1Window* lab1Window = new QtLab1Window();
        lab1Window->setAttribute(Qt::WA_DeleteOnClose); // удалять при закрытии

        this->hide();  // скрываем главное меню
        lab1Window->show();

        // когда окно закроется ? вернуть главное меню
        connect(lab1Window, &QObject::destroyed, this, [=]() {
            this->show();
            });
        });
}

HubWindow::~HubWindow() {}
