#ifndef LOCALSERVER_H
#define LOCALSERVER_H


#include <QLocalServer>
#include <QLocalSocket>

#include <QJsonDocument>
#include <QJsonObject>

#include <QUrl>

#include <Plugin/agentpluginapi.h>
#include <Core/core_api.h>
#include <DB/database.h>

#include "Core/file_core.h"
#include "Core/file.h"
#include "Deps/message_head.h"

#include <QDir>

//#include <Settings/athena_settings.h>
//#include "athena_settings.h"

class LocalServer : public QObject
{
    Q_OBJECT

public:
    LocalServer(QString serverName, QObject* parent = 0);
    ~LocalServer();

    friend QDataStream& operator<<(QDataStream& stream,const message_head& head);
    friend QDataStream& operator>>(QDataStream& stream, message_head& head);

    friend QDataStream& operator<<(QDataStream& stream,const File& file);
    friend QDataStream& operator>>(QDataStream& stream, File& file);

    void setConnection(CoreApi *);
    void sendToAllClients(const QByteArray &data);

    void connectToDB();
    void getAllFilesFromDB(QLocalSocket* localSocket);

    void sendPluginsInfo(QLocalSocket* localSocket);
    void sendPluginInfo(QLocalSocket* localSocket, QString pluginName, short pluginState);

    void sendInfo(std::string pluginName, std::string json);
    void sendFile(QLocalSocket* localSocket = 0, const File& file = {});

private:
    CoreApi* agentAPI;
    Database* database;

    QList<File> files;

    QLocalServer* server;
    quint16 nextBlockSize;

    QList<QLocalSocket*> clientConnections;

//    AthenaSettings* settings;

    void sendMsgToClient(QLocalSocket* localSocket = 0, const QString& string = "");

    bool recursiveScan;
    bool scanExecutable;
    QString filesExtenions;
    QString localDirsToScan;
    const int maxFilesSent;
    QStringList filesToScan;

    void funcLoadScanParams();
    void funcScanSystem(const QDir &dir);
    void addFileToScanList(const QString &file);
    void sendScanFiles(); //const QStringList &list); // flush

    void sendCommandToClient(const QString& cmd);

    void check(const QString& strFile);

    //----------------------------------------
    json _j;

    json getProperty(const string& name) const;
    std::string GetFullName(const string& name);
    void initAthenaSettings(const string);

signals:
    void signalSendData(QLocalSocket*, QByteArray);
    void signalScanSystem();

public slots:
    virtual void slotNewConnection();
    void slotReadClient();

    void slotSendInfo(QLocalSocket*, QByteArray);

    void deleteClient(); // (QLocalSocket*);
private slots:
    void slotScanSystem();
};


#endif // LOCALSERVER_H
