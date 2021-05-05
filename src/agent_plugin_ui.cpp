#include <thread>
#include <iostream>

#include "agent_plugin_ui.h"

/// NOTE: можно выкинуть из этого проекта всё Qt ??
///     подобрать альтернативу в с++ (boost?)

AgentPluginUI::AgentPluginUI()
{
    printf("Creating AgentPluginUI\n");
    this->core = nullptr;
}

AgentPluginUI::~AgentPluginUI()
{
    this->core = nullptr;

    delete serverAgent;
    delete app;
}

void AgentPluginUI::setConnection(void *_core)
{
    this->core = static_cast<CoreApi*>(_core);
}

void AgentPluginUI::runPlugin()
{
    if (this->core == nullptr)
    {
        printf("Need connection to CoreApi!\n");
        return;
    }

    /// - стартует CoreApplication
    /// - в нём запускаем LocalServer
    /// - методы что были в mainFrom - теперь в LocalServer
    ///         + собственно, серверная часть
    std::thread th(createServer, this->core);
    th.detach();
    /// можно всё сделать на С'шных сокетах

    core->subscribePlugin(getName());
}

void AgentPluginUI::stopPlugin()
{
    printf("Try to stop plugin...\n");
}

const std::string AgentPluginUI::getName() const
{
    return  "ui";
}

int AgentPluginUI::getInfo(std::string *a)
{
    a->append("{ \"ui\":\"ui\" }");
    return 0;
}

void AgentPluginUI::setInfo(std::string pluginName, std::string json)
{
    // отправляем пакет на отображение
    if(serverAgent)
        serverAgent->sendInfo(pluginName, json);
}

boost::shared_ptr<AgentPluginApi> AgentPluginUI::create()
{
    return boost::shared_ptr<AgentPluginApi>(new AgentPluginUI());
}

int AgentPluginUI::createServer(CoreApi* _core)
{
    qRegisterMetaType<QVector<int> >("QVector<int>");
    int argc = 1;

    app = new QCoreApplication(argc, nullptr);

    serverAgent = new LocalServer("AthenaLocalServer");
    serverAgent->setConnection(_core);
    serverAgent->connectToDB();

    return app->exec();
}

