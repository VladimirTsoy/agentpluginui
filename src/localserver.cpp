#include "localserver.h"

#include <QDataStream>
#include "Deps/datastream.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>

#define MAX_FILES_SENT 10
// NOTE: если будет возможность отправлять несколько файлов на анализ (хэш/файлы)
//  то тут как раз и привяжем к этому значению
// (+ т.о.избегаем разростания списка на отправку)

/// NOTE: пока так
/// это в CMakeLists.txt ИЛИ в settings.txt
// можно задать несколько каталогов/путей через разделитель
#define DIR_LIST_SEPARATOR ";"
#define LOCAL_SYSTEM_DIR "/home/poddubsky/_tmp;/home/poddubsky/windows;/home/poddubsky/Working_dir"
#define FILES_MASK_TO_SCAN "";


LocalServer::LocalServer(QString serverName, QObject* parent)
    : QObject(parent), nextBlockSize(0),
      recursiveScan(true), scanExecutable(true), maxFilesSent(MAX_FILES_SENT)
{
//    settings = AthenaSettings::getSettings();
    initAthenaSettings("settings.txt");

    // Если пред.вариант завершился без удаления сервера
    // чистим его при новом запуске (файл в системе)
    QLocalServer::removeServer(serverName);

    server = new QLocalServer();
    server->setSocketOptions(QLocalServer::WorldAccessOption);
    if(!server->listen("AthenaLocalServer"))
    {
        qDebug() << "Server error " << "Unable to start server: " << server->errorString();
        server->close();
        return;
    }
    qDebug() << "\n" << "AthenaLocalServer started successfully!";

    connect(server, &QLocalServer::newConnection,
            this, &LocalServer::slotNewConnection);

    /// NOTE: вот это важная штука! т.к. сокеты крутятся в своих потоках
    /// напрямую дёргать запись в них из этого сервера нельзя
    /// - будет ошибка "socket notifiers cannot be enabled from another thread"
    /// НО! вызывая метод через signal-slot connection там автоматом происходит
    ///     вызов записи в нужном потоке (done)
    connect(this, &LocalServer::signalSendData,
            this, &LocalServer::slotSendInfo);

    connect(this, &LocalServer::signalScanSystem,
            this, &LocalServer::slotScanSystem);
}

LocalServer::~LocalServer()
{
}

void LocalServer::slotNewConnection()
{
//qDebug() << "== slotNewConnection()";
    QLocalSocket* localSocket = server->nextPendingConnection();
    clientConnections.append(localSocket);

    connect(localSocket, &QLocalSocket::disconnected,
            this, &LocalServer::deleteClient);
    connect(localSocket, &QLocalSocket::readyRead,
            this, &LocalServer::slotReadClient);

    sendMsgToClient(localSocket, "Server response: Connected!");

    // отправить клиенту информацию по плагинам на отображение
    sendPluginsInfo(localSocket);

    // выгрузить БД клиенту
    getAllFilesFromDB(localSocket);
}

// Удаляем сокет из списка рассылки при его отключении
void LocalServer::deleteClient()
{
    QObject* obj = QObject::sender();
    QLocalSocket* localClient = qobject_cast<QLocalSocket*>(obj);
    clientConnections.removeOne(localClient);
    localClient->deleteLater();
}

void LocalServer::slotSendInfo(QLocalSocket* localSocket, QByteArray arrayBlock)
{
    localSocket->write(arrayBlock);
}


void LocalServer::setConnection(CoreApi * _core)
{
    agentAPI = _core;
}

/** Тут отправляем всё клиенту, а там уже отображаем
 *
 *  Можно отправлять как msg_plugin_info, а можно как json
 *  (вообще всё можно слать как json...)
 */
void LocalServer::sendPluginsInfo(QLocalSocket* localSocket)
{
    map<string, short> pluginsInfo = agentAPI->getPluginsInfo();

    map<string, short>::iterator it;
    for (it = pluginsInfo.begin(); it != pluginsInfo.end(); ++it)
    {
        qDebug() << ">> plugin_name: " << QString::fromStdString(it->first)
                 << " | status=" << it->second;

        sendPluginInfo(localSocket,
                       QString::fromStdString(it->first),
                       it->second);
    }
}

