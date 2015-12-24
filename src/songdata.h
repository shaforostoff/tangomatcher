#ifndef SONGDATA_H
#define SONGDATA_H

#include <QStringList>

struct SongData
{
    QString name;
    QString year;
    QString vocal;
    QString genre;
    QString duration;
    QString composer;
    QString author;
    QString label;

    int popularity;

    //bool used;
    QStringList matchedFileNames;
    QStringList fuzzyMatchedFileNames;
    QString originalFileNames;
    QString fileDurations;
    QString fileComposers;
    QString bpm;

    SongData():popularity(0){}

    bool operator==(const SongData& other) const
    {return name==other.name && year==other.year && vocal==other.vocal && duration==other.duration;}
    bool operator !=(const SongData& other) const
    {return !(operator==(other));}
};

typedef QVector<SongData> SongDataVector;

#endif
