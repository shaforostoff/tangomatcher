cmake_minimum_required(VERSION 2.8)


project(tangomatcher)
find_package(Qt5Widgets)
find_package(Qt5Xml)

set(QT_USE_QTXML TRUE)
set(QT_USE_QTGUI TRUE)

set(tangomatcher_SOURCES main.cpp mainwindow.cpp diff.cpp utils.cpp jobs.cpp ../bpmcounter/beatcounter.cpp)
set(tangomatcher_HEADERS mainwindow.h jobs.h ../bpmcounter/beatcounter.h)
set(tangomatcher_FORMS mainwindow.ui tagscheme.ui )

include_directories( .  )
include_directories( ${CMAKE_BINARY_DIR} )

qt5_wrap_cpp(tangomatcher_HEADERS_MOC ${tangomatcher_HEADERS})
qt5_wrap_ui(tangomatcher_FORMS_HEADERS ${tangomatcher_FORMS})

add_executable(tangomatcher ${tangomatcher_SOURCES} 
    ${tangomatcher_HEADERS_MOC} 
    ${tangomatcher_FORMS_HEADERS} 
    )


IF (WIN32)
include_directories( ../taglib ../taglib/taglib ../taglib/taglib/mpeg/id3v2 ../taglib/taglib/toolkit ../taglib/build )
target_link_libraries(tangomatcher Qt5::Widgets Qt5::Xml ../taglib/build/taglib/tag )
add_definitions(-DTAGLIB_STATIC)
ELSE (WIN32)
target_link_libraries(tangomatcher Qt5::Widgets Qt5::Xml -ltag)
ENDIF(WIN32)