/*!
 * \brief LocalServer::connectToDB - подключение к БД
 */
void LocalServer::connectToDB()
{
//qDebug() << "== connectToDB()";
    std::string a = "agent.db";
    database = static_cast<Database*>(agentAPI->getPluginDB(a));
}


/*!
 * \brief LocalServer::getAllFilesFromDB - метод загрузки всего из БД в модель AgentGUI
 * \param localSocket - сокет текщуего клиента
 *
 * Выполняется каждый раз при подключении нового клиента
 * (ему загружается вся локальная БД)
 */
void LocalServer::getAllFilesFromDB(QLocalSocket* localSocket)
{
//qDebug() << "== getAllFilesFromDB()";
    std::string query = "SELECT sha256, status, verdict.verdict, filepath.path, web_id.web_id, time FROM files\
            JOIN filepath ON files.id = filepath.id\
            JOIN verdict ON files.verdict = verdict.id\
            JOIN web_id ON files.id = web_id.id\
            ORDER BY time";

    table_type files;

    int rec = database->execSQLQuery_raw(query.c_str(), callbackDB, &files);

    File file;
    QUrl url;

sendMsgToClient(localSocket,
                "Server response: databse | files.size=" + QString::number(files.size())
                + " rec=" + QString::number(rec));

    // Тут по циклу перебрасываем все строки из БД - через сервер-клиент - в GUI
    // NOTE: слать не по одному файлу - а всей пачкой
    if (rec == 0 && files.size() > 0)
    {
        for(int i = 0; i < files.size(); i++ )
        {
            file.hash = files.at(i).at(0).data();
            file.status = std::atoi(files.at(i).at(1).data());
            file.verdict = files.at(i).at(2).data();
            file.path = files.at(i).at(3).data();
            file.webid = std::atoi(files.at(i).at(4).data());
            url.setUrl(QString::fromStdString(file.path));
            file.name = url.fileName().toStdString();
            file.time = files.at(i).at(5).data();

            if(file.status != 11 && file.status != -1)
            {
                sendFile(localSocket, file); // отправка команды в GUI
                agentAPI->notify("new_file", file.toJSON());
            }
            else
            {
                sendFile(localSocket, file); // отправка команды в GUI
            }
        }
    }
}

// Рассылка всем
void LocalServer::sendToAllClients(const QByteArray &data)
{
//qDebug() << "== sendToAllClients()";
    if(clientConnections.isEmpty()) {
        qDebug() << "clientConnections is empty...";
        return;
    }

    foreach(QLocalSocket* clientConnection, clientConnections) {
        qDebug() << "write to:  " << clientConnection
                << " | descriptor:" << clientConnection->socketDescriptor();
        emit signalSendData(clientConnection, data);
    }
}

