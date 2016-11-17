#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_tagscheme.h"
#include "diff.h"
#include "jobs.h"
#include "utils.h"

#include "../bpmcounter/beatcounter.h"


#include <QtCore>
#include <QDomDocument>
#include <QtGlobal>
#include <QSortFilterProxyModel>
#include <QProgressBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QDesktopServices>
#include <QAction>
#include <QShortcut>
#include <QClipboard>
#include <QDropEvent>
#include <QDesktopWidget>


#include <taglib/fileref.h>
#include <taglib/tag.h>
#ifdef Q_OS_WIN32
#include <taglib/mpeg/mpegfile.h>
#include <taglib/mpeg/id3v1/id3v1tag.h>
#include <taglib/mpeg/id3v2/id3v2tag.h>
#include <taglib/mpeg/id3v2/frames/textidentificationframe.h>
#include <taglib/mpeg/id3v2/frames/popularimeterframe.h>
#include <taglib/mp4/mp4file.h>
#include <taglib/flac/flacfile.h>
#include <taglib/ogg/xiphcomment.h>
#else
#include <taglib/mpegfile.h>
#include <taglib/id3v1tag.h>
#include <taglib/id3v2tag.h>
#include <taglib/textidentificationframe.h>
#include <taglib/popularimeterframe.h>
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

void checkSongData(QVector<SongData> songData)
{
    QTime a; a.start();
    QVector<QString> simplified; simplified.reserve(songData.size());
    for(int songI=0; songI<songData.size();songI++)
    {
        const SongData& s=songData.at(songI);
        simplified.append(simplify(s.name));
    }

    for(int songI=0; songI<songData.size();songI++)
    {
        //const SongData& s1=songData.at(songI);
        const QString& name1=simplified.at(songI);
        for(int songJ=songI+1; songJ<songData.size();songJ++)
        {
            //const SongData& s2=songData.at(songJ);
            const QString& name2=simplified.at(songJ);
            if (name1.length()<=name2.length())
                continue;
            if (name1.contains(name2))
                qWarning()<<name1<<"contains"<<name2;
                //qWarning()<<s2.name<<"contains"<<s1.name;
        }
    }
    //qDebug()<<"checkSongData"<<a.elapsed();
}

void loadDiscoGraphyTable(QString filename, QVector<SongData>& songData, QString& orchestra, QString& source)
{
    QTime a; a.start();
    QFile xmldata(filename/*+".xml"*/);
    xmldata.open(QFile::ReadOnly);

    QDomDocument doc("mydocument");
    if (!doc.setContent(&xmldata)) {
        return;
    }
    xmldata.close();

    if (doc.elementsByTagName("discography").count())
    {
        QDomElement d=doc.elementsByTagName("discography").at(0).toElement();
        orchestra=d.attribute("orchestra");
        source=d.attribute("source");
    }

    SongData s;
    QDomNodeList trs=doc.elementsByTagName("track");
    for (int i=0;i<trs.count();i++)
    {
        QDomElement track=trs.at(i).toElement();
        s.name=track.attribute("name");
        s.vocal=track.attribute("vocal");
        s.genre=track.attribute("genre");
        s.year=track.attribute("year");
        s.composer=track.attribute("composer");
        s.author=track.attribute("author");
        s.label=track.attribute("label");
        s.duration=track.attribute("duration");
        s.popularity=track.attribute("popularity").toInt();

        songData.append(s);
    }
    qSort<>(songData.begin(), songData.end(), nameLengthLessThan);
}


void SongDataModel::setSongData(const SongDataVector& sd, QString o)
{
    beginResetModel();

    songData=sd;
    if (o.length())
        orchestra=o;

    endResetModel();
}

QVariant SongDataModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role!=Qt::DisplayRole)
        return QVariant();

    if (section==DiscoItem)
        return "Discography item";
    if (section==Files)
        return "Matched files";
    if (section==Duration)
        return "Duration";
    if (section==FileDurations)
        return "File durations";
    if (section==Status)
        return "Status";
    if (section==Popularity)
        return "Pop";
    if (section==Date)
        return "Date";
    if (section==Bpm)
        return "BPM";
    if (section==OriginalFileNames)
        return "Original names";
    return QVariant();
}

