#include "HubWindow.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    HubWindow window;
    window.show();
    return app.exec();
}
