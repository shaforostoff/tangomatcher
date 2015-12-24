#include "jobs.h"
#include "songdata.h"
#include "utils.h"

#include <QtCore>
#include <QtGlobal>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#ifdef Q_OS_WIN32
#include <taglib/mpeg/mpegfile.h>
#include <taglib/mpeg/id3v1/id3v1tag.h>
#include <taglib/mpeg/id3v2/id3v2tag.h>
#include <taglib/mpeg/id3v2/frames/textidentificationframe.h>
#include <taglib/mpeg/id3v2/frames/popularimeterframe.h>
#include <taglib/mpeg/id3v2/frames/attachedpictureframe.h>
#include <taglib/mp4/mp4file.h>
#include <taglib/flac/flacfile.h>
#include <taglib/ogg/xiphcomment.h>
#endif
#ifdef Q_OS_LINUX
#include <taglib/mpegfile.h>
#include <taglib/id3v1tag.h>
#include <taglib/id3v2tag.h>
#include <taglib/textidentificationframe.h>
#include <taglib/popularimeterframe.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/mp4file.h>
#include <taglib/flacfile.h>
#include <taglib/xiphcomment.h>
#endif
#ifdef Q_OS_MAC
#include <taglib/mpeg/mpegfile.h>
#include <taglib/mpeg/id3v1/id3v1tag.h>
#include <taglib/mpeg/id3v2/id3v2tag.h>
#include <taglib/flac/flacfile.h>
#include <taglib/mp4/mp4file.h>
#include <taglib/ogg/xiphcomment.h>
#include <taglib/mpeg/id3v2/frames/textidentificationframe.h>
#endif



#ifdef Q_OS_WIN32
#define CONVFN(qstring) (const wchar_t*)qstring.unicode()
#else
#define CONVFN(qstring) qstring.toUtf8().constData()
#endif


static QString substitute(QString pattern, SongData& s, const QString& orchestra, const QString& fileDistinguisher)
{
    pattern.replace("%title%", s.name+fileDistinguisher);
    int barPos=s.name.indexOf(" | ");
    pattern.replace("%tit%", s.name.left(barPos)+fileDistinguisher);

    //pattern.replace("%orchestra%", orchestra);
    pattern.replace("%orquesta%", orchestra);
    pattern.replace("%orq%", orchestra.mid(orchestra.indexOf(' ')+1));

    //pattern.replace("%vocal%", s.vocal);
    pattern.replace("%cantor%", s.vocal);
    QString shortVocal=s.vocal.mid(s.vocal.indexOf(' ')+1);
    pattern.replace("%can%", shortVocal);
    //pattern.replace("%voc%", shortVocal);

    QString genre=s.genre;
    genre.remove("Arr. en ", Qt::CaseInsensitive);
    pattern.replace("%gnr%", genre.left(1).toLower());
    pattern.replace("%genre%", s.genre);

    pattern.replace("%yer%", s.year.left(4));
    pattern.replace("%date%", s.year);

    //pattern.replace("%album%", s.);
    return pattern;
}


