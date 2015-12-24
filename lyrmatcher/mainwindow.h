#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QModelIndex>
#include <QUrl>

#include <songdata.h>

class QProgressBar;
class QStringListModel;
class QFileSystemModel;
class QSortFilterProxyModel;
class SortFilterProxyModel;
class Task;
class LyrDataModel;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    
public slots:
    void displayLyrics(QModelIndex);
    void writeLyrics();
    void playFile(QModelIndex);
    void makeXhtml();
    void makeXhtml2();
    bool findNextWithMatches();
    void writeAllLyrics();
    
    void setAddMinuses(bool v){addminuses = v;}
    void setOverwriteMode(bool v){overwrite = v;}

private:
    Ui::MainWindow *ui;
    QFileSystemModel* model;
    QSortFilterProxyModel* sortModel;
    QProgressBar* progressBar;
    
    QStringListModel* matchedFilesModel;
    SortFilterProxyModel* p;

    QString dirPath;
    QString xmlFileName;

    QVector<SongData> songData;

    int refreshId;


    bool addminuses;
    bool overwrite;
};



#endif // MAINWINDOW_H
