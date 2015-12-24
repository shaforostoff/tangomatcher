#include "beatcounter.h"
#include "ui_beatcounter.h"
#include "utils.h"

#include <QtCore>
#include <QtXml>
#include <QtGlobal>
#include <QAction>
#include <QShortcut>
#include <QDropEvent>
#include <QDesktopServices>
#include <QStringListModel>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#ifdef Q_OS_WIN32
#include <taglib/mpeg/mpegfile.h>
#include <taglib/mpeg/id3v1/id3v1tag.h>
#include <taglib/mpeg/id3v2/id3v2tag.h>
#include <taglib/mpeg/id3v2/frames/textidentificationframe.h>
#include <taglib/mpeg/id3v2/frames/unsynchronizedlyricsframe.h>
#include <taglib/mp4/mp4file.h>
#include <taglib/flac/flacfile.h>
#include <taglib/ogg/xiphcomment.h>
#define lround(x) (int)floor(x+0.5)
#define round(x) floor(x+0.5)
#else
#include <taglib/mpegfile.h>
#include <taglib/id3v1tag.h>
#include <taglib/id3v2tag.h>
#include <taglib/textidentificationframe.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/mp4file.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>
#endif

#ifdef Q_OS_WIN32
#define CONVFN(qstring) (const wchar_t*)qstring.unicode()
#else
#define CONVFN(qstring) qstring.toUtf8().constData()
#endif



BeatCounter::BeatCounter(QWidget *parent)
 : QWidget(parent)
 , ui(new Ui::BeatCounter)
{
    ui->setupUi(this);

    connect(ui->bpmTyper, SIGNAL(textEdited(QString)), this, SLOT(countBeat()));

    setAcceptDrops(true);


    matchedFilesModel=new QStringListModel(this);
    ui->filesToWrite->setModel(matchedFilesModel);
    connect(ui->filesToWrite, SIGNAL(activated(QModelIndex)), this, SLOT(playFile(QModelIndex)));

    connect(ui->write, SIGNAL(clicked(bool)), this, SLOT(writeBpm()));

    //if (testAttribute(Qt::WA_DeleteOnClose))
    if (qApp->activeWindow()) //running standalone or not
        new QShortcut(Qt::Key_Escape, this, SLOT(deleteLater()));
    else
        new QShortcut(Qt::Key_Escape, this, SLOT(resetBeatCount()));

    resetBeatCount();
}

BeatCounter::~BeatCounter()
{
    QSettings s;
//    s.setValue("columnsState", ui->treeView->header()->saveState());
//    s.setValue("folderToMatch", ui->folderToMatch->text());
    delete ui;
}

void BeatCounter::resetBeatCount()
{
    msecs2 = 0;
    count = 0;
    allBpm = 0;
    avgBpm = 0;
    bpm = 0;

    ui->results->clear();
    ui->bpmTyper->clear();
}

void BeatCounter::countBeat()
{
    long msecs=time.elapsed();
    if (count == 0)
    {
        time.start();
        ui->results->setText("<center><h1>1st tap</h1></center>");
        msecs2 = 0;
        count++;
        return;
    }
    double oldBpm = bpm;
    bpm = (1.0 / ((msecs - msecs2) / 1000.0)) * 60.0;
    if (bpm > 220)
        bpm = 220;
    double bpmChg = (round((bpm - oldBpm) * 10)) / 10.0;
    count++;
    allBpm = allBpm + bpm;
    double oldAvg = avgBpm;
    avgBpm = allBpm / (count - 1);
    double avgChg = (round((avgBpm - oldAvg) * 10)) / 10;
    msecs2 = msecs;
    //if (bpmChg >= 0) { PbpmChg = "+" + bpmChg } else { PbpmChg = bpmChg }
    //if (avgChg >= 0) { PavgChg = "+" + avgChg } else { PavgChg = avgChg }
    QString tAvg = QString("%1").arg((round(avgBpm * 10)) / 10, 0, 'f', 1);
    QString tLast = QString("%1").arg((round(bpm * 10)) / 10, 0, 'f', 1);

    double devBpm = (bpm>avgBpm)?(bpm/avgBpm):(avgBpm/bpm);

    if (devBpm>2)
    {
        resetBeatCount();
        ui->results->setText("<center><h1>-</h1></center>");
    }
    else if (devBpm>1.5)
    {
        QPalette p=ui->results->palette();
        p.setColor(QPalette::Window, QColor(0xEE, 0xCC, 0xCC));
        ui->results->setPalette(p);
    }
    else
    {
        QPalette p=ui->label->palette();
        ui->results->setPalette(p);

        QString hit="#" + QString::number(count);
        ui->results->setText(QString(
        "<center><h1>%1 bpm</h1>"
        "<h3><table><tr><td>Average:</td><td>%2</td></tr>" //<br>
        "<tr><td>Last tap:</td><td>%3</td></tr>"
        "<tr><td>Taps:</td><td>%4</td></tr></table></h3></center>").arg(lround(avgBpm)).arg(tAvg).arg(tLast).arg(hit));

        ui->bpmToWrite->setText(QString::number(lround(avgBpm)));
//            document.BEATSPERMINUTE.PROGRESS.style.width = 2*Math.round(avgBpm) + "px";
//            document.BEATSPERMINUTE.PROGRESS.value = Math.round(avgBpm) + " bpm";
//            document.BEATSPERMINUTE.PROGRESS.style.backgroundColor = "#99FF99";
    }

#if 0
    static int a=0;
    a++;
    QTime now=QTime::currentTime();
    //if (abs(lastBeat.secsTo(lastBeat))>5)
    if (a==1)
    {
        firstBeat=now;
        beatCount=0;
        ui->results->setPlainText(QString());
    }
    else
    {
        beatCount++;
        int secs=abs(firstBeat.secsTo(now));
        if (secs)
        {
            QString s("%1");
            s=s.arg(60.0*beatCount/(double)secs, 0, 'f', 1);
            ui->results->setPlainText(s);
        }
    }
    lastBeat=now;
#endif
}