QVariant SongDataModel::data(const QModelIndex& item, int role) const
{
    const SongData& s=songData.at(item.row());
    if (role==Qt::BackgroundRole && s.fuzzyMatchedFileNames.count())
    {
        static QColor color(0xc3, 0x8e, 0xc7);
        return color;
    }
/*
    if (role==Qt::SizeHintRole && item.column()!=DiscoItem && item.column()!=Files)
    {
        static QColor color(0xc3, 0x8e, 0xc7);
        return color;
    }
*/

#if 0
    if (role==Qt::BackgroundRole && s.fileDurations.length() && s.duration.length())
    {
        QTime expectedTime=QTime::fromString(s.duration, "m:ss");
        QStringList durations=s.fileDurations.split('\n');
        for (int i=0;i<durations.count();++i)
        {
            QTime fileTime=QTime::fromString(durations.at(i), "m:ss");
            if (abs(fileTime.secsTo(expectedTime))<10)
                return QVariant();
        }
        static QColor color(0xc5, 0x90, 0xc9);
        return color;
    }
#endif

    if (role==Qt::DisplayRole || role==Qt::UserRole)
    {
        if (item.column()==DiscoItem)
        {
            QString year=s.year;
            if (year.isEmpty()) year="????";
            if (role == Qt::UserRole)
                year=year.left(4);

            QString genre=s.genre;
            if (genre.isEmpty()) genre="????";
            return QString(orchestra % " - " % s.vocal % " - " % s.name % " - " % year % " - " % genre);
        }
        if (item.column()==Files)
        {
            static QString nl="\n";
            if (s.matchedFileNames.count())
                return s.matchedFileNames.join(nl);
            return s.fuzzyMatchedFileNames.join(nl);
        }
        if (item.column()==Duration)
            return s.duration;
        if (item.column()==FileDurations)
            return s.fileDurations;
        if (item.column()==Status)
        {
            //TODO tag genres
            QString t;
            static QString NOGENREINFILENAME("NOGENREINFILENAME ");
            static QString FUZZY("FUZZY ");
            static QString NEEDSRENAME("REALLYNEEDSRENAME ");
            static QString NEEDSTAGWRITE("NEEDSTAGWRITE ");
            bool checkForRenaming=true;
            for (int i=0;i<s.matchedFileNames.count(); i++)
            {
                if (!s.matchedFileNames.at(i).contains(s.genre, Qt::CaseInsensitive))
                {
                    t+=NOGENREINFILENAME;
                    checkForRenaming=false;
                    break;
                }
            }
        
            for (int i=0;i<s.fuzzyMatchedFileNames.count(); i++)
            {
                if (!s.fuzzyMatchedFileNames.at(i).contains(s.genre, Qt::CaseInsensitive))
                {
                    t+=NOGENREINFILENAME;
                    break;
                }
            }
            if (s.fileDurations.length() && s.duration.length())
            {
                long maxDiff=0;
                QTime expectedTime=QTime::fromString(s.duration, "m:ss");
                QStringList durations=s.fileDurations.split('\n');
                for (int i=0;i<durations.count();++i)
                {
                    QTime fileTime=QTime::fromString(durations.at(i), "m:ss");
                    int diff=abs(fileTime.secsTo(expectedTime));
                    if (diff>maxDiff)
                        maxDiff=diff;
                }
                if (maxDiff>=10)
                    t+=QString("TIMEDIFF=%1 ").arg(maxDiff, (int)2, (int)10, QChar('0'));
            }
            if (s.fuzzyMatchedFileNames.count())
                    t+=FUZZY;

            if (!checkForRenaming)
                return t;

            static QRegExp distinguisher(" \\([a-z0-9 \\-]+\\)");
            QString expectedFilename=item.sibling(item.row(), 0).data(Qt::UserRole).toString();
            expectedFilename.remove(distinguisher);
            expectedFilename=simplify(expectedFilename);
            for (int i=0;i<qMin(1, s.matchedFileNames.count()); i++)
            {
                QString fn=s.matchedFileNames.at(i).left(s.matchedFileNames.at(i).lastIndexOf('.'));
                fn.remove(distinguisher);
                if (simplify(fn)!=expectedFilename)
                {
                    t+=NEEDSRENAME;
                    break;
                }
            }
            
            if (s.fileDurations.count() && !s.fileComposers.startsWith(s.composer))
                t+=NEEDSTAGWRITE;

            return t;
        }
        if (item.column()==Popularity)
        {
            static QString templ("%1");
            return templ.arg(s.popularity, 2);
        }
        if (item.column()==Date)
        {
            return s.year;
        }
        if (item.column()==Bpm)
        {
            return s.bpm;
        }
        if (item.column()==OriginalFileNames)
        {
            return s.originalFileNames;
        }
    }
    if (role==Qt::ToolTipRole)
    {
        QString expectedFilename=deaccent(item.sibling(item.row(), 0).data(Qt::UserRole).toString());
        QString t;
        for (int i=0;i<s.matchedFileNames.count(); i++)
            t+=userVisibleWordDiff(s.matchedFileNames.at(i).left(s.matchedFileNames.at(i).lastIndexOf('.')), expectedFilename, Html)+"<br>";
        
        for (int i=0;i<s.fuzzyMatchedFileNames.count(); i++)
            t+=userVisibleWordDiff(s.fuzzyMatchedFileNames.at(i).left(s.fuzzyMatchedFileNames.at(i).lastIndexOf('.')), expectedFilename, Html)+"<br>";
        
        return t;
        //TODO return ";;";
    }
	return QVariant();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls() || event->mimeData()->urls().isEmpty())
        return;
    
    //TODO drops on tree view
    setDir(event->mimeData()->urls().at(0).toLocalFile());
}

