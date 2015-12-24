#include <QApplication>
#include <QDebug>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationDomain("program.net.ua");
    a.setOrganizationName("TangoMatcher");
    a.setApplicationName("TangoMatcher");
    MainWindow w;
    w.show();
    
    if (a.arguments().count()>1)
        w.setDir(a.arguments().last());
    
    qRegisterMetaType<SongDataVector>("SongDataVector");
    
    // return 0;
    return a.exec();
}