void CopyShortTask::run()
{
    emit started();

    //int maxPop=findMaxPop(songData);
    for(int songI=0; songI<songData.size();songI++)
    {
        emit progress(100*songI/songData.size());

        SongData s=songData.at(songI);
        for (int fileI = 0; fileI < s.matchedFileNames.size(); ++fileI)
        {
            QString fileDistinguisher=extractDistinguisher(s.matchedFileNames.at(fileI));
            QString discographyDistinguisher=extractDistinguisher(deaccent(s.name));
            QString discographyVocalDistinguisher=extractDistinguisher(deaccent(s.vocal));
            if (discographyDistinguisher==fileDistinguisher || discographyVocalDistinguisher==fileDistinguisher)
                fileDistinguisher.clear(); //e.g. El once (a divertirse) (a divertirse)

            if (fileDistinguisher.length() && !qApp->arguments().contains("--include-alternatives"))
                continue;

            QString newFilename=s.matchedFileNames.at(fileI);
            newFilename.remove('/');
            newFilename.remove('\\');
            newFilename.remove('?');
            newFilename.remove('"');
            newFilename.remove('|');
            newFilename.prepend(targetDirPath+'/');
            
#ifdef Q_OS_LINUX
            static bool encoderFound=QFile::exists(QCoreApplication::applicationDirPath() % "/NeroAACCodec-1.5.1/linux/neroAacEnc");
            if (encoderFound && (s.matchedFileNames.at(fileI).endsWith(".mp3")||s.matchedFileNames.at(fileI).endsWith(".flac")))
            {
                newFilename.replace(".flac", ".mp4");
                newFilename[newFilename.length()-1]='4';
                QFileInfo info(newFilename);
                if (!qApp->arguments().contains("--force-replace") && info.exists() && info.lastModified().date()!=QDate::currentDate())
                {
                    if (QFileInfo(dirPath%"/"%s.matchedFileNames.at(fileI)).lastModified()<info.lastModified())
                        continue;
                }
                qWarning()<<"encoding"<<s.matchedFileNames.at(fileI);
                QProcess process1;
                QProcess process2;

                process1.setStandardOutputProcess(&process2);
                process2.setProcessChannelMode(QProcess::ForwardedChannels);

                if (s.matchedFileNames.at(fileI).endsWith(".mp3"))
                    process1.start("lame --decode \"" % dirPath%"/"%s.matchedFileNames.at(fileI) % "\" -");
                else
                    process1.start("flac --decode \"" % dirPath%"/"%s.matchedFileNames.at(fileI) % "\" -o -");
                process2.start(QCoreApplication::applicationDirPath() % "/NeroAACCodec-1.5.1/linux/neroAacEnc -q 0.4 -if - -of \"" % newFilename % "\"");
                process1.waitForFinished(1000000);
                process2.waitForFinished(1000000);
            }
#else            
            if (!QFile::copy( dirPath%"/"%s.matchedFileNames.at(fileI), newFilename ))
            {
                failedFiles.append(newFilename);
                continue;
            }
#endif
            TagLib::FileRef f(CONVFN(newFilename));
            if (!f.file())
                continue;

            {
                if (TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file()) )
                    mpf->ID3v2Tag(true);
            }
            if (!f.tag())
                continue;

            QString genre=s.genre;
            genre.remove("Arr. en ", Qt::CaseInsensitive);
            
            //int barPos=s.name.indexOf(" | ");
            //f.tag()->setTitle(q2t(genre.left(1).toLower() % s.year.left(4) % " " % s.name.left(barPos)+fileDistinguisher));
            f.tag()->setTitle(q2t(substitute(copyTitle, s, orchestra, fileDistinguisher)));
            f.tag()->setArtist(q2t(substitute(copyArtist, s, orchestra, fileDistinguisher)));
            if (copyAlbum.length())
                f.tag()->setAlbum(q2t(substitute(copyAlbum, s, orchestra, fileDistinguisher)));

            //QString shortOrchestra=orchestra.mid(orchestra.indexOf(' ')+1);
            //QString shortVocal=s.vocal.mid(s.vocal.indexOf(' ')+1);
            //f.tag()->setArtist(q2t(shortOrchestra));
            //f.tag()->setAlbum(q2t(shortOrchestra%" - "%shortVocal));
            if (s.genre.length()) s.genre[0]=s.genre.at(0).toUpper();
            f.tag()->setGenre(q2t(s.genre));

#if 0
//#ifdef Q_OS_LINUX
            if(!s.matchedFileNames.at(fileI).endsWith(".mp4"))
            {
                TagLib::FileRef f(CONVFN(QString(dirPath%"/"%s.matchedFileNames.at(fileI))));
                TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file());
                TagLib::ID3v2::Tag* tag2=mpf?mpf->ID3v2Tag():0;
                if (tag2 && tag2->frameListMap().contains("APIC"))
                {
                    if (TagLib::ID3v2::AttachedPictureFrame* f=dynamic_cast<TagLib::ID3v2::AttachedPictureFrame*>(tag2->frameList("APIC")[0]))
                    {
                        QFile a("test.jpg");
                        a.open(QIODevice::Truncate | QIODevice::WriteOnly);
                        a.write(f->picture().data(), f->picture().size());
                        system("convert test.jpg smaller.jpg");
                        QProcess::execute(QCoreApplication::applicationDirPath() % "/NeroAACCodec-1.5.1/linux/neroAacTag '" %newFilename %"' -add-cover:front:smaller.jpg");
                    }
                }
            }
#endif
            if (!f.save())
            {}//QMessageBox::information(this, tr("Tag write error"), tr("Cannot write tag to ")%s.matchedFileNames.at(fileI)%".");
        }
    }
    emit finished();
}


