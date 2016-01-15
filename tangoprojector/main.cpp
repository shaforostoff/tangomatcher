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
#include <QPushButton>
#include <QWindow>
#include <QScreen>
#include <QDebug>
#include <QString>
#include <QStringBuilder>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QDesktopWidget>
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QDataStream>
#include <QTcpSocket>
#include <QTcpServer>

class Settings;
class TangoNotifier;

QPointer<Settings> settings = 0;
QPointer<TangoNotifier> welcome = 0;
QList<QString> lastGenres;
QColor textColor;

QTcpServer* tcpServer;

class Settings: public QWidget
{
public:
	Settings()
	{
	//	QPushButton* reset = new QPushButton(QStringLiteral("Reset tanda history"), this);
	//	connect(reset, &QPushButton::clicked, [](){lastGenres.clear();lastGenres<<QString();});

		addressEdit = new QLineEdit(this);
		addressEdit->setText(QStringLiteral("white"));
		QFormLayout* l = new QFormLayout(this);
		l->addRow(QStringLiteral("Address:"), addressEdit);
	//	l->addRow(reset);
	}
	QLineEdit* addressEdit;
};


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

	void receiveData()
	{
		QTcpSocket *clientConnection = tcpServer->nextPendingConnection();
		clientConnection->waitForReadyRead();
		QDataStream in(clientConnection);
		in.setVersion(QDataStream::Qt_5_4);
		QString text;
		in>>text;
		label->setText(text);
		QString imageFn;
		in>>imageFn;
		if (QFile::exists(imageFn))
		{
			welcome->pictureLabel->setPixmap(QPixmap(imageFn).scaled(welcome->pictureLabel->size(), Qt::KeepAspectRatio));
		}
	}

	QLabel* pictureLabel;
	QLabel* label;
};


int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    QCoreApplication::setApplicationName(QStringLiteral("TangoProjector"));
    QCoreApplication::setApplicationVersion("1.0");
    QCoreApplication::setOrganizationName(QStringLiteral("TangoAltruismo"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tangoaltruismo.zz.mu"));
    parser.process(app);

	lastGenres << QString();

	welcome=new TangoNotifier();
	if (QApplication::desktop()->screenCount()>1)
	{
		welcome->show();
	    welcome->windowHandle()->setScreen(QApplication::screens().last());
	    //welcome->move(QApplication::screens().last()->geometry().topLeft());
	}
	welcome->showFullScreen();
	
	settings = new Settings();
    settings->move(QApplication::desktop()->screen()->rect().center() - settings->rect().center());
    settings->show();



	tcpServer = new QTcpServer();
	QObject::connect(tcpServer, &QTcpServer::newConnection, welcome, &TangoNotifier::receiveData);

	if (!tcpServer->listen(QHostAddress::Any, 1945)) {
        QMessageBox::critical(0, QString("Fortune Server"),
                              QString("Unable to start the server: %1.")
                              .arg(tcpServer->errorString()));
        tcpServer->close();
        return 0;
    }

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
	
	
	int code=app.exec();

    return code;
}