void MainWindow::setDir(QString dirPath, QString xmlFileName)
{
    bool isRefreshing=(dirPath==this->dirPath && xmlFileName==this->xmlFileName);

    QFileInfoList xmlfiles;
    QFileInfo info(dirPath);
    if (!info.exists())
        return;

    if (dirPath.endsWith(".xml"))
    {
        xmlfiles.append(info);
        dirPath=info.dir().absolutePath();
    }
    QDir dir(dirPath);
    if (!dir.exists())
        return;

    if (xmlFileName.isEmpty())
    {
        xmlfiles+=dir.entryInfoList(QStringList("*.xml"), QDir::Files, QDir::Name);
        if (xmlfiles.isEmpty() || !xmlfiles.at(0).exists())
            return;

        xmlFileName=xmlfiles.at(0).filePath();
    }
    if (dirPath.endsWith('/')) dirPath.chop(1);
    this->dirPath=dirPath;

    this->xmlFileName=xmlFileName;

    setUpdatesEnabled(false);
   
    ui->results->clear();
    songData.clear();
    QString orchestra;
    QString source;
    loadDiscoGraphyTable(xmlFileName, songData, orchestra, source);

    QString simplifiedOrchestra=simplify(orchestra);

    QStringList masks("*.mp3"); masks<<"*.m4a"<<"*.flac";
    QFileInfoList files=dir.entryInfoList(masks);
    for(int songI=0; songI<songData.size();songI++)
    {
        SongData& s=songData[songI];
        int barPos=s.name.indexOf(" | ");
        QString simplifiedName=simplify(s.name.mid(0, barPos));
        QString simplifiedName2; if (barPos>0) simplifiedName2=simplify(s.name.mid(barPos+3));
        /* wrong behaviour for aaa (b)
        if (barPos==-1)
        {
            int bracePos=s.name.indexOf(" (");
            if (bracePos>0) simplifiedName2=simplify(s.name.mid(bracePos+2));
        }
        */
        QString simplifiedVocal=simplify(s.vocal);
        for (int fileI = 0; fileI < files.size(); ++fileI)
        {
            QString simplifiedFilename=simplify(files.at(fileI).fileName()).toLower();
            if (!simplifiedVocal.contains(simplifiedOrchestra)) simplifiedFilename.remove(simplifiedOrchestra); //Fresedo - Fresedo; contracanto por Francisco Canaro
            if (!simplifiedFilename.contains(simplifiedName))
            {
                //do not match only if the alternative name does not match as well
                if (!simplifiedName2.length() || !simplifiedFilename.contains(simplifiedName2))
                    continue;
            }
            //only match Candombe as a name when the genre is also Candombe if filename contains it twice
            const QString& g=s.genre.toLower();
            if (simplifiedName.contains(g) && simplifiedFilename.count(g)<2)
                continue;
#ifndef TRIMSPACES
            if (!simplifiedFilename.contains(simplifiedVocal))
                continue;
            QString year=s.year.left(4);
            if (!simplifiedFilename.contains(year))
                continue;
#endif
            s.matchedFileNames.append(files.takeAt(fileI--).fileName());
        }

    }
    
    QSet<QString> fuzzyMatchedFiles;
    if (ui->fuzzyMatching->isChecked())
    {
        int fileI = files.size();
        while(--fileI>=0)
        {
            TagLib::FileRef f(CONVFN(files.at(fileI).absoluteFilePath()));
            if (!f.file() || !f.tag())
                continue;
            TagLib::String title = f.tag()->title();
            QString fileTitle=t2q (title);
            static QRegExp simplificationRE(" *\\(.*\\)");
            fileTitle.remove(simplificationRE);
            QString simplifiedFileTitle=simplify(fileTitle);

            for(int songI=0; songI<songData.size();songI++)
            {
                SongData& s=songData[songI];
                if (s.matchedFileNames.count())
                    continue;

                QString simplifiedName=s.name;
                simplifiedName=simplify(simplifiedName.remove(simplificationRE));

                QString simplifiedVocal=s.vocal;
                simplifiedVocal=simplify(simplifiedVocal.remove(simplificationRE));

				QString simplifiedFileTitleCleaned = simplifiedFileTitle;
				if (simplifiedFileTitleCleaned.contains(simplifiedVocal)) simplifiedFileTitleCleaned.remove(simplifiedVocal);

                int d=levenshtein_distance<QString>(simplifiedName, simplifiedFileTitleCleaned);
                if (float(d)/simplifiedName.length()<0.2)
                {
                    /*
                    static QRegExp year("19..");
                    if (year.indexIn(files.at(fileI).fileName())!=-1
                        && !s.year.startsWith(year.cap(0)) )
                        continue;
                    */
                        
                    fuzzyMatchedFiles.insert(files.at(fileI).fileName());
                    s.fuzzyMatchedFileNames.append(files.at(fileI).fileName());
                    //qDebug()<<files.at(fileI).fileName()<<s.name<<s.vocal<<s.year;
                }
            }
        }
    }

    if (files.size()-fuzzyMatchedFiles.count())
        ui->results->append(QString("Not matched (%1):").arg(files.size()-fuzzyMatchedFiles.count()));
    QString html;
    for (int fileI = 0; fileI < files.size(); ++fileI)
    {
        if (!fuzzyMatchedFiles.contains(files.at(fileI).fileName()))
        {
            html += "<br>"%files.at(fileI).fileName()
            % " (<a href=\"play:"%files.at(fileI).fileName()%"\">play</a>, " 
            % " <a href=\"rename:"%files.at(fileI).fileName()%"\">rename</a>)";
        }
    }
    ui->results->insertHtml(html);

    if (files.size()-fuzzyMatchedFiles.count())
        ui->results->append(QString());
    if (fuzzyMatchedFiles.count())
        ui->results->append(QString("Fuzzy matched: %1").arg(fuzzyMatchedFiles.count()));

    int matchedCount=0;
    for(int songI=0; songI<songData.size();songI++)
        matchedCount+=!(songData.at(songI).matchedFileNames.isEmpty());
    int percent=0;
    if (songData.size())
        percent=int(100*(matchedCount+fuzzyMatchedFiles.count())/songData.size());
    ui->results->append(QString("Matched: %1+%2 / %3 (%4%)").arg(matchedCount).arg(fuzzyMatchedFiles.count()).arg(songData.size()).arg(percent));
    ui->results->append(QString());


    //model->setSongData(songData, xmlfiles.at(0).baseName());
    model->setSongData(songData, orchestra);

    if (!isRefreshing && ui->treeView->model()->rowCount()) //means except when bad filter is entered
    {
        ui->treeView->resizeColumnToContents(SongDataModel::DiscoItem);
        ui->treeView->resizeColumnToContents(SongDataModel::Files);
        int maxWidth=(int)QApplication::desktop()->width()/2.5;
        if (ui->treeView->columnWidth(SongDataModel::DiscoItem)>maxWidth)
            ui->treeView->setColumnWidth(SongDataModel::DiscoItem, maxWidth);
        if (ui->treeView->columnWidth(SongDataModel::Files)>maxWidth)
            ui->treeView->setColumnWidth(SongDataModel::Files, maxWidth);
    }

    ui->label->hide();
    ui->filterEdit->show();
    ui->results->show();
    ui->treeView->show();
    ui->fuzzyMatching->show();
    
    setWindowTitle("TangoMatcher: "%orchestra%" by "%source);

    refreshId++;
    readDurations();

    setUpdatesEnabled(true);
}