void BeatCounter::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        event->accept();
    }
}

void BeatCounter::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        event->accept();
    }
}

QString bpmFromFile(QString fn)
{
    TagLib::FileRef f(CONVFN(fn));
    if (!f.file())
        return QString();

        //if (!f.tag())
    if (TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file()) )
        mpf->ID3v2Tag(true);
    if (!f.tag())
        return QString();

    TagLib::FLAC::File* flacf=dynamic_cast<TagLib::FLAC::File*>(f.file());
    TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file());

    if (flacf && flacf->xiphComment())
    {
        if (flacf->xiphComment()->contains("BPM"))
        return t2q(flacf->xiphComment()->fieldListMap()["BPM"].toString());
    }

    if (mpf || flacf)
    {
        TagLib::ID3v2::Tag* tag2=0;
        if (mpf) tag2=mpf->ID3v2Tag(true);
        if (flacf) tag2=flacf->ID3v2Tag(); //taglib may not be able to create it properly
        if (tag2)
        {
            TagLib::ID3v2::FrameListMap map=tag2->frameListMap();
            if (map.contains("TBPM") && map["TBPM"].size())
                return t2q(tag2->frameList("TBPM")[0]->toString())+'\n';
        }
    }

    if (TagLib::MP4::File* mp4f=dynamic_cast<TagLib::MP4::File*>(f.file()))
    {
        TagLib::MP4::Tag* tag=mp4f->tag();
        if (tag)
        {
            TagLib::MP4::ItemListMap& map=tag->itemListMap();

            if (map.contains("tmpo"))
                return QString::number(map["tmpo"].toInt());
        }
    }

    return QString();
}

void BeatCounter::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasUrls())
    {
        QStringList files;
        foreach(const QUrl url, event->mimeData()->urls())
            files.append(url.toLocalFile());
        addAudioFiles(files, event->keyboardModifiers()&Qt::CTRL);
        event->acceptProposedAction();
        event->accept();
    }
}
void BeatCounter::addAudioFiles(QStringList uncheckedFiles, bool append)
{
    QStringList files;
    if (append)
        files=matchedFilesModel->stringList();

    foreach(const QString& fn, uncheckedFiles)
    {
        if (QFile::exists(fn))
        {
            TagLib::FileRef f(CONVFN(fn));
            if (f.file())
                files.append(fn);
        }
        if (files.count()==1)
        ui->bpmToWrite->setText(bpmFromFile(fn));
    }
    matchedFilesModel->setStringList(files);
}

void saveBpmTable(QStringList filenames, QString bpm);

void BeatCounter::writeBpm()
{
    foreach (const QString& fn, matchedFilesModel->stringList())
    {
        TagLib::FileRef f(CONVFN(fn));
        if (!f.file())
            continue;

        //if (!f.tag())
        if (TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file()) )
            mpf->ID3v2Tag(true);
        if (!f.tag())
            continue;

        bool save=false;
        QString bpmQText=ui->bpmToWrite->text();
        TagLib::String bpmText=q2t(bpmQText);

        TagLib::FLAC::File* flacf=dynamic_cast<TagLib::FLAC::File*>(f.file());
        TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file());

        if (mpf || flacf)
        {
            TagLib::ID3v2::Tag* tag2=0;
            if (mpf) tag2=mpf->ID3v2Tag(true);
            if (flacf) tag2=flacf->ID3v2Tag(); //taglib may not be able to create it properly
            if (tag2)
            {
                TagLib::ID3v2::FrameListMap map=tag2->frameListMap();
                if (!map.contains("TBPM"))
                    tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TBPM", TagLib::String::UTF8));
                //else if (TagLib::ID3v2::TextIdentificationFrame* f=dynamic_cast<TagLib::ID3v2::TextIdentificationFrame*>(tag2->frameList("TCOM")[0]))
                //    f->setTextEncoding(TagLib::String::UTF8);
                save=save||t2q(tag2->frameList("TBPM")[0]->toString())!=bpmQText;
                tag2->frameList("TBPM")[0]->setText(bpmText);
            }
        }

        if (flacf)
        {
            flacf->xiphComment(true);
            save=save||t2q(flacf->xiphComment()->fieldListMap()["BPM"].toString())!=bpmQText;
            flacf->xiphComment()->addField("BPM", bpmText);
        }

        bool ok;
        int bpmNum=bpmQText.toInt(&ok);
        qDebug()<<bpmNum;
        if (TagLib::MP4::File* mp4f=dynamic_cast<TagLib::MP4::File*>(f.file()))
        {
            TagLib::MP4::Tag* tag=mp4f->tag();
            if (tag && ok)
            {
                TagLib::MP4::ItemListMap& map=tag->itemListMap();
                save=save||map["tmpo"].toInt()!=bpmNum;
                map["tmpo"]=TagLib::MP4::Item(bpmNum);

                if (save)
                    tag->save();
            }
        }

        if (save)
            f.save();
    }
    //saveBpmTable(matchedFilesModel->stringList(), ui->bpmToWrite->text());

    //statusBar()->showMessage(tr("Tag writing done."), 5000);
}

void BeatCounter::playFile(QModelIndex item)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(item.data().toString()));
}
