/* ****************************************************************************
  Copyright (C) 2015 by Nick Shaforostoff <shafff@ukr.net>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of
  the License or (at your option) version 3 or any later version
  accepted by the membership of KDE e.V. (or its successor approved
  by the membership of KDE e.V.), which shall act as a proxy 
  defined in Section 14 of version 3 of the license.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

**************************************************************************** */



#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QWindow>
#include <QScreen>
#include <QDebug>
#include <QString>
#include <QStringBuilder>
#include <QFile>
#include <QFileInfo>
#include <QDesktopWidget>
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QNetworkInterface>
#include <QDataStream>
#include <QTcpSocket>
#include <QTcpServer>


class Settings: public QWidget
{
public:
	Settings()
	{
		colorEdit = new QLineEdit(this);
		addressEdit = new QLineEdit(this);

		QFormLayout* l = new QFormLayout(this);
		l->addRow(QStringLiteral("Text color:"), colorEdit);
		l->addRow(QStringLiteral("Address:"), addressEdit);
	}
	QLineEdit* colorEdit;
	QLineEdit* addressEdit;
};

QTcpSocket* tcpSocket = 0;

class TangoNotifier: public QWidget
{
public:
	TangoNotifier()
	{
		QPalette p = palette();
		p.setColor(QPalette::Window, Qt::black);
		setPalette(p);
		pictureLabel = new QLabel(this);
		pictureLabel->setAlignment(Qt::AlignHCenter);
		label = new QLabel(this);
		label->setTextFormat(Qt::RichText);
		label->setText(QStringLiteral("<font size=7><br/>TTVTTM</font>"));
		QVBoxLayout* l = new QVBoxLayout(this);
		l->addWidget(pictureLabel, 1024);
		l->addWidget(label, 0, Qt::AlignCenter);
		resize(400, 400);
	}
	void sendData()
	{
		QDataStream out(tcpSocket);
		out.setVersion(QDataStream::Qt_5_4);
		out<<label->text();
		out<<imageFn;

	}
	QLabel* pictureLabel;
	QLabel* label;
	QString imageFn;
};
Settings* settings = 0;
TangoNotifier* welcome = 0;
QList<QString> lastGenres;
QColor textColor;


QString deaccent(QString name)
{
    for(int i=0;i<name.size();i++)
    {
        const QString& decomp=name.at(i).decomposition();
        if (decomp.length()==2)
            name[i]=decomp.at(0);
    }

    return name;
}
QString shortGenre(QString genre)
{
	genre = genre.toLower();
	if (genre.contains(QLatin1String("tangon")))	return QStringLiteral("M");
	if (genre.contains(QLatin1String("tango")))		return QStringLiteral("T");
	if (genre.contains(QLatin1String("vals")))		return QStringLiteral("V");
	if (genre.contains(QLatin1String("milong")))	return QStringLiteral("M");
	if (genre.contains(QLatin1String("nuev")))		return QStringLiteral("M");
	return QString();
}

QString previousGenre(int skip)
{
	int i=lastGenres.size();
	while (--i>=0)
	{
/*
		bool cortina = !lastGenres.at(i).contains("tango")
					&& !lastGenres.at(i).contains("vals")
					&& !lastGenres.at(i).contains("nuev")
					&& !lastGenres.at(i).contains("milonga");
					*/
		bool cortina = lastGenres.at(i).isEmpty();
		if (!cortina)
			continue;
		if (skip-- == 0)
			return i+1<lastGenres.size()?lastGenres.at(i+1):QString();
	}
	return QString();
}


#ifdef Q_OS_WIN
#include <windows.h>
#define PLAYBACK_STATE_MESSAGE 13
char sentPath[256];
COPYDATASTRUCT MyCDS;