void MainWindow::copyName()
{
    int row=ui->treeView->currentIndex().row();
    if (row>=0)
        QApplication::clipboard()->setText(deaccent(ui->treeView->currentIndex().data(Qt::UserRole).toString()));

    QString result;
    QModelIndexList rows=ui->treeView->selectionModel()->selectedRows();
    if (rows.count()<=1) return;
    foreach(QModelIndex index, rows)
    {
        result+=index.sibling(index.row(), 0).data(Qt::UserRole).toString()+'\n';
        //result+=index.data(Qt::UserRole).toString()+'\n';
    }
    QApplication::clipboard()->setText(result);
    
}

void MainWindow::refresh()
{
    setDir(dirPath, xmlFileName);
}


MainWindow::MainWindow(QWidget *parent)
 : QMainWindow(parent)
 , ui(new Ui::MainWindow)
 , refreshId(0)
{
    ui->setupUi(this);
    ui->filterEdit->hide();
    ui->results->hide();
    ui->treeView->hide();
    ui->fuzzyMatching->hide();

    setAcceptDrops(true);

    model=new SongDataModel(this);

    sortModel=new QSortFilterProxyModel(this);
    sortModel->setSourceModel(model);
    sortModel->setDynamicSortFilter(true);
    sortModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    sortModel->setFilterKeyColumn(-1);
    sortModel->sort(0, Qt::AscendingOrder);
    ui->treeView->setModel(sortModel);

    connect(ui->filterEdit, SIGNAL(textChanged(QString)), sortModel, SLOT(setFilterFixedString(QString)));
    connect(ui->fuzzyMatching, SIGNAL(clicked(bool)), this, SLOT(refresh()));
    connect(ui->treeView, SIGNAL(activated(QModelIndex)), this, SLOT(playFiles(QModelIndex)));

    int w=QFontMetrics(QFont()).averageCharWidth();
    ui->treeView->setColumnWidth(SongDataModel::Duration, w*5);
    ui->treeView->setColumnWidth(SongDataModel::FileDurations, w*5);
    ui->treeView->setColumnWidth(SongDataModel::Popularity, w*3);
    ui->treeView->setColumnWidth(SongDataModel::Date, w*10);
    ui->treeView->setColumnWidth(SongDataModel::Bpm, w*6);

    QSettings s;
    ui->treeView->header()->restoreState(s.value("columnsState").toByteArray());

    ui->treeView->setContextMenuPolicy(Qt::ActionsContextMenu);
    QAction* a= new QAction( tr("Copy discography name(s)"), this );
    a->setShortcut(QKeySequence::Copy);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(a, SIGNAL(triggered(bool)), this, SLOT(copyName()));
    ui->treeView->addAction(a);

    a= new QAction( tr("Write standard tags for selected files"), this );
    connect(a, SIGNAL(triggered(bool)), this, SLOT(writeTags()));
    ui->treeView->addAction(a);

    a= new QAction( tr("Count BPM for selected files"), this );
    a->setShortcut(QKeySequence::Bold);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(a, SIGNAL(triggered(bool)), this, SLOT(doBeatCount()));
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
/*
    a= new QAction( tr("Read file durations"), this );
    connect(a, SIGNAL(triggered(bool)), this, SLOT(readDurations()));
    ui->treeView->addAction(a);
*/
    QShortcut* sc=new QShortcut(Qt::CTRL+Qt::Key_L, ui->filterEdit, SLOT(setFocus()), 0, Qt::WindowShortcut);
    connect(sc, SIGNAL(activated()), ui->filterEdit, SLOT(selectAll()));

    connect(ui->results, SIGNAL(anchorClicked(QUrl)), this, SLOT(handleFileLink(QUrl)));

    connect(ui->openSettings, SIGNAL(clicked(bool)), this, SLOT(editTagScheme()));
    
    connect(ui->saveBpms, SIGNAL(clicked(bool)), this, SLOT(saveBpmTable()));
    
    
    progressBar=new QProgressBar(statusBar());
    progressBar->setRange(0, 100);
    statusBar()->addPermanentWidget(progressBar);
    progressBar->hide();
}

