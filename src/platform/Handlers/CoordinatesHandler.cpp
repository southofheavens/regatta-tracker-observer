#include <Handlers/CoordinatesHandler.h>

#include <RGT/Devkit/RaceLookup.h>
#include <RGT/Devkit/General.h>

#include <Poco/Redis/Command.h>

namespace
{

struct Coordinates
{
    double longitude;
    double latitude;
};

using CoordinatesVector = std::vector<std::pair<RGT::Devkit::UserId, std::optional<Coordinates>>>;

CoordinatesVector getCurrentParticipantsCoordinates
(
    Poco::Data::Session & session,
    Poco::Redis::PooledConnection & pc,
    RGT::Devkit::RaceId raceId
)

{
    static std::string luaScript = RGT::Devkit::readLuaScript("lua_scripts/get_current_coordinates.lua");

    Poco::Redis::Client::Ptr redisClient = static_cast<Poco::Redis::Client::Ptr>(pc);
    if (redisClient.isNull()) {
        throw std::runtime_error("Redis::Client::Ptr is null");
    }

    std::vector<RGT::Devkit::UserId> participants = RGT::Devkit::getParticipantsOfRace(session, pc, raceId);

    Poco::Redis::Command cmd("EVAL");
    cmd << luaScript
        << std::to_string(participants.size());
    
    for (RGT::Devkit::UserId userId : participants) {
        cmd << std::format("user_participation:{}", RGT::Devkit::mapUserIdToUint(userId));
    }

    Poco::Redis::Array reply = redisClient->execute<Poco::Redis::Array>(cmd);
    if (reply.isNull()) 
    {
        throw RGT::Devkit::RGTException("Internal server error", 
            Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
    }

    CoordinatesVector coords;
    coords.reserve(participants.size());
    uint64_t participantsPos = 0;
    for (const Poco::Redis::RedisType::Ptr & typePtr : reply)
    {
        if (typePtr.isNull()) {
            coords.push_back({participants[participantsPos++], std::nullopt});
        }
        else 
        {
            const Poco::Redis::Type<Poco::Redis::BulkString> & typeBulkString = 
                dynamic_cast<const Poco::Redis::Type<Poco::Redis::BulkString> &>(*typePtr);

            const Poco::Redis::BulkString & bulkStr = typeBulkString.value();
            // Ключа не существует в Redis
            if (bulkStr.isNull()) 
            {
                coords.push_back({participants[participantsPos++], std::nullopt});
                continue;
            }
            const std::string & str = bulkStr.value();

            // Яхтсмен еще не начал загрузку координат 
            if (str == "init") {
                coords.push_back({participants[participantsPos++], std::nullopt});
            }
            else 
            {
                uint64_t firstSemicolonPos = str.find(';');
                uint64_t secondSemicolonPos = str.find(';', firstSemicolonPos + 1);

                double longitude = std::stod(str.substr(0, firstSemicolonPos));
                double latitude = std::stod(str.substr(firstSemicolonPos + 1, secondSemicolonPos));

                coords.push_back({participants[participantsPos++], Coordinates{longitude, latitude}});
            }
        }
    }

    return coords;
}

Poco::JSON::Object::Ptr fillJsonWithCoordinates(const CoordinatesVector & coords)
{
    Poco::JSON::Object::Ptr json(new Poco::JSON::Object);

    for (auto [userId, pos] : coords)
    {
        if (pos.has_value())
        {
            Poco::JSON::Object coordsJson;
            coordsJson.set("longitude", pos->longitude);
            coordsJson.set("latitude", pos->latitude);

            json->set(std::to_string(RGT::Devkit::mapUserIdToUint(userId)), coordsJson);
        }
        else {
            json->set(std::to_string(RGT::Devkit::mapUserIdToUint(userId)), Poco::Dynamic::Var{});
        }
    }

    return json;
}

} // namespace

namespace RGT::Observer::Handlers
{

constexpr uint8_t count_of_segments = 2;

void CoordinatesHandler::requestPreprocessing(Poco::Net::HTTPServerRequest & request)
{
    if (segments_.size() != count_of_segments) 
    {
        throw RGT::Devkit::RGTException("There must be two segments in the uri",
            Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
    }
}

void CoordinatesHandler::extractPayloadFromRequest(Poco::Net::HTTPServerRequest & request)
{
    const std::string & accessToken = HTTPRequestHandler::extractTokenFromRequest(request);
    RGT::Devkit::JWTPayload tokenPayload = HTTPRequestHandler::extractPayload(accessToken);

    uint64_t rawRaceId;
    try {
        rawRaceId = std::stoull(segments_[1]);
    }
    catch (const std::exception & e)
    {
        throw RGT::Devkit::RGTException
        (
            "The second segment in the uri should be a number, "
            "which is the race ID",
            Poco::Net::HTTPResponse::HTTP_BAD_REQUEST
        );
    }

    requestPayload_.tokenPayload = tokenPayload;
    requestPayload_.raceId = Devkit::mapUintToRaceId(rawRaceId);
}

void CoordinatesHandler::requestProcessing
(
    Poco::Net::HTTPServerRequest & request, 
    Poco::Net::HTTPServerResponse & response
) 
{
    if (requestPayload_.tokenPayload.role != RGT::Devkit::UserRole::Judge) 
    {
        throw RGT::Devkit::RGTException("Only judge can view the coordinates of the participants", 
            Poco::Net::HTTPResponse::HTTP_FORBIDDEN);
    }

    Poco::Data::Session session = psqlPool_.get();
    Poco::Redis::PooledConnection pc(redisPool_, 500);
    
    if (not RGT::Devkit::isRaceExists(session, pc, requestPayload_.raceId))
    {
        throw RGT::Devkit::RGTException
        (
            std::format
            (
                "The race with id {} is not exists", RGT::Devkit::mapRaceIdToUint(requestPayload_.raceId)
            ), 
            Poco::Net::HTTPResponse::HTTP_NOT_FOUND
        );
    }

    if (not RGT::Devkit::isParticipationExists(session, pc, requestPayload_.raceId, requestPayload_.tokenPayload.sub))
    {
        throw RGT::Devkit::RGTException
        (
            std::format
            (
                "The judge is not part of the judging panel for the race with ID {}",
                RGT::Devkit::mapRaceIdToUint(requestPayload_.raceId)
            ),
            Poco::Net::HTTPResponse::HTTP_FORBIDDEN
        );
    }

    Devkit::RaceStatus raceStatus = RGT::Devkit::getRaceStatus(session, pc, requestPayload_.raceId);

    switch (raceStatus)
    {
        case RGT::Devkit::RaceStatus::Finished:
            throw RGT::Devkit::RGTException
            (
                std::format
                (
                    "The race with id {} already finished",
                    Devkit::mapRaceIdToUint(requestPayload_.raceId)
                ),
                Poco::Net::HTTPResponse::HTTP_CONFLICT
            );
        case RGT::Devkit::RaceStatus::NotStarted:
            throw RGT::Devkit::RGTException
            (
                std::format
                (
                    "The race with id {} hasn't started yet",
                    Devkit::mapRaceIdToUint(requestPayload_.raceId)
                ),
                Poco::Net::HTTPResponse::HTTP_CONFLICT
            );
        case RGT::Devkit::RaceStatus::InProgress:
        {
            CoordinatesVector coords = getCurrentParticipantsCoordinates(session, pc, requestPayload_.raceId);
            Poco::JSON::Object::Ptr resultJson = fillJsonWithCoordinates(coords);
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_OK);
            std::ostream & out = response.send();
            resultJson->stringify(out);
        }
        default:
            throw std::runtime_error("Unsupported race status");
    }
}

} // namespace RGT::Observer::Handlers