void MatchFilesTask::run()
{
    emit started();

}

void WriteTagsTask::run()
{
    emit started();

    //int maxPop=findMaxPop(songData);
    for(int songI=0; songI<songData.size();songI++)
    {
        emit progress(100*songI/songData.size());

        SongData s=songData.at(songI);

        for (int fileI = 0; fileI < s.matchedFileNames.size(); ++fileI)
        {
            TagLib::FileRef f(CONVFN(QString(dirPath%"/"%s.matchedFileNames.at(fileI))));
            if (!f.file())
                continue;
            //if (!f.tag())
            {
                if (TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file()) )
                    mpf->ID3v2Tag(true);
            }
            if (!f.tag())
                continue;

            QString fileDistinguisher=extractDistinguisher(s.matchedFileNames.at(fileI));
            QString discographyDistinguisher=extractDistinguisher(deaccent(s.name));
            QString discographyVocalDistinguisher=extractDistinguisher(deaccent(s.vocal));
            if (discographyDistinguisher==fileDistinguisher || discographyVocalDistinguisher==fileDistinguisher)
                fileDistinguisher.clear(); //e.g. El once (a divertirse) (a divertirse)
            
            /*
            //rewrite second part after | in braces
            int barPos=s.name.indexOf(" | ");
            QString name=s.name.mid(0, barPos).trimmed();
            if (barPos!=-1)
                name+=" ("%s.name.mid(barPos+3).trimmed()%")";
            */
            int barPos=s.name.indexOf(" | ");
            //qDebug()<<s.name.mid(0, barPos).trimmed();
            
            QString oldAlbum=t2q(f.tag()->album());
            qDebug()<<"OOO"<<oldAlbum;

            QString vocalA;//=s.vocal=="Instrumental"?"":(" - "+s.vocal);
            f.tag()->setTitle(q2t(s.name+fileDistinguisher));
            //f.tag()->setArtist(q2t(orchestra));
            //f.tag()->setAlbum(q2t(orchestra%" - "%s.vocal));
            f.tag()->setArtist(q2t(orchestra%" - "%s.vocal));
            if (orchestra.isEmpty()) f.tag()->setArtist(q2t(s.vocal));
            //f.tag()->setAlbum("TngoEntropy");
            if (s.genre.length()) s.genre[0]=s.genre.at(0).toUpper();
            f.tag()->setGenre(q2t(s.genre));

            int year=s.year.left(4).toInt();
            if (year>1800)
                f.tag()->setYear(year);

            //f.tag()->setComment("TngoEntropy");
            if (f.tag()->comment()=="TngoEntropy")
                f.tag()->setComment("");

            TagLib::FLAC::File* flacf=dynamic_cast<TagLib::FLAC::File*>(f.file());
            TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file());
            if (mpf || flacf)
            {
                TagLib::ID3v1::Tag* tag1=0;
                if (mpf) tag1=mpf->ID3v1Tag(true);
                if (flacf) tag1=flacf->ID3v1Tag();
                if (tag1)
                {
                    tag1->setTitle(q2t(deaccent(s.name.mid(0, barPos).trimmed()+fileDistinguisher)));
                    //tag1->setAlbum(q2t(deaccent(orchestra%" - "%s.vocal)));
                    //tag1->setArtist(q2t(deaccent(orchestra)));
                    tag1->setArtist(q2t(deaccent(orchestra%" - "%s.vocal)));
                    if (orchestra.isEmpty()) tag1->setArtist(q2t(deaccent(s.vocal)));
                }
            }

            if (flacf)
            {
                /*
                TagLib::Ogg::FieldListMap map=flacf->xiphComment(true)->fieldListMap();
                for (TagLib::Ogg::FieldListMap::ConstIterator it=map.begin();it!=map.end();++it)
                {
                    qDebug()<<t2q((*it).first);
                    TagLib::StringList list=(*it).second;
                    for (int i=0;i<list.size();++i)
                        qDebug()<<t2q(list[i]);
                }*/
                
                flacf->xiphComment(true);
                if (oldAlbum.length() && !flacf->xiphComment()->contains("ORIGINAL ALBUM"))
                    flacf->xiphComment()->addField("ORIGINAL ALBUM", q2t(oldAlbum));
                if (s.year.length()) flacf->xiphComment()->addField("DATE", q2t(s.year));
                //if (s.year.length()) flacf->xiphComment()->addField("RECORDING DATE", q2t(s.year));
                if (s.genre.length()) flacf->xiphComment()->addField("GENRE", q2t(s.genre));
                if (s.composer.length()) flacf->xiphComment()->addField("COMPOSER", q2t(s.composer));
                if (s.author.length()) flacf->xiphComment()->addField("LYRICIST", q2t(s.author));

                flacf->xiphComment()->addField("ALBUMARTIST", q2t(orchestra));
            }
            
            if (TagLib::MP4::File* mp4f=dynamic_cast<TagLib::MP4::File*>(f.file()))
            {
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

                    map.erase("aART");
                    map["\251day"]=TagLib::MP4::Item(TagLib::StringList(q2t(s.year)));
                    if (s.composer.length())
                        map["\251wrt"]=TagLib::MP4::Item(TagLib::StringList(q2t(s.composer)));
                    //fucks up tag in m4a file on windows
                    //if (s.author.length())
                    //    map["----:com.apple.iTunes:LYRICIST"]=TagLib::MP4::Item(TagLib::StringList(q2t(s.author)));
                    //if (oldAlbum.length())
                    //    map["----:com.apple.iTunes:ORIGINAL ALBUM"]=TagLib::MP4::Item(TagLib::StringList(q2t(oldAlbum)));

                    if (!tag->save())
                    {
                        failedFiles.append(s.matchedFileNames.at(fileI));
                        continue;
                    }
                }
            }

            if (mpf || flacf)
            {
                TagLib::ID3v2::Tag* tag2=0;
                if (mpf) tag2=mpf->ID3v2Tag(true);
                if (flacf) tag2=flacf->ID3v2Tag(); //taglib may not be able to create it properly
                if (tag2)
                {
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
                    if (map.contains("TIT2") && map["TIT2"].size())
                        if (TagLib::ID3v2::TextIdentificationFrame* f=dynamic_cast<TagLib::ID3v2::TextIdentificationFrame*>(tag2->frameList("TIT2")[0]))
                            f->setTextEncoding(TagLib::String::UTF8);
                    if (map.contains("TALB") && map["TALB"].size())
                        if (TagLib::ID3v2::TextIdentificationFrame* f=dynamic_cast<TagLib::ID3v2::TextIdentificationFrame*>(tag2->frameList("TALB")[0]))
                            f->setTextEncoding(TagLib::String::UTF8);

                    if (oldAlbum.length())
                    {
                        if (!map.contains("TOAL"))
                        {
                            tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TOAL", TagLib::String::UTF8));
                            tag2->frameList("TOAL")[0]->setText(q2t(oldAlbum));
                        }
                        //else
                        //    f.tag()->setAlbum(tag2->frameList("TOAL")[0]->toString());
                    }

                    if (s.composer.length())
                    {
                        if (!map.contains("TCOM"))
                            tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TCOM", TagLib::String::UTF8));
                        else if (TagLib::ID3v2::TextIdentificationFrame* f=dynamic_cast<TagLib::ID3v2::TextIdentificationFrame*>(tag2->frameList("TCOM")[0]))
                            f->setTextEncoding(TagLib::String::UTF8);
                        tag2->frameList("TCOM")[0]->setText(q2t(s.composer));
                    }
                    if (s.author.length())
                    {
                        if (!map.contains("TEXT"))
                            tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TEXT", TagLib::String::UTF8));
                        else if (TagLib::ID3v2::TextIdentificationFrame* f=dynamic_cast<TagLib::ID3v2::TextIdentificationFrame*>(tag2->frameList("TEXT")[0]))
                            f->setTextEncoding(TagLib::String::UTF8);
                        tag2->frameList("TEXT")[0]->setText(q2t(s.author));
                    }
                    if (s.label.length())
                    {
                        if (!map.contains("TPUB"))
                            tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TPUB", TagLib::String::UTF8));
                        tag2->frameList("TPUB")[0]->setText(q2t(s.label));
                    }
                    
                    {
                        if (!map.contains("TDRC"))
                            tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TDRC", TagLib::String::UTF8));
                        tag2->frameList("TDRC")[0]->setText(q2t(s.year));
                    }

                    if (orchestra.length())
                    {
                        if (!map.contains("TPE2"))
                            tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TPE2", TagLib::String::UTF8));
                        else if (TagLib::ID3v2::TextIdentificationFrame* f=dynamic_cast<TagLib::ID3v2::TextIdentificationFrame*>(tag2->frameList("TPE2")[0]))
                            f->setTextEncoding(TagLib::String::UTF8);
                        tag2->frameList("TPE2")[0]->setText(q2t(orchestra));
                    }
                    
                    {
                        if (!map.contains("TPE1"))
                            tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TPE1", TagLib::String::UTF8));
                        else if (TagLib::ID3v2::TextIdentificationFrame* f=dynamic_cast<TagLib::ID3v2::TextIdentificationFrame*>(tag2->frameList("TPE1")[0]))
                            f->setTextEncoding(TagLib::String::UTF8);
                        tag2->frameList("TPE1")[0]->setText(q2t(orchestra%" - "%s.vocal));
                        if (orchestra.isEmpty()) tag2->frameList("TPE1")[0]->setText(q2t(s.vocal));
                    }
                    /*
                    if (maxPop && s.popularity && map.contains("POPM"))
                    {
                        //if (!map.contains("POPM"))
                        //    tag2->addFrame(new TagLib::ID3v2::PopularimeterFrame());
                        TagLib::ID3v2::PopularimeterFrame* f=dynamic_cast<TagLib::ID3v2::PopularimeterFrame*> (tag2->frameList("POPM")[0]);
                        //f->setRating(255*s.popularity/maxPop);
                        //f->setEmail("popularity@tango.info");
                        qDebug()<<"sss"<<(255*s.popularity/maxPop)<<f->rating()<<t2q(f->email());
                    }
                    */
                    
                    //tag2->removeFrames("TPE2");
                }
            }
            
            
            if (!f.save())
                failedFiles.append(s.matchedFileNames.at(fileI));
        }
    }
    emit finished();
}

