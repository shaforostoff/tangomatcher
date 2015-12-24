#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QModelIndex>
#include <QUrl>

#include <songdata.h>

class Task;
class QProgressBar;

class QSortFilterProxyModel;
class SongDataModel;
namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void dragEnterEvent(QDragEnterEvent* );
    void dropEvent(QDropEvent* );
    
    void setDir(QString dirPath, QString xmlFileName=QString());

public slots:
    void writeTags();
    void rename();
    void copyShort();
    void copyName();
    void refresh();
    void readDurations();
    void playFiles(QModelIndex);
    void handleFileLink(const QUrl&);

    bool editTagScheme();
    void doBeatCount();
    void saveBpmTable();

    void applyEnhancedSongData(SongDataVector,int);

private:
    void prepareAndStartTask(Task*, bool selectedOnly=true);

private:
    Ui::MainWindow *ui;
    SongDataModel* model;
    QSortFilterProxyModel* sortModel;
    QProgressBar* progressBar;

    QString dirPath;
    QString xmlFileName;

    QVector<SongData> songData;

    int refreshId;
};


class SongDataModel: public QAbstractListModel
{
    Q_OBJECT
public:
    enum Columns
    {
        DiscoItem=0,
        Files,
        Duration,
        FileDurations,
        Status,
        Popularity,
        Date,
        Bpm,
        OriginalFileNames,
        ColumnCount
    };
    
    SongDataModel(QObject* parent):QAbstractListModel(parent){}
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
    int rowCount(const QModelIndex& parent = QModelIndex()) const {return songData.size();}
    int columnCount(const QModelIndex& parent) const {return ColumnCount;}
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
public slots:
    void setSongData(const SongDataVector& sd, QString o=QString());
private:
    QVector<SongData> songData;
public:
    QString orchestra;
};

#endif // MAINWINDOW_H
