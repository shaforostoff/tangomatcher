#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "utils.h"

#include <QtCore>
#include <QtGui>
#include <QtXml>
#include <QtGlobal>

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


template<class T>
int levenshtein_distance(const T &s1, const T & s2)
{
    const int len1 = s1.size(), len2 = s2.size();
    QVector<int> col(len2+1), prevCol(len2+1);

    for (int i = 0; i < prevCol.size(); i++)
        prevCol[i] = i;
    for (int i = 0; i < len1; i++)
    {
        col[0] = i+1;
        for (int j = 0; j < len2; j++)
            col[j+1] = qMin( qMin( 1 + col[j], 1 + prevCol[1 + j]),
                prevCol[j] + (s1[i]==s2[j] ? 0 : 1) );
            col.swap(prevCol);
    }
    return prevCol[len2];
}


bool nameLengthLessThan(const SongData& s1, const SongData& s2)
{
    if (s1.name.length()==s2.name.length())
        return s1.vocal.length()>s2.vocal.length(); //charlo or charlo con acomp.
    return s1.name.length()>s2.name.length();
}

void loadLyrics(QString filename, SongLyrDataVector& songData)
{
    QTime a; a.start();
    QFile xmldata(filename/*+".xml"*/);
    xmldata.open(QFile::ReadOnly);

    QDomDocument doc("mydocument");
    if (!doc.setContent(&xmldata)) {
        return;
    }
    xmldata.close();

/*
    if (doc.elementsByTagName("lyrics").count())
    {
        QDomElement d=doc.elementsByTagName("lyrics").at(0).toElement();
    }
*/
    SongLyrData s;
    QDomNodeList trs=doc.elementsByTagName("translation");
    for (int i=0;i<trs.count();i++)
    {
        QDomElement translation=trs.at(i).toElement();
        s.name=translation.attribute("name");
        s.language=translation.attribute("lang");
        s.source=translation.attribute("source");
        s.author=translation.attribute("author");
        s.contents=translation.text();

        songData.append(s);
    }
    //qSort<>(songData.begin(), songData.end(), nameLengthLessThan);
}



