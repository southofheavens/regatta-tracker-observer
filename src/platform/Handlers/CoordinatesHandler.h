#pragma once

#include <RGT/Devkit/Types.h>
#include <RGT/Devkit/HTTPRequestHandler.h>

#include <Poco/Redis/PoolableConnectionFactory.h>
#include <Poco/Data/SessionPool.h>

namespace RGT::Observer::Handlers
{

class CoordinatesHandler : public RGT::Devkit::HTTPRequestHandler
{
public:
    using RedisClientObjectPool = Poco::ObjectPool<Poco::Redis::Client, Poco::Redis::Client::Ptr>;

    CoordinatesHandler(std::vector<std::string> segments, RedisClientObjectPool & redisPool, 
        Poco::Data::SessionPool & psqlPool)
        : segments_{segments}
        , redisPool_{redisPool}
        , psqlPool_{psqlPool}
    {
    }

private:    
    virtual void requestPreprocessing(Poco::Net::HTTPServerRequest & request) final;

    virtual void extractPayloadFromRequest(Poco::Net::HTTPServerRequest & request) final;

    virtual void requestProcessing
    (
        Poco::Net::HTTPServerRequest  & request, 
        Poco::Net::HTTPServerResponse & response
    ) final;

private:
    struct
    {
        Devkit::JWTPayload tokenPayload;

        Devkit::RaceId raceId;
    } requestPayload_;

    std::vector<std::string>  segments_;
    RedisClientObjectPool   & redisPool_;
    Poco::Data::SessionPool & psqlPool_;
};

} // namespace RGT::Observer::Handlers
