#ifndef AGENTPLUGINUI_H
#define AGENTPLUGINUI_H

// MinGW Limitation
#define BOOST_DLL_FORCE_ALIAS_INSTANTIATION

#include <QtCore/QtGlobal>
#include <QJsonDocument>
#include <QCoreApplication>

#include <boost/dll/alias.hpp>
#include <boost/shared_ptr.hpp>
#include <Plugin/agentpluginapi.h>
#include <Core/core_api.h>

#include "localserver.h"

class AgentPluginUI : public AgentPluginApi
{
public:
    AgentPluginUI();
    ~AgentPluginUI();

    void setConnection(void *_core) override;
    const std::string getName() const override;

    int getInfo(std::string *a) override;
    void setInfo(std::string pluginName, std::string json) override;

    void runPlugin() override;
    void stopPlugin() override;

    static boost::shared_ptr<AgentPluginApi> create();
    static int createServer(CoreApi* _core);

private:
    CoreApi * core;

    static QCoreApplication *app;
    static LocalServer* serverAgent;
};

LocalServer* AgentPluginUI::serverAgent = nullptr;
QCoreApplication* AgentPluginUI::app = nullptr;

#endif // AGENTPLUGINUI_H

BOOST_DLL_ALIAS
(
    AgentPluginUI::create,
    create
)
