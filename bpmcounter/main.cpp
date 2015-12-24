
#include <QtGui/QApplication>
#include <QtCore/QDebug>
#include "beatcounter.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationDomain("program.net.ua");
    a.setOrganizationName("BeatCounter");
    a.setApplicationName("BeatCounter");
    BeatCounter w;
    w.show();

    if (a.arguments().count()>1)
    {
        QStringList args=a.arguments();
        args.removeFirst();
        w.addAudioFiles(args);
    }

    return a.exec();
}
