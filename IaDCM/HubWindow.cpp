#include "HubWindow.h"
#include "QtLab1Window.h"

HubWindow::HubWindow(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    // ��� ����� �� "�������������"
    connect(ui.pushButton, &QPushButton::clicked, this, [=]() {
        QtLab1Window* lab1Window = new QtLab1Window();
        lab1Window->setAttribute(Qt::WA_DeleteOnClose); // ������� ��� ��������

        this->hide();  // �������� ������� ����
        lab1Window->show();

        // ����� ���� ��������� ? ������� ������� ����
        connect(lab1Window, &QObject::destroyed, this, [=]() {
            this->show();
            });
        });
}

HubWindow::~HubWindow() {}