void WriteOriginalFileNamesToTagsTask::run()
{
    emit started();

    for(QMap<QString, QString>::const_iterator it=newToOldFilename.constBegin(); it!=newToOldFilename.constEnd();it++)
    {
        TagLib::FileRef f(CONVFN( QString(dirPath%"/"%it.key()) ));
        if (!f.file())
            continue;

        TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file());
        TagLib::FLAC::File* flacf=dynamic_cast<TagLib::FLAC::File*>(f.file());

        TagLib::ID3v2::Tag* tag2=0;
        if (mpf) tag2=mpf->ID3v2Tag(true);
        if (flacf) tag2=flacf->ID3v2Tag(); //taglib may not be able to create it properly
        if (tag2)
        {
            TagLib::ID3v2::FrameListMap map=tag2->frameListMap();

            if (!map.contains("TOFN"))
            {
                tag2->addFrame(new TagLib::ID3v2::TextIdentificationFrame("TOFN", TagLib::String::UTF8));
                tag2->frameList("TOFN")[0]->setText(q2t( it.value() ));
            }
        }
        f.save();
    }

    emit finished();
}

void ReadTagsTask::run()
{
    emit started();

    for(int songI=0; songI<songData.size();songI++)
    {
        if (songI%10==0) emit progress(100*songI/songData.size());

        SongData& s=songData[songI];
        TagLib::AudioProperties::ReadStyle mode=s.fileDurations.count()?TagLib::AudioProperties::Accurate : TagLib::AudioProperties::Average;
        s.fileDurations.clear();
        s.bpm.clear();
        s.fileComposers.clear();
        s.originalFileNames.clear();
        const QStringList& list=s.matchedFileNames.count()?s.matchedFileNames:s.fuzzyMatchedFileNames;
        for (int fileI = 0; fileI < list.size(); ++fileI)
        {
            TagLib::FileRef f(CONVFN(QString(dirPath%"/"%list.at(fileI))), true, mode);
            if (!f.file() || !f.file()->audioProperties())
            {
                qWarning()<<"bad file"<<list.at(fileI);
                continue;
            }

            QTime time=QTime(0,0).addSecs(f.file()->audioProperties()->length());
            s.fileDurations+=time.toString("m:ss")+'\n';
            QString bpm;

            TagLib::FLAC::File* flacf=dynamic_cast<TagLib::FLAC::File*>(f.file());
            TagLib::MPEG::File* mpf=dynamic_cast<TagLib::MPEG::File*>(f.file());
            if (mpf || flacf)
            {
                TagLib::ID3v2::Tag* tag2=0;
                if (mpf) tag2=mpf->ID3v2Tag(true);
                //if (flacf) tag2=flacf->ID3v2Tag(); //taglib may not be able to create it properly
                if (tag2)
                {
                    TagLib::ID3v2::FrameListMap map=tag2->frameListMap();
                    if (map.contains("TBPM"))
                        bpm=t2q(tag2->frameList("TBPM")[0]->toString())+'\n';
                    if (map.contains("TOFN"))
                        s.originalFileNames+=t2q(tag2->frameList("TOFN")[0]->toString())+'\n';
                    if (map.contains("TCOM"))
                        s.fileComposers+=t2q(tag2->frameList("TCOM")[0]->toString())+'\n';

                }
            }

            if (TagLib::MP4::File* mp4f=dynamic_cast<TagLib::MP4::File*>(f.file()))
            {
                TagLib::MP4::Tag* tag=mp4f->tag();
                if (tag)
                {
                    TagLib::MP4::ItemListMap& map=tag->itemListMap();

                    if (map.contains("tmpo"))
                        bpm=QString::number(map["tmpo"].toInt())+'\n';
                    if (map.contains("\251wrt"))
                        s.fileComposers+=t2q(map["\251wrt"].toStringList().front())+'\n';
                }
            }

            if (flacf && flacf->xiphComment())
            {
                if (flacf->xiphComment()->contains("COMPOSER"))
                    s.fileComposers+=t2q(flacf->xiphComment()->fieldListMap()["COMPOSER"].toString())+'\n';
                if (flacf->xiphComment()->contains("BPM"))
                    bpm=t2q(flacf->xiphComment()->fieldListMap()["BPM"].toString())+'\n';
            }
            s.bpm+=bpm;
        }
        s.fileDurations.chop(1);
        s.fileComposers.chop(1);
        s.originalFileNames.chop(1);
        s.bpm.chop(1);
    }
    emit done(songData, refreshId);
    emit finished();
}