// Слот чтения информации от клиента
void LocalServer::slotReadClient()
{
//qDebug() << "== slotReadClient()";
    message_head head;
    QString str_info;

    File file;

    QLocalSocket* localSocket = (QLocalSocket*)sender();
    QDataStream in(localSocket);
    in.setVersion(QDataStream::Qt_5_3);

    QString pluginName;
    QString json;
    QString command;

    for(;;)
    {
        if(!nextBlockSize)
        {
            if(localSocket->bytesAvailable() < (int)sizeof(quint16))
                break;
            in >> nextBlockSize;
        }
        if(nextBlockSize < sizeof(message_head)) {
            qDebug() << "socket_client: Wrong package.";
            break;
        }

        in >> head;
        switch(head.type)
        {
        case msg_string:
            in >> str_info;
            qDebug() << str_info;
            break;
        case msg_file: // файл на проверку
            in >> file;

            files.push_back(file);
            /// NOTE: непонятное действие - тут файлы попадают по одному и так

            for(int i = 0; i < files.size(); ++i)
            {
                QJsonDocument doc;
                QJsonObject obj;
/*
                obj["id"] = files[i].id;
                obj["name"] = QString::fromUtf8(files[i].name.data());
                obj["path"]= QString::fromUtf8(files[i].path.data());
                obj["hash"]= QString::fromUtf8(files[i].hash.data());
                obj["status"]= files[i].status;
                obj["time"]= QString::fromUtf8(files[i].time.data());
                obj["verdict"]= QString::fromUtf8(files[i].verdict.data());
                obj["webid"] = files[i].webid;
*/
                obj["id"] = files[i].id;
                obj["name"] = QString::fromStdString(files[i].name);
                obj["path"]= QString::fromStdString(files[i].path);
                obj["hash"]= QString::fromStdString(files[i].hash);
                obj["status"]= files[i].status;
                obj["time"]= QString::fromStdString(files[i].time);
                obj["verdict"]= QString::fromStdString(files[i].verdict);
                obj["webid"] = files[i].webid;

                doc.setObject(obj);

                //std::cout<< "\nMy json: " << QString::fromUtf8(doc.toJson(QJsonDocument::Compact)).toLocal8Bit().data()<< endl;
                agentAPI->notify("new_file", QString(doc.toJson(QJsonDocument::Compact)).toStdString());
            }
            files.clear();
            break;
        case msg_json:
            // такого не получаем, только передаём
            break;
        case msg_cmd:
            in >> command;
            if(command == "scan_system") {
                qDebug() << "Server: msg_cmd=scan_system";
                emit signalScanSystem();
            }
            break;
        }

//         Преобразуем полученные данные и показываем их в виджете
//        QString message = time.toString() + " " + "Client has sent - " + string;
//        textEdit->append(message);

        nextBlockSize = 0;
        // Отправляем ответ клиенту
//        sendMsgToClient(localSocket, "Server response: received \"" + string + "\"");
    }
}

void LocalServer::sendPluginInfo(QLocalSocket* localSocket,
                                 QString pluginName, short pluginState)
{
sendMsgToClient(localSocket, "Server response: sendPluginInfo");
//qDebug() << "== sendPluginInfo()";

        message_head head;
        head.type = msg_plugin_info;;
        head.sz_body = sizeof(pluginName) + sizeof(pluginState);

        QByteArray arrayBlock;
        QDataStream out(&arrayBlock, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_5_3);

        out << quint16(0);
        out << head;
        out << pluginName;
        out << pluginState;

        out.device()->seek(0);
        out << quint16(arrayBlock.size() - sizeof(quint16));

        emit signalSendData(localSocket, arrayBlock);
}

void LocalServer::sendFile(QLocalSocket* localSocket, const File& file)
{
sendMsgToClient(localSocket, "Server response: sendFile");
//qDebug() << "== sendFile()";
    message_head head;
    head.type = msg_file;;
    head.sz_body = sizeof(file);

    QByteArray arrayBlock;
    QDataStream out(&arrayBlock, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_3);

    out << quint16(0);
    out << head;
    out << file;

    out.device()->seek(0);
    out << quint16(arrayBlock.size() - sizeof(quint16));

    if(localSocket)
//         localSocket->write(arrayBlock);
        emit signalSendData(localSocket, arrayBlock);
    else
        sendToAllClients(arrayBlock);
}

/// А вот тут - рассылка, пока  - всем одинаково
/// НО! можно подумать, чтобы всем не слать файлы, которые они не закидывали
void LocalServer::sendInfo(std::string pluginName, std::string json)
{
//qDebug() << "== sendInfo()";
    QString pluginName_q = QString::fromStdString(pluginName);
    QString json_q = QString::fromStdString(json);

    message_head head;
    head.type = msg_json;
    head.sz_body = sizeof(pluginName_q) + sizeof(json_q);

    QByteArray arrayBlock;
    QDataStream out(&arrayBlock, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_3);

    out << quint16(0);
    out << head;
    out << pluginName_q;
    out << json_q;

    out.device()->seek(0);
    out << quint16(arrayBlock.size() - sizeof(quint16));

    sendToAllClients(arrayBlock);
}