MainWindow::MainWindow(QWidget *parent)
 : QMainWindow(parent)
 , ui(new Ui::MainWindow)
 , refreshId(0)
 , addminuses(false)
 , overwrite(false)
{
    ui->setupUi(this);
    ui->label->hide();
    ui->filterEdit->hide();

    setAcceptDrops(true);

    model=new QFileSystemModel(this);
    model->setRootPath(QDir::currentPath());
    model->setNameFilters(QStringList("*.xml"));
    model->setNameFilterDisables(false);
    model->setFilter(QDir::Files);

    sortModel=new QSortFilterProxyModel(this);
    sortModel->setSourceModel(model);
    sortModel->setDynamicSortFilter(true);
    sortModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    sortModel->setFilterKeyColumn(-1);
    sortModel->sort(0, Qt::AscendingOrder);
    ui->treeView->setModel(sortModel);
    
    ui->treeView->setRootIndex(sortModel->mapFromSource(model->index(QDir::currentPath())));
#if defined(Q_OS_WIN32) || defined(Q_OS_MAC)
    connect(ui->treeView, SIGNAL(clicked(QModelIndex)), this, SLOT(displayLyrics(QModelIndex)));
#endif
    connect(ui->treeView, SIGNAL(activated(QModelIndex)), this, SLOT(displayLyrics(QModelIndex)));

    int w=QFontMetrics(QFont()).averageCharWidth();
    ui->treeView->setColumnWidth(0, w*20);

    matchedFilesModel=new QStringListModel(this);
    ui->matchedFilesView->setModel(matchedFilesModel);
    connect(ui->matchedFilesView, SIGNAL(activated(QModelIndex)), this, SLOT(playFile(QModelIndex)));

    connect(ui->write, SIGNAL(clicked(bool)), this, SLOT(writeLyrics()));
    
    QSettings s;
    ui->treeView->header()->restoreState(s.value("columnsState").toByteArray());
    ui->folderToMatch->setText(s.value("folderToMatch").toString());
    overwrite = s.value("overwrite", overwrite).toBool();
    addminuses = s.value("addMinuses", addminuses).toBool();
    if (s.contains("geometry")) setGeometry(s.value("geometry", geometry()).toRect());

    connect(ui->filterEdit, SIGNAL(textChanged(QString)), this, SLOT(setFilter(QString)));
    /*
    connect(ui->resultFilter, SIGNAL(textChanged(QString)), sortModel, SLOT(setFilterFixedString(QString)));
    connect(ui->fuzzyMatching, SIGNAL(clicked(bool)), this, SLOT(refresh()));

    ui->treeView->setColumnWidth(LyrDataModel::FileDurations, w*5);
    ui->treeView->setColumnWidth(LyrDataModel::Popularity, w*3);
    ui->treeView->setColumnWidth(LyrDataModel::Date, w*10);
    ui->treeView->setColumnWidth(LyrDataModel::Bpm, w*6);

*/
    ui->treeView->setContextMenuPolicy(Qt::ActionsContextMenu);
    QAction* a = new QAction( tr("Generate xhtml"), this );
    connect(a, SIGNAL(triggered(bool)), this, SLOT(makeXhtml2()));
    ui->treeView->addAction(a);

    a = new QAction( tr("Find next having matches"), this );
    a->setShortcut(QKeySequence::FindNext);
    connect(a, SIGNAL(triggered(bool)), this, SLOT(findNextWithMatches()));
    ui->treeView->addAction(a);

    a = new QAction( tr("Write lyrics"), this );
    a->setShortcut(Qt::CTRL + Qt::Key_Return);
    connect(a, SIGNAL(triggered(bool)), this, SLOT(writeLyrics()));
    connect(a, SIGNAL(triggered(bool)), this, SLOT(findNextWithMatches()), Qt::QueuedConnection);
    ui->treeView->addAction(a);

    a = new QAction( tr("Write ALL lyrics"), this );
    connect(a, SIGNAL(triggered(bool)), this, SLOT(writeAllLyrics()));
    ui->treeView->addAction(a);

    a = new QAction( tr("Add minuses"), this );
    a->setCheckable(true);
    a->setChecked(addminuses);
    connect(a, SIGNAL(triggered(bool)), this, SLOT(setAddMinuses(bool)));
    ui->treeView->addAction(a);

    a = new QAction( tr("Overwrite"), this );
    a->setCheckable(true);
    a->setChecked(overwrite);
    connect(a, SIGNAL(triggered(bool)), this, SLOT(setOverwriteMode(bool)));
    ui->treeView->addAction(a);

/*
    a= new QAction( tr("Write standard tags for selected files"), this );
    connect(a, SIGNAL(triggered(bool)), this, SLOT(writeTags()));
    ui->treeView->addAction(a);

    a= new QAction( tr("Rename file(s) in standard way"), this );
    connect(a, SIGNAL(triggered(bool)), this, SLOT(rename()));
    ui->treeView->addAction(a);

    a= new QAction( tr("Copy files to device with short tags"), this );
    connect(a, SIGNAL(triggered(bool)), this, SLOT(copyShort()));
    ui->treeView->addAction(a);

    a= new QAction( tr("Refresh"), this );
    a->setShortcut(QKeySequence::Refresh);
    connect(a, SIGNAL(triggered(bool)), this, SLOT(refresh()));
    ui->treeView->addAction(a);
*/
    QShortcut* sc=new QShortcut(Qt::CTRL+Qt::Key_L, ui->filterEdit, SLOT(setFocus()), 0, Qt::WindowShortcut);
    connect(sc, SIGNAL(activated()), ui->filterEdit, SLOT(selectAll()));
/*
    connect(ui->results, SIGNAL(anchorClicked(QUrl)), this, SLOT(handleFileLink(QUrl)));

    connect(ui->openSettings, SIGNAL(clicked(bool)), this, SLOT(editTagScheme()));
    
    
    progressBar=new QProgressBar(statusBar());
    progressBar->setRange(0, 100);
    statusBar()->addPermanentWidget(progressBar);
    progressBar->hide();
    */
}

