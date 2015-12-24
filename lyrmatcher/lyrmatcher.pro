#-------------------------------------------------
#
# Project created by QtCreator 2012-06-06T01:26:21
#
#-------------------------------------------------

QT       += core gui xml

TARGET = lyrmatcher
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp\ 
        utils.cpp

HEADERS  += mainwindow.h

FORMS    += mainwindow.ui 

win32: DEFINES += TAGLIB_STATIC

unix: LIBS += -ltag
win32: LIBS += ../taglib/build/taglib/tag.lib
win32: INCLUDEPATH += ../taglib ../taglib/taglib ../taglib/taglib/mpeg/id3v2 ../taglib/taglib/toolkit ../taglib/build
#win32: INCLUDEPATH += ../taglib/include ../taglib/build


 
