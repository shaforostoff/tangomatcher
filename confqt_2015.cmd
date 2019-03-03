@echo off

REM set prefix=%~dp0
configure.bat -debug-and-release -opensource -confirm-license -prefix C:/Qt/Qt-5.9-static ^
	-static -nomake tests -nomake examples -no-accessibility  ^
	-platform win32-msvc2015 -mp ^
	-no-gif -no-ico -no-libjpeg -no-libpng ^
	-no-sqlite -no-sql-odbc -no-sql-psql ^
	-no-openssl -no-cups -no-opengl -no-freetype -no-fontconfig -no-harfbuzz -no-qml-debug -no-dbus -no-system-proxies ^
	-no-feature-colordialog -no-feature-datetimeedit -no-feature-calendarwidget ^
	-no-feature-printer -no-feature-printdialog -no-feature-printpreviewdialog -no-feature-printpreviewwidget ^
	-no-feature-picture -no-feature-pdf -no-feature-textodfwriter -no-feature-dial ^
	-no-feature-cssparser -no-feature-style-stylesheet -no-feature-style-fusion -no-feature-paint_debug -no-feature-graphicseffect ^
	-no-feature-lcdnumber -no-feature-whatsthis  -no-sm -no-feature-movie ^
	-no-feature-sql -no-feature-testlib -no-feature-network