MainWindow::~MainWindow()
{
    QSettings s;
    s.setValue("columnsState", ui->treeView->header()->saveState());
    s.setValue("folderToMatch", ui->folderToMatch->text());
    s.setValue("overwrite", overwrite);
    s.setValue("addMinuses", addminuses);
    s.setValue("geometry", geometry());
    delete ui;
}

QStringList findFile(QString path, QStringList glob)
{
    QStringList result;
    QDir dir(path);
    dir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);

    QFileInfoList list = dir.entryInfoList();
    for (int i = 0; i < list.size(); ++i)
    {
        QFileInfo fileInfo = list.at(i);
        result += findFile(fileInfo.filePath(), glob);
    }
    
    dir.setFilter(QDir::Files);
    dir.setNameFilters(glob);

    list = dir.entryInfoList();
    for (int i = 0; i < list.size(); ++i)
    {
        QFileInfo fileInfo = list.at(i);
        result += fileInfo.filePath();
    }
    
    return result;
}

void MainWindow::displayLyrics(QModelIndex item)
{
    //code duplication with writeLyrics()

    SongLyrDataVector songData;
    loadLyrics(item.data(QFileSystemModel::FilePathRole).toString(), songData);
    QString combined;
    for (int i = 0; i<songData.count(); ++i)
        combined += "[" % songData.at(i).language % "] " % songData.at(i).name % "\n" % songData.at(i).contents % "\n\n";
    ui->results->setPlainText(combined);

    ui->matchedFilesView->setDisabled(true);
    QApplication::processEvents();
    QString fp=ui->folderToMatch->text(); //fromnative?
    fp.replace('\\', '/');
    QString fn=model->fileInfo(sortModel->mapToSource(item)).fileName();
    fn.chop(4); //.xml and nothing more
    static QString MINUS_SPACE("- ");
    static QString SPACE_MINUS(" -");
    static QString STAR("*");
    if (addminuses && !fn.startsWith(MINUS_SPACE)) fn.prepend(MINUS_SPACE);
    if (addminuses && !fn.endsWith(SPACE_MINUS))   fn.append(SPACE_MINUS); //TODO (
    QStringList r=findFile(fp, QStringList(STAR % fn % STAR));
    if (r.count() && fn.endsWith(SPACE_MINUS)) fn.chop(1), r+=findFile(fp, QStringList(STAR % fn % "(*"));
    if (!fp.endsWith('/')) fp.append('/');
    r.replaceInStrings(fp, QString());
    if (ui->resultFilter->text().length()) r = r.filter(ui->resultFilter->text(),  Qt::CaseInsensitive);
    matchedFilesModel->setStringList(r);
    ui->matchedFilesView->setDisabled(false);

    statusBar()->clearMessage();
}

