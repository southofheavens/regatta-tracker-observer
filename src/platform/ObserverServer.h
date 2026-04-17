#pragma once

#include <Poco/Util/ServerApplication.h>
#include <Poco/Util/JSONConfiguration.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServer.h>

#include <RGT/Devkit/ProjectName.h>
#include <RGT/Devkit/General.h>

#include <Poco/Redis/Command.h>

#include <ObserverFactory.h>

namespace RGT::Observer
{

class ObserverServer : public Poco::Util::ServerApplication
{
public:
    void initialize(Application & self) final
    {
        try
        {
            Poco::Util::JSONConfiguration::Ptr cfg = new Poco::Util::JSONConfiguration(RGT::Devkit::getConfigPath());
            self.config().add(cfg, PRIO_APPLICATION);
        }
        catch (const Poco::Exception & e) {
            throw std::runtime_error(std::format("Error loading JSON config: {}", e.displayText()));
        }

        RGT::Devkit::readDotEnv();

        Poco::Util::Application::addSubsystem(new RGT::Devkit::Subsystems::RedisSubsystem());
        Poco::Util::Application::addSubsystem(new RGT::Devkit::Subsystems::PsqlSubsystem());

        ServerApplication::initialize(self);
    }

    void uninitialize() final
    { ServerApplication::uninitialize(); }

    int main(const std::vector<std::string> &) final
    {
        Poco::Util::LayeredConfiguration & cfg = ObserverServer::config();

        Poco::Net::ServerSocket svs(cfg.getUInt16("server.port"));

        auto & redisSubsystem = Poco::Util::Application::getSubsystem<Devkit::Subsystems::RedisSubsystem>();
        auto & psqlSubsystem = Poco::Util::Application::getSubsystem<Devkit::Subsystems::PsqlSubsystem>();

        Poco::Redis::PooledConnection pc(redisSubsystem.getPool(), 500);
        Poco::Redis::Client::Ptr redisClient = static_cast<Poco::Redis::Client::Ptr>(pc);

        Poco::Net::HTTPServer srv
        (
            new RGT::Observer::ObserverFactory
            (
                redisSubsystem.getPool(), psqlSubsystem.getPool()
            ), 
            svs, 
            new Poco::Net::HTTPServerParams
        );

        srv.start();
        
        waitForTerminationRequest();
        
        srv.stop();
        
        return Application::EXIT_OK;
    }
};

} // namespace RGT::Observer
