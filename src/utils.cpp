#include "utils.h"
#include <QRegExp>
#include <QVector>
#include <QChar>

QString t2q(const TagLib::String& t)
{
    return QString::fromWCharArray((const wchar_t*)t.toCWString(), t.length());
    //return QString::fromUcs4((const uint*)t.toCWString(), t.length());
    /*
    QString q; q.resize(t.size());
    for(int i=0;i<q.size();i++)
        q[i]=t[i];
    return q;*/
}

TagLib::String q2t(const QString& q)
{
    if (q.length()<1024)
    {
        static wchar_t tmp[1024]; //no thread safety
        q.toWCharArray(tmp);
        tmp[q.length()]=0;
        return TagLib::String(tmp);
    }

    wchar_t* tmp = new wchar_t[q.length()+1];
    q.toWCharArray(tmp);
    tmp[q.length()]=0;
    TagLib::String t(tmp);
    delete[] tmp;
    return t;

    //////return TagLib::String((const wchar_t*)q.utf16());
    //////return TagLib::String((const wchar_t*)q.unicode(), TagLib::String::UTF16LE);
    return TagLib::String(q.toStdWString());
}

QString extractDistinguisher(const QString& name)
{
    static QRegExp distinguisher(" \\([a-z0-9 \\-\\(\\)]+\\)");
    distinguisher.lastIndexIn(name);
    
    return distinguisher.cap(0);
}


QString deaccent(QString name)
{
    /*
    static QVector<QChar> letters, simpleL;
    if (letters.isEmpty())
    {
        letters<<QString::fromUtf8("ó").at(0); simpleL<<'o';
        letters<<QString::fromUtf8("é").at(0); simpleL<<'e';
        letters<<QString::fromUtf8("á").at(0); simpleL<<'a';
        letters<<QString::fromUtf8("ñ").at(0); simpleL<<'n';
        letters<<QString::fromUtf8("ú").at(0); simpleL<<'u';
        letters<<QString::fromUtf8("ü").at(0); simpleL<<'u';
        letters<<QString::fromUtf8("í").at(0); simpleL<<'i';
        letters<<QString::fromUtf8("Á").at(0); simpleL<<'A';
        letters<<QString::fromUtf8("ç").at(0); simpleL<<'c';
        letters<<QString::fromUtf8("ª").at(0); simpleL<<'a';
    }

    for(int i=0;i<letters.size();i++)
        name.replace(letters.at(i), simpleL.at(i));
*/
    //qDebug()<<QString::fromUtf8("c").at(0).decomposition().length();

    
    name.remove(QString::fromUtf8("¡"));
    name.remove('!');
    name.remove(QString::fromUtf8("¿"));
    name.remove('?');

    for(int i=0;i<name.size();i++)
    {
        const QString& decomp=name.at(i).decomposition();
        if (decomp.length()==2)
            name[i]=decomp.at(0);
    }

    return name;
}

QString simplify(QString name)
{
    name=deaccent(name).simplified();
    static QVector<QChar> letters;
    if (letters.isEmpty())
    {
        letters<<'.';
        letters<<',';
        letters<<'!';
        letters<<'(';
        letters<<')';
        letters<<'"';
        letters<<'\'';
#ifdef TRIMSPACES
        letters<<' ';
#endif
    }

    for(int i=0;i<letters.size();i++)
        name.remove(letters.at(i));

    return name.toLower();
}