MainWindow::~MainWindow()
{
    QSettings s;
    s.setValue("columnsState", ui->treeView->header()->saveState());
    delete ui;
}

void MainWindow::handleFileLink(const QUrl& url)
{
    if (url.scheme()=="rename")
    {
        QString baseName=url.path();
        QString ext=baseName.mid(baseName.lastIndexOf('.'));
        baseName.chop(ext.size());

        QInputDialog input(this);
        input.setInputMode(QInputDialog::TextInput);
        input.setTextValue(baseName);
        input.setLabelText(tr("Edit filename (without extension):"));
        if (!input.exec())
            return;

        QFile::rename(dirPath%"/"%url.path(), dirPath%"/"%input.textValue()%ext);
        QTimer::singleShot(0, this, SLOT(refresh()));
    }
    else
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(dirPath).canonicalFilePath()%"/"%url.path()));
    }
}


void MainWindow::playFiles(QModelIndex index)
{
    QString dp=QFileInfo(dirPath).canonicalFilePath(); //player will run with different working dir
    const SongData& s=songData.at(sortModel->mapToSource(index).row());
    for (int fileI = 0; fileI < s.matchedFileNames.size(); ++fileI)
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dp%"/"%s.matchedFileNames.at(fileI)));
    }
    for (int fileI = 0; fileI < s.fuzzyMatchedFileNames.size(); ++fileI)
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(dp%"/"%s.fuzzyMatchedFileNames.at(fileI)));
    }
}


