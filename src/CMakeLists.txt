cmake_minimum_required(VERSION 2.8)


project(tangomatcher)
find_package(Qt4 REQUIRED)

set(QT_USE_QTXML TRUE)
set(QT_USE_QTGUI TRUE)

include(${QT_USE_FILE})
add_definitions(${QT_DEFINITIONS})

set(tangomatcher_SOURCES main.cpp mainwindow.cpp diff.cpp utils.cpp jobs.cpp)
set(tangomatcher_HEADERS mainwindow.h jobs.h )
set(tangomatcher_FORMS mainwindow.ui tagscheme.ui )

include_directories( . )

qt4_wrap_cpp(tangomatcher_HEADERS_MOC ${tangomatcher_HEADERS})
qt4_wrap_ui(tangomatcher_FORMS_HEADERS ${tangomatcher_FORMS})

add_executable(tangomatcher ${tangomatcher_SOURCES} 
    ${tangomatcher_HEADERS_MOC} 
    ${tangomatcher_FORMS_HEADERS} 
    )


IF (WIN32)
include_directories( ../taglib ../taglib/taglib ../taglib/taglib/mpeg/id3v2 ../taglib/taglib/toolkit ../taglib/build )
target_link_libraries(tangomatcher ${QT_LIBRARIES} ../taglib/build/taglib/tag )
add_definitions(-DTAGLIB_STATIC)
ELSE (WIN32)
target_link_libraries(tangomatcher ${QT_LIBRARIES} -ltag)
ENDIF(WIN32)
