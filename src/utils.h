#ifndef UTILS_H
#define UTILS_H

#include <QString>

#include <taglib/fileref.h>
#include <taglib/tag.h>

//#define TRIMSPACES 1

QString t2q(const TagLib::String& t);
TagLib::String q2t(const QString& q);

QString extractDistinguisher(const QString& name);

QString deaccent(QString name);
QString simplify(QString name);




#endif