void MainWindow::readDurations()
{
    ReadTagsTask* task=new ReadTagsTask;
    connect(task, SIGNAL(done(SongDataVector,int)), this, SLOT(applyEnhancedSongData(SongDataVector,int)));
    prepareAndStartTask(task, /*selectedOnly*/ false);
}

void MainWindow::applyEnhancedSongData(SongDataVector songData, int rId)
{
    if (rId==this->refreshId)
        model->setSongData(this->songData=songData);
}

static int findMaxPop(const QVector<SongData>& songData)
{
    int maxPop=0;
    for(int songI=0; songI<songData.size();songI++)
    {
        const SongData& s=songData.at(songI);
        if (s.popularity>maxPop)
            maxPop=s.popularity;
    }
    return maxPop;
}

void MainWindow::writeTags()
{
    //statusBar()->showMessage(tr("Tag writing done."), 10000);
    WriteTagsTask* task=new WriteTagsTask;
    prepareAndStartTask(task);
}

void MainWindow::copyShort()
{
    QSettings s;
    if (s.value("copyTitle").toString().isEmpty() && !editTagScheme())
        return;
    
    CopyShortTask* task=new CopyShortTask;
    task->copyTitle=s.value("copyTitle").toString();
    task->copyArtist=s.value("copyArtist").toString();
    task->copyAlbum=s.value("copyAlbum").toString();

    task->targetDirPath=s.value("copyDir", QDir::rootPath()).toString();
    
    task->targetDirPath=QFileDialog::getExistingDirectory(this, tr("Destination directory"), task->targetDirPath);
    
    if (task->targetDirPath.isEmpty())
        return;
    
    s.setValue("copyDir", task->targetDirPath);

    prepareAndStartTask(task);
}

