#include <QtWidgets/QApplication>
#include <QtCore/QDebug>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationDomain("program.net.ua");
    a.setOrganizationName("LyricsMatcher");
    a.setApplicationName("LyricsMatcher");
    MainWindow w;
    w.show();
    
//     if (a.arguments().count()>1)
//         w.setDir(a.arguments().at(1));
        
    // return 0;
    return a.exec();
}