// * время сообщения не ставлю
void LocalServer::sendMsgToClient(QLocalSocket* localSocket, const QString& info_string)
{
//qDebug() << "== sendMsgToClient()";
    message_head head;
    head.type = msg_string;
    head.sz_body = sizeof(info_string);

    QByteArray arrayBlock;
    QDataStream out(&arrayBlock, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_3);

    out << quint16(0);
    out << head;
    out << info_string;

    out.device()->seek(0);
    out << quint16(arrayBlock.size() - sizeof(quint16));

    // Отправляем получившийся блок клиенту
    if(localSocket)
//        localSocket->write(arrayBlock);
        emit signalSendData(localSocket, arrayBlock);
    else
        sendToAllClients(arrayBlock);
}

void LocalServer::sendCommandToClient(const QString& cmd)
{
    message_head head;
    head.type = msg_cmd;
    head.sz_body = sizeof(cmd);

    QByteArray arrayBlock;
    QDataStream out(&arrayBlock, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_3);

    out << quint16(0);
    out << head;
    out << cmd;

    out.device()->seek(0);
    out << quint16(arrayBlock.size() - sizeof(quint16));

    // шлём всем на локальной машине
    sendToAllClients(arrayBlock);
}


/*!
 * \brief LocalServer::funcLoadScanParams - загружаем параметры для сканирования из settings.txt
 *
 * scan_dirs
 * scan_files_mask
 *
 */
void LocalServer::funcLoadScanParams()
{
    QString key;
    json answer;

    // рекурсивный поиск
    key = "scan_recursive";
    answer = getProperty(key.toStdString());
    if(answer == json::object())
        qDebug() << "[settings] Не найдено определение " << key;
    else
    {
        if(QString::fromStdString(answer.get<std::string>()) == "false")
            recursiveScan = false;
        else
            recursiveScan = true;
        qDebug() << "recursiveScan: " << (recursiveScan ? "true" : "false");
    }

    // рекурсивный поиск
    key = "scan_executable";
    answer = getProperty(key.toStdString());
    if(answer == json::object())
        qDebug() << "[settings] Не найдено определение " << key;
    else
    {
        if(QString::fromStdString(answer.get<std::string>()) == "false")
            scanExecutable = false;
        else
            scanExecutable = true;
        qDebug() << "scanExecutable: " << (scanExecutable ? "true" : "false");
    }

    // считываем каталоги, которые нужно сканировать
    key = "scan_dirs";
    answer = getProperty(key.toStdString());
    if(answer == json::object())
        qDebug() << "[settings] Не найдено определение " << key;
    else
    {
        localDirsToScan = QString::fromStdString(answer.get<std::string>());
        qDebug() << "localDirsToScan: " << localDirsToScan;
    }

    // считываем маску поиска для файлов (разрешение)
    key = "scan_files_mask";
    answer = getProperty(key.toStdString());
    if(answer == json::object())
        qDebug() << "[settings] Не найдено определение " << key;
    else
    {
        filesExtenions = QString::fromStdString(answer.get<std::string>());
        qDebug() << "scanning filesExtenions: " << filesExtenions;
    }

//-------------------------------
/*
    filesExtenions = FILES_MASK_TO_SCAN;
    qDebug() << "scanning filesExtenions: " << filesExtenions;

//    qDebug() << "Directories to scan: " << QDir(LOCAL_SYSTEM_DIR).path();
//    funcScanSystem(QDir(LOCAL_SYSTEM_DIR)); // QDir::current().absolutePath()
    localDirsToScan = LOCAL_SYSTEM_DIR;
    qDebug() << "str(LOCAL_SYSTEM_DIR): " << localDirsToScan;
*/
}

/*!
 * \brief LocalServer::slotScanSystem - сканирование системы
 *
 * перебираем целевой каталог
 * (если это системный - ОС (Win/Lin) и соотв.каталог - брать из CMakeLists)
 */