void MainWindow::writeLyrics()
{
    //code duplication with displayLyrics()

    QModelIndex item=ui->treeView->currentIndex();
    if (!item.isValid()) return;

    SongLyrDataVector songData;
    loadLyrics(item.data(QFileSystemModel::FilePathRole).toString(), songData);
    //ui->results->setPlainText(songData.first().contents.trimmed());
    QString fn=model->fileInfo(sortModel->mapToSource(item)).fileName();
    fn.chop(4); //.xml and nothing more
    if (addminuses && !fn.startsWith("- ")) fn.prepend("- ");
    if (addminuses && !fn.endsWith(" -"))   fn.append(" -"); //TODO (

    QStringList s=findFile(ui->folderToMatch->text(), QStringList("*" % fn % "*"));
    if (s.count() && fn.endsWith(" -")) fn.chop(1), s+=findFile(ui->folderToMatch->text(), QStringList("*" % fn % "(*"));
    qDebug()<<s;
    if (ui->resultFilter->text().length()) s = s.filter(ui->resultFilter->text(),  Qt::CaseInsensitive);

    for (int fileI = 0; fileI < s.size(); ++fileI)
    {
        TagLib::FileRef f(CONVFN(QString(s.at(fileI))));
        if (!f.file())
            continue;
        
        //if (!f.tag())
        if (TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file()) )
            mpf->ID3v2Tag(true);
        if (!f.tag())
            continue;


        bool doSave = false;
        TagLib::FLAC::File* flacf=dynamic_cast<TagLib::FLAC::File*>(f.file());
        TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file());

        if (mpf || flacf)
        {
            TagLib::ID3v2::Tag* tag2=0;
            if (mpf) tag2=mpf->ID3v2Tag(true);
            if (flacf) tag2=flacf->ID3v2Tag(); //taglib may not be able to create it properly
            if (tag2)
            {
                doSave = true;
                TagLib::ID3v2::FrameListMap map=tag2->frameListMap();
                    /*
                    for (TagLib::ID3v2::FrameListMap::ConstIterator it=map.begin();it!=map.end();++it)
                    {
                        qDebug()<<QByteArray((*it).first.data(), 4);
                        TagLib::ID3v2::FrameList list=(*it).second;
                        for (int i=0;i<list.size();++i)
                            qDebug()<<t2q(list[i]->toString());
                        
                    }
                    */
                //for preexisting bad tags
                if (map.contains("USLT") && map["USLT"].size())
                {
                    if (!overwrite) return;
                    tag2->removeFrames("USLT");
                    //if (TagLib::ID3v2::UnsynchronizedLyricsFrame* f=dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame*>(tag2->frameList("USLT")[0]))
                    //    f->setTextEncoding(TagLib::String::UTF8);
                   
                }

                for (int transI=0;transI<songData.count();transI++)
                {
                    const SongLyrData& ld=songData.at(transI);
                    tag2->addFrame(new TagLib::ID3v2::UnsynchronizedLyricsFrame(TagLib::String::UTF8));
                    if (TagLib::ID3v2::UnsynchronizedLyricsFrame* f=dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame*>(tag2->frameList("USLT").back()))
                    {
                        //f->setTextEncoding(TagLib::String::UTF8);
                        f->setLanguage(ld.language.toLatin1().constData());
                        QString c=ld.name % "\n\n" % ld.contents.trimmed() % "\n\n";
                        c.replace('\n', "\r\n"); //for win players
                        f->setText(q2t(c));
                        f->setDescription(q2t(ld.name));
                    }
                }
            }
        }

        if (flacf)
        {
            doSave = true;
            flacf->xiphComment(true);
            QString l;
            for (int transI=0;transI<songData.count();transI++)
            {
                const SongLyrData& ld=songData.at(transI);
                l+=ld.name.trimmed()%"\n\n"%ld.contents.trimmed()%"\n\n\n\n";
            }
            l.replace('\n', "\r\n"); //for win players

            //qDebug()<<"old"<<t2q(flacf->xiphComment()->fieldListMap()["LYRICS"]);
            if (flacf->xiphComment()->contains("LYRICS") && t2q(flacf->xiphComment()->fieldListMap()["LYRICS"][0]) == l)
                continue;
            if (!overwrite && flacf->xiphComment()->contains("LYRICS")) continue;
            flacf->xiphComment()->removeField("LYRICS");
            flacf->xiphComment()->addField("LYRICS", q2t(l));
        }

        if (TagLib::MP4::File* mp4f=dynamic_cast<TagLib::MP4::File*>(f.file()))
        {
            doSave = true;
            TagLib::MP4::Tag* tag=mp4f->tag();
            if (tag)
            {
                TagLib::MP4::ItemListMap& map=tag->itemListMap();

                /*
                for (TagLib::MP4::ItemListMap::ConstIterator it=map.begin();it!=map.end();++it)
                {
                    qDebug()<<t2q((*it).first);
                    TagLib::StringList list=(*it).second.toStringList();
                    for (int i=0;i<list.size();++i)
                        qDebug()<<t2q(list[i]);
                    
                }
                */
                if (map.contains("\251lyr") && !overwrite) continue;

                QString l;
                for (int transI=0;transI<songData.count();transI++)
                {
                    const SongLyrData& ld=songData.at(transI);
                    l+=ld.name.trimmed()%"\n\n"%ld.contents.trimmed()%"\n\n\n\n";
                }
                l.replace('\n', "\r\n"); //for win players

                map["\251lyr"]=TagLib::MP4::Item(TagLib::StringList(q2t(l)));
                //fucks up tag in m4a file on windows
                //if (s.author.length())
                //    map["----:com.apple.iTunes:LYRICIST"]=TagLib::MP4::Item(TagLib::StringList(q2t(s.author)));
                //if (oldAlbum.length())
                //    map["----:com.apple.iTunes:ORIGINAL ALBUM"]=TagLib::MP4::Item(TagLib::StringList(q2t(oldAlbum)));

                tag->save();
                QDesktopServices::openUrl(QUrl::fromLocalFile(s.at(fileI)));
            }
        }

        //file->tag()->itemListMap()["----:com.apple.iTunes:foo"] = TagLib::StringList("Some custom tag");

        //QStringList failedFiles;
        if (doSave && !f.save())
            qDebug()<<s.at(fileI);
            //failedFiles.append(s.at(fileI));
    }
    statusBar()->showMessage(tr("Tag writing done."), 5000);
}

