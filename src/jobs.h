#ifndef JOBS_H
#define JOBS_H

#include <QRunnable>
#include <QVector>
#include <QObject>
#include <QMap>

#include <songdata.h>

class Task: public QObject, public QRunnable
{
    Q_OBJECT
public:
    QVector<SongData> songData;
    QString orchestra;

    QString dirPath;
    
    QStringList failedFiles;

    int refreshId;

signals:
    void progress(int percent);
    void finished();
    void started();
};


class MatchFilesTask: public Task
{
public:
    void run();
    
    QString xmlfile;
};


class WriteTagsTask: public Task
{
public:
    void run();
};

class WriteOriginalFileNamesToTagsTask: public Task
{
public:
    void run();
    
    QMap<QString, QString> newToOldFilename;
};

class ReadTagsTask: public Task
{
    Q_OBJECT
public:
    void run();
signals:
    void done(const SongDataVector& sd, int refreshId);
};


class CopyShortTask: public Task
{
public:
    void run();

    QString copyTitle;
    QString copyArtist;
    QString copyAlbum;

    QString targetDirPath;
};




#endif
