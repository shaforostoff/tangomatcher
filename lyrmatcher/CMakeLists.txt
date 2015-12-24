cmake_minimum_required(VERSION 2.8)

if(WIN32)
    add_definitions ( /Zc:wchar_t-)
endif(WIN32)

project(lyrmatcher)
find_package(Qt4 REQUIRED)

set(QT_USE_QTXML TRUE)
set(QT_USE_QTGUI TRUE)

include(${QT_USE_FILE})
add_definitions(${QT_DEFINITIONS})

set(lyrmatcher_SOURCES main.cpp mainwindow.cpp utils.cpp)
set(lyrmatcher_HEADERS mainwindow.h )
set(lyrmatcher_FORMS mainwindow.ui  )

include_directories( . )

qt4_wrap_cpp(lyrmatcher_HEADERS_MOC ${lyrmatcher_HEADERS})
qt4_wrap_ui(lyrmatcher_FORMS_HEADERS ${lyrmatcher_FORMS})

add_executable(lyrmatcher ${lyrmatcher_SOURCES} 
    ${lyrmatcher_HEADERS_MOC} 
    ${lyrmatcher_FORMS_HEADERS} 
    )


IF (WIN32)
    include_directories( ../taglib ../taglib/taglib ../taglib/taglib/mpeg/id3v2 ../taglib/taglib/toolkit ../taglib/build )
    target_link_libraries(lyrmatcher ${QT_LIBRARIES} ../../taglib/build/taglib/tag )
    add_definitions(-DTAGLIB_STATIC)
ELSE (WIN32)
    target_link_libraries(lyrmatcher ${QT_LIBRARIES} -ltag)
ENDIF(WIN32)

