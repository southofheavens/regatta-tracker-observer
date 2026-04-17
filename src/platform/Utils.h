#pragma once 

#include <vector>

#include <RGT/Devkit/RGTException.h>

namespace RGT::Observer
{

inline std::vector<std::string> getPathSegments(const std::string & uri)
{
    if (uri.empty()) {
        return {};
    }

    std::vector<std::string> segments;

    std::string currentSegment;
    for (uint64_t i = 1; i < uri.length(); ++i)
    {
        const unsigned char uCharacter = static_cast<unsigned char>(uri[i]);

        if (uCharacter == '/' or uCharacter == '\\')
        {
            segments.push_back(currentSegment);
            currentSegment.clear();
        }
        else 
        {
            currentSegment += uCharacter;

            if (i == uri.length() - 1) [[unlikely]] {
                segments.push_back(currentSegment);
            }
        }
    }

    return segments;
}

} // namespace RGT::Observer
