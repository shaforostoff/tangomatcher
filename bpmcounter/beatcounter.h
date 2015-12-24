#ifndef BEATCOUNTER_H
#define BEATCOUNTER_H

#include <QMainWindow>
#include <QModelIndex>
#include <QUrl>
#include <QTime>

#include <songdata.h>

class QStringListModel;
class SortFilterProxyModel;
class QFileSystemModel;
class Task;
class QProgressBar;

class QSortFilterProxyModel;
namespace Ui {
class BeatCounter;
}

class BeatCounter: public QWidget
{
    Q_OBJECT
    
public:
    explicit BeatCounter(QWidget *parent = 0);
    ~BeatCounter();

public slots:
    void writeBpm();
    void playFile(QModelIndex);
    void countBeat();
    void resetBeatCount();
    void addAudioFiles(QStringList files, bool append=true);

protected:
    void dragEnterEvent(QDragEnterEvent* );
    void dragMoveEvent(QDragMoveEvent* );
    void dropEvent(QDropEvent* );
    
private:
    Ui::BeatCounter *ui;
    
    int beatCount;

    QTime time;
    long msecs2;
    int count;
    double allBpm;
    double avgBpm;
    double bpm;
    
    QSortFilterProxyModel* sortModel;
    QProgressBar* progressBar;
    
    QStringListModel* matchedFilesModel;
    SortFilterProxyModel* p;
};

QString bpmFromFile(QString fn);

#endif // BEATCOUNTER_H