PCOPYDATASTRUCT pMyCDS;
LONG_PTR WINAPI windowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_COPYDATA)
    {
        pMyCDS = (PCOPYDATASTRUCT)lParam;
        if (pMyCDS->dwData==PLAYBACK_STATE_MESSAGE)
        {
			if (strcmp(sentPath, (char*)pMyCDS->lpData)==0) return 0;
			qstrncpy(sentPath, (char*)pMyCDS->lpData, 250);
			QString text = QString::fromUtf8((char*)pMyCDS->lpData);
			if (!text.contains('|'))
				return 0;

			int barPos = text.indexOf(QLatin1String(" | "));
			QString genre = shortGenre(text.mid(3 + barPos));
			if (genre.length() || lastGenres.last().length())
				lastGenres << genre;
			if (lastGenres.size()>30)
				lastGenres.removeFirst();

			if (genre.isEmpty()) return 0;
			QString color = settings->colorEdit->text();
			if (color.isEmpty())
				color = "white";
			welcome->label->setText(QLatin1String("<font color=") % color % QLatin1String(" size=7><center>") % text.leftRef(barPos) % QLatin1String("<br/>")
				% previousGenre(5)
				% previousGenre(4)
				% previousGenre(3)
				% previousGenre(2)
				% previousGenre(1)
				% QLatin1String("<b>") % previousGenre(0) % QLatin1String("</b>")
				% QLatin1String("</center></font>"));


			QString fn = text.leftRef(text.indexOf(QLatin1String(" - "))) % QLatin1String(".jpg");
			fn = deaccent(fn);
			if (!QFile::exists(fn))
				fn = QStringLiteral("default.jpg");
			if (QFile::exists(fn))
				welcome->pictureLabel->setPixmap(QPixmap(fn).scaled(welcome->pictureLabel->size(), Qt::KeepAspectRatio));
			else
				welcome->pictureLabel->setPixmap(QPixmap());

			welcome->imageFn = fn;
		    tcpSocket->abort();
			tcpSocket->connectToHost(settings->addressEdit->text(), 1945);
        }
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}
#endif

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    QCoreApplication::setApplicationName(QStringLiteral("TangoNotifier"));
    QCoreApplication::setApplicationVersion("1.0");
    QCoreApplication::setOrganizationName(QStringLiteral("TangoAltruismo"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tangoaltruismo.zz.mu"));
    parser.process(app);

	lastGenres << QString();
#ifdef Q_OS_WIN
	sentPath[0]=0;
    TCHAR gClassName[100];
    wsprintf(gClassName, TEXT("TangoNotifier"));

	WNDCLASS windowClass;
    windowClass.style = CS_GLOBALCLASS | CS_DBLCLKS;
    windowClass.lpfnWndProc = windowProc;
    windowClass.cbClsExtra  = 0;
    windowClass.cbWndExtra  = 0;
    windowClass.hInstance   = (HINSTANCE) GetModuleHandle(NULL);
    windowClass.hIcon = 0;
    windowClass.hCursor = 0;
    windowClass.hbrBackground = 0;
    windowClass.lpszMenuName  = 0;
    windowClass.lpszClassName = gClassName;
    RegisterClass(&windowClass);
    HWND responder = CreateWindow(gClassName, L"TangoNotifier", 0, 0, 0, 10, 10, 0, (HMENU)0, (HINSTANCE)GetModuleHandle(NULL), 0);
#endif
	welcome=new TangoNotifier();
	if (QApplication::desktop()->screenCount()>1)
	{
		welcome->show();
	    welcome->windowHandle()->setScreen(QApplication::screens().last());
	    welcome->move(QApplication::screens().last()->geometry().topLeft());
		welcome->showFullScreen();
	}
	else
		welcome->show();
	
	settings = new Settings();
    settings->move(QApplication::desktop()->screen()->rect().center() - settings->rect().center());
    settings->show();


    QString ipAddress;
    QList<QHostAddress> ipAddressesList = QNetworkInterface::allAddresses();
    // use the first non-localhost IPv4 address
    for (int i = 0; i < ipAddressesList.size(); ++i) {
        if (ipAddressesList.at(i) != QHostAddress::LocalHost &&
            ipAddressesList.at(i).toIPv4Address()) {
            ipAddress = ipAddressesList.at(i).toString();
            break;
        }
    }
    // if we did not find one, use IPv4 localhost
    if (ipAddress.isEmpty())
        ipAddress = QHostAddress(QHostAddress::LocalHost).toString();
    settings->addressEdit->setText(ipAddress);

	tcpSocket = new QTcpSocket();
	QObject::connect(tcpSocket, &QTcpSocket::connected, welcome, &TangoNotifier::sendData);

	int code=app.exec();

#ifdef Q_OS_WIN
    DestroyWindow(responder);
#endif
    return code;
}


