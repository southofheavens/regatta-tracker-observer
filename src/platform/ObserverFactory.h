#pragma once

#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Redis/PoolableConnectionFactory.h>
#include <Poco/Net/HTTPServerRequest.h>

#include <RGT/Devkit/Subsystems/RedisSubsystem.h>
#include <RGT/Devkit/Subsystems/PsqlSubsystem.h>
#include <RGT/Devkit/ErrorHandler.h>

#include <Handlers/CoordinatesHandler.h>

#include <Utils.h>

namespace RGT::Observer
{

class ObserverFactory : public Poco::Net::HTTPRequestHandlerFactory
{
public:
    using RedisClientObjectPool = Poco::ObjectPool<Poco::Redis::Client, Poco::Redis::Client::Ptr>;

    ObserverFactory(RedisClientObjectPool & redisPool, Poco::Data::SessionPool & psqlPool)
        : redisPool_{redisPool}
        , psqlPool_{psqlPool}
    {
    }

    Poco::Net::HTTPRequestHandler * createRequestHandler(const Poco::Net::HTTPServerRequest & request) final
    {
        const std::string & uri = request.getURI();
        const std::string & method = request.getMethod();

        if (method == "GET")
        {
            std::vector<std::string> segments = RGT::Observer::getPathSegments(uri);

            if (segments[0] == "coordinates") {
                return new RGT::Observer::Handlers::CoordinatesHandler(segments, redisPool_, psqlPool_);
            }
            else {
                return new RGT::Devkit::ErrorHandler;
            }
        }
        else {
            return new RGT::Devkit::ErrorHandler;
        }
    }

private:
    RedisClientObjectPool   & redisPool_;
    Poco::Data::SessionPool & psqlPool_;
};

} // namespace RGT::Observer