// NOTE: ещё можно добавить отображение - проверки файлов
//  сколько отправили / сколько вернулось с нормальным вердиктом
// всё это в полосе - закраска (она же кнопка)
// НО: большая проблема с тем - как хранить список (он может быть слишком большой)
void LocalServer::slotScanSystem()
{
    sendCommandToClient("scan_start");

    funcLoadScanParams();

    // NOTE: НО! таким образом мы не сможем сканировать всю систему (Debian)
    //  (если задать пустую строку - то будет просканировано всё)
    if(!localDirsToScan.isEmpty())
    {
        QStringList listDir = localDirsToScan.split(DIR_LIST_SEPARATOR); // разделяем пробелом
        qDebug() << "list: " << listDir;

        // * если не задано (будет пустая строка) то просто всё проскочим
        foreach (QString subdir, listDir)
        {
            if (subdir == "." || subdir == "..")
            {
                continue;
            }
            funcScanSystem(QDir(QDir::fromNativeSeparators(subdir)));
        }

        sendScanFiles();
        filesToScan.clear();
    }

    sendCommandToClient("scan_complete");
}

/**
 * Идея держать список и как только по нему будут вердикты - провальная
 *  - такой список держать накладно.
 * Если что - просто будет слаться всё повторно, но тут уже будет ответ
 *  - такой файл уже есть
 **/
void LocalServer::addFileToScanList(const QString &file)
{
    filesToScan.append(file);
    if(filesToScan.size() >= maxFilesSent)
    {
qDebug() << "PRINT";
        sendScanFiles();
        filesToScan.clear();
    }
}

void LocalServer::sendScanFiles()
{
/// TEST: пишем в файл список
    QFile file("file.txt");
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&file);
    foreach (QString one_file, filesToScan)
    {
        out << one_file << "\n";
        // СЮДА: перетащить содержимое функции Check()
        check(one_file);
    }
    file.close();
}

// рекурсивно перебираем целевой каталог
void LocalServer::funcScanSystem(const QDir& dir)
{
qDebug() << "funcScanSystem: dir=" << dir.absolutePath();
    QCoreApplication::processEvents(); // чтобы не вещать остальные события

// NOTE: ::entryList() returns an empty list if the directory is
//      unreadable, does not exist, or if nothing matches the specification.

    // QDir::Filters(files/dir) + nameFilters
    QStringList listFiles;
    QFileInfo infoFile;

    // Сначала прогоним все исполняемые файлы (isExecutable)
    if(scanExecutable)
    {
        listFiles = dir.entryList(QDir::Files);
        if(listFiles.isEmpty())
            qDebug() << "can't open Dir (files): " << dir.absolutePath();
        foreach (QString file, listFiles)
        {
            infoFile.setFile(file);
            if(infoFile.isExecutable())
                addFileToScanList(dir.absoluteFilePath(file));
        }
        listFiles.clear(); // на всякий
    }

    // потом все что совпадают по маске (если маска задана)
    if(!filesExtenions.isEmpty()) {
        listFiles = dir.entryList(filesExtenions.split(" "), QDir::Files);

        if(listFiles.isEmpty())
            qDebug() << "can't open Dir (files): " << dir.absolutePath();
        foreach (QString file, listFiles)
        {
            addFileToScanList(dir.absoluteFilePath(file));
        }
    }

    if(!recursiveScan) // добавим возможность контролировать рекусривное сканирование
        return;

    // потом уже перебираем каталоги
    QStringList listDir = dir.entryList(QDir::Dirs);
    if(listDir.isEmpty())
        qDebug() << "can't open Dir (dirs): " << dir.absolutePath();
    foreach (QString subdir, listDir)
    {
        if (subdir == "." || subdir == "..")
        {
            continue;
        }
        funcScanSystem(QDir(dir.absoluteFilePath(subdir)));
    }
}