void MainWindow::playFile(QModelIndex item)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(ui->folderToMatch->text()%"/"%item.data().toString()));
}

void MainWindow::makeXhtml()
{
    QModelIndexList rows=ui->treeView->selectionModel()->selectedRows();
    if (rows.count()<1) return;
    QFile file("out.txt");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out.setCodec(QTextCodec::codecForMib(106));
    QSet<QString> spanishNames;

    foreach(QModelIndex index, rows)
    {
        QString fn = index.sibling(index.row(), 0).data(QFileSystemModel::FilePathRole).toString();
        SongLyrDataVector songData;
        loadLyrics(fn, songData);

        for (int transI=0;transI<songData.count();transI++)
        {
            const SongLyrData& ld=songData.at(transI);
            if (transI==0 && spanishNames.contains(ld.name))
                break;
            else if (transI==0)
                spanishNames.insert(ld.name);

            if (transI)
                out << "<h4>" << ld.name << "</h4>\n\n";
            else
                out << "<h3>" << ld.name << "</h3>\n\n";
            QString c = ld.contents.trimmed();
            c.replace("\n\n", "</p><p>"); //for win players
            c.replace('\n', "<br/>\n"); //for win players
            c.replace("</p><p>", "</p>\n<p>"); //for win players
            out << "<p>" << c << "</p>\n\n";
        }
    }

}


void MainWindow::makeXhtml2()
{
    QFile file("out.txt");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out.setCodec(QTextCodec::codecForMib(106));
    QSet<QString> spanishNames;
    
    while(findNextWithMatches())
    {
		QModelIndex item=ui->treeView->currentIndex();
		if (!item.isValid()) return;

		SongLyrDataVector songData;
		loadLyrics(item.data(QFileSystemModel::FilePathRole).toString(), songData);

        for (int transI=0;transI<songData.count();transI++)
        {
            const SongLyrData& ld=songData.at(transI);
            if (transI==0 && spanishNames.contains(ld.name))
                break;
            else if (transI==0)
                spanishNames.insert(ld.name);

            if (transI)
                out << "<h4>" << ld.name << "</h4>\n\n";
            else
                out << "<h3>" << ld.name << "</h3>\n\n";
            QString c = ld.contents.trimmed();
            c.replace("\n\n", "</p><p>"); //for win players
            c.replace('\n', "<br/>\n"); //for win players
            c.replace("</p><p>", "</p>\n<p>"); //for win players
            out << "<p>" << c << "</p>\n\n";
        }
    }

}



bool MainWindow::findNextWithMatches()
{
    QModelIndex item=ui->treeView->currentIndex();
    item=item.sibling(item.row()+1, 0);
    bool old = addminuses;
    addminuses = true;
    while (item.isValid())
    {
        ui->treeView->setCurrentIndex(item);
        displayLyrics(item);
        if (matchedFilesModel->rowCount())
        {
            addminuses = old;
            displayLyrics(item);
            return true;
        }
        item=item.sibling(item.row()+1, 0);
    }
    addminuses = old;
    return false;
}

void MainWindow::writeAllLyrics()
{
    while(findNextWithMatches())
    {
        writeLyrics();
    }
}