void MainWindow::prepareAndStartTask(Task* task, bool selectedOnly)
{
    task->orchestra=model->orchestra;
    task->dirPath=dirPath;
    task->refreshId=refreshId;

    if (selectedOnly)
    {
        foreach(QModelIndex index, ui->treeView->selectionModel()->selectedRows())
        {
            const SongData& s=songData.at(sortModel->mapToSource(index).row());
            if(s.matchedFileNames.size()) task->songData.append(s);
        }
    }
    else
        task->songData=songData;

    connect(task, SIGNAL(started()), progressBar, SLOT(show()));
    connect(task, SIGNAL(progress(int)), progressBar, SLOT(setValue(int)));
    connect(task, SIGNAL(finished()), progressBar, SLOT(hide()));

    QThreadPool::globalInstance()->setMaxThreadCount(1);
    QThreadPool::globalInstance()->start(task);

    //if (task->failedFiles.count())
    //    QMessageBox::information(this, tr("File copy error"), tr("Some files failed to copy."));

}

bool MainWindow::editTagScheme()
{
    QDialog dialog;
    Ui_TagScheme ui;
    ui.setupUi(&dialog);

    QSettings s;
    ui.titlePattern->setText(s.value("copyTitle", "%gnr%%yer% %tit% {%can%}").toString());
    ui.artistPattern->setText(s.value("copyArtist", "%orquesta%").toString());
    ui.albumPattern->setText(s.value("copyAlbum", "%orq% - %genre%").toString());

    if (!dialog.exec())
        return false;

    s.setValue("copyTitle", ui.titlePattern->text());
    s.setValue("copyArtist", ui.artistPattern->text());
    s.setValue("copyAlbum", ui.albumPattern->text());
    
    return true;
}


void MainWindow::rename()
{
    QMap<QString, QString> newToOldFilename;

    QModelIndexList rows=ui->treeView->selectionModel()->selectedRows();
    foreach(QModelIndex index, rows)
    {
        SongData& s=songData[sortModel->mapToSource(index).row()];
        QStringList filenames=s.matchedFileNames+s.fuzzyMatchedFileNames;
        for (int fileI = 0; fileI < filenames.size(); ++fileI)
        {
            QString oldFilename=filenames.at(fileI);
            QString fileDistinguisher=extractDistinguisher(oldFilename);
            QString discographyDistinguisher=extractDistinguisher(deaccent(s.name));
            QString discographyVocalDistinguisher=extractDistinguisher(deaccent(s.vocal));
            if (discographyDistinguisher==fileDistinguisher || discographyVocalDistinguisher==fileDistinguisher)
                fileDistinguisher.clear(); //e.g. El once (a divertirse) (a divertirse)

            //TODO | alternative names
            QString name=s.name;
#define ONLY_FIRST_PART_IN_NAME 1 //win32 doesnt allow | in the flename anyway
#ifdef ONLY_FIRST_PART_IN_NAME
            int barPos=name.indexOf(" | ");
            if (barPos!=-1)
            {
                //see what part actually matched
                name=name.mid(0, barPos).trimmed();
                QString name2=s.name.mid(barPos+3).trimmed();

                QString simplifiedName=simplify(name);
                QString simplifiedName2=simplify(name2);

                if (!oldFilename.contains(simplifiedName) && oldFilename.contains(simplifiedName2))
                    name=name2;
            }
#endif
            //qDebug()<<deaccent(index.sibling(index.row(), 0).data(Qt::UserRole).toString())%oldFilename.midRef(oldFilename.lastIndexOf('.'));

            QString year=s.year;
            year=year.left(4);
            if (s.genre.length()) s.genre[0]=s.genre.at(0).toUpper();
            //QString genre=s.genre.endsWith("ango")?QString():(" - "+s.genre);
            QString genre=" - "+s.genre;
            QString newFilename=deaccent(model->orchestra%" - "%s.vocal%" - "%name%fileDistinguisher%" - "%year%genre%oldFilename.mid(oldFilename.lastIndexOf('.')).toLower());
            newFilename.remove('/');
            newFilename.remove('\\');
            newFilename.remove('?');
            newFilename.remove('"');
            if (oldFilename!=newFilename)
            {
                if (filenames.size()>1 && QMessageBox::No==QMessageBox::question(this, tr("Rename?"), "Rename\n"%oldFilename%"\nto\n"%newFilename%"\n?", QMessageBox::Yes| QMessageBox::No) )
                    continue;

                newToOldFilename[newFilename]=oldFilename;

#ifdef Q_OS_WIN32
                if(oldFilename.toLower()==newFilename.toLower())
                {
                    QFile::rename(dirPath%"/"%oldFilename, dirPath%"/"%oldFilename+"_");
                    oldFilename+='_';
                }
#endif
                
                if (!QFile::rename(dirPath%"/"%oldFilename, dirPath%"/"%newFilename))
                {
                    newToOldFilename.remove(newFilename);
                    qWarning()<<"cannot rename to"<<dirPath%"/"%newFilename;
                    if (QFile::exists(dirPath%"/"%newFilename))
                        qWarning()<<"already exists";
                }
;
            }
        }
    }

    WriteOriginalFileNamesToTagsTask* task=new WriteOriginalFileNamesToTagsTask();
    task->newToOldFilename=newToOldFilename;
    prepareAndStartTask(task);

    refresh();
}