void LocalServer::check(const QString& strFile)
{
    File file;
    QFileInfo info;

qDebug() << "CHECK > strFile: " << strFile;
    QUrl tmpUrl(strFile);
qDebug() << "CHECK > tmpUrl: " << tmpUrl.fileName();
    info.setFile(strFile);
qDebug() << "CHECK > info: " << info.fileName();
qDebug() << "CHECK > info: " << info.absoluteFilePath();
    // Отправлять можем только файлы
    // закидывать каталоги с вложениями - не пролучится
    if(info.isDir()) {
        qDebug() << "Невозможно отправить каталог (только файлы)" << info.fileName();
        return;
    }

    if(info.size() >= 300*1024*1024)
    {
        qDebug() << "Файл недопустимого размера. "
                << "Файл " + info.fileName() << " слишком большой. Максимальный размер 300мб";
        return;
    }
    else if(info.size() <= 0)
    {
        qDebug() << "Файл недопустимого размера. "
                << "Файл " <<  info.fileName() << " пуст.";
        return;
    }
    else
    {
        // WARNING: в path - нужно отправлять путь+имя_файла (!)
        //  практика показала, что имя_файла в web'е - берётся отсюда
        file.path = info.filePath().toStdString();
        QFile tmpfile(strFile);

        if(tmpfile.open(QFile::ReadOnly))
        {
            QCryptographicHash h(QCryptographicHash::Sha256);

            if(h.addData(&tmpfile))
            {
                file.hash = QString(h.result().toHex()).toStdString();
            }
        }

        file.name = info.fileName().toStdString();
        file.status = 1;
        file.verdict = "Undefined";
        file.time = QDateTime::currentDateTime().toString(Qt::DateFormat::ISODateWithMs).toStdString();

        // готовый file перерабатываем в json и отправляем
        QJsonDocument doc;
        QJsonObject obj;

        obj["id"] = file.id = -1;
        obj["name"] = QString::fromStdString(file.name);
        obj["path"]= QString::fromStdString(file.path);
        obj["hash"]= QString::fromStdString(file.hash);
        obj["status"]= file.status;
        obj["time"]= QString::fromStdString(file.time);
        obj["verdict"]= QString::fromStdString(file.verdict);
        obj["webid"] = file.webid = -1;

        doc.setObject(obj);
qDebug() << doc;
        // на уникальность - там уже проверяется
/// TEST: для теста  - блокируем отправку, чтобы не слать всё в web при тестировании
        agentAPI->notify("new_file", QString(doc.toJson(QJsonDocument::Compact)).toStdString());
        // NOTE: можно тут же слать до кучи и в GUI
    }
}

//===================================================================
json LocalServer::getProperty(const string& name) const
{
    if (_j.find(name) == _j.end())
        return json::object();
    return _j[name];
}

//get full name of <name> file in app_data folder
std::string LocalServer::GetFullName(const std::string &name)
{
    std::string AppData_Path = SHARED_APP_DATA_PATH;

    boost::filesystem::path full_name = AppData_Path;
    full_name += "/AVSoft/AthenaAgent";

    if ( !boost::filesystem::is_directory(full_name) )
    {
        try {
            if (!boost::filesystem::create_directories(full_name)) {
                std::cerr << "Failed to create directory " << full_name << ".\n";
                return name;
            }
        }
        catch(const boost::filesystem::filesystem_error& e) {
            if(e.code() == boost::system::errc::permission_denied)
                std::cout << "Search permission is denied for one of the directories "
                          << "in the path prefix of " << full_name << "\n";
            else
                std::cout << "is_directory(" << full_name << ") failed with "
                          << e.code().message() << '\n';
            return name;
        }
    }

    full_name += "/" + name;
    return full_name.string();
}

void LocalServer::initAthenaSettings(const string file_name)
{
    string full_name = GetFullName(file_name); //e.g C:/ProgramData/AVSoft/AthenaAgent/settings.txt
    std::ifstream binary_file(full_name.c_str(), std::ios_base::binary);
    if (binary_file)
    {
        binary_file >> _j;
    }
    else
    {
        qDebug() << "Loading scan settings: there is no settings.txt.";
    }
    binary_file.close();
}