void MainWindow::doBeatCount()
{
    BeatCounter* w=new BeatCounter();
    w->show();
    w->setAttribute(Qt::WA_DeleteOnClose);

    QStringList args;
    //QString dp=QFileInfo(dirPath).canonicalFilePath(); //player will run with different working dir
    
    foreach(QModelIndex index, ui->treeView->selectionModel()->selectedRows())
    {
        const SongData& s=songData.at(sortModel->mapToSource(index).row());
        int i=s.matchedFileNames.size();
        while (--i>=0)
            args.append(dirPath%"/"%s.matchedFileNames.at(i));
    }
    w->addAudioFiles(args);
}


void saveBpmTable(QString filename, const QVector<SongData>& songData, const QString& orchestra, const QString& dirPath)
{
    if (!filename.endsWith(".xml"))
        filename+=".xml";
    QFile xmldata(filename);
    xmldata.open(QFile::WriteOnly | QFile::Truncate);

    QXmlStreamWriter stream(&xmldata);
    stream.setAutoFormatting(true);
    stream.writeStartDocument();

    stream.writeStartElement("bpm");
    stream.writeAttribute("orchestra", orchestra);
    for(int songI=0; songI<songData.size();songI++)
    {
        const SongData& s=songData.at(songI);
        QStringList bpms=s.bpm.split('\n', QString::KeepEmptyParts);
        int i=s.matchedFileNames.size();
        qDebug()<<i<<s.matchedFileNames.size()<<s.bpm;
        while (--i>=0)
        {
            QString bpm;
            if (s.matchedFileNames.size()==bpms.size())
                bpm=bpms.at(i);
            else
                bpm=bpmFromFile(dirPath%"/"%s.matchedFileNames.at(i));

            if (bpm.length() && !bpm.contains('.'))
            {
                stream.writeStartElement("file");
                stream.writeAttribute("name", s.matchedFileNames.at(i));
                stream.writeAttribute("bpm", bpm);
                stream.writeEndElement();
            }
        }
        
    }
    stream.writeEndElement(); // bookmark
    stream.writeEndDocument();
}

void MainWindow::saveBpmTable()
{
    QString filename=QFileDialog::getSaveFileName(this, tr("Save BPM info"), dirPath, "*.xml");
    if (filename.length())
        ::saveBpmTable(filename, songData, model->orchestra, dirPath);
}

