#include "kiwoom_protocol.h"

#include "json_lite.h"

#include <sstream>

namespace trading
{
    std::string BuildWebSocketRemovalMessage(
        const std::string& groupNumber,
        const std::vector<std::string>& items,
        const std::vector<std::string>& realTimeTypes)
    {
        std::ostringstream out;
        out
            << '{'
            << "\"trnm\":\"REMOVE\","
            << "\"grp_no\":"
            << json_lite::EscapeString(groupNumber)
            << ','
            << "\"data\":[{\"item\":[";

        for (std::size_t index = 0; index < items.size(); ++index) {
            if (index > 0) out << ',';
            out << json_lite::EscapeString(items[index]);
        }

        out << "],\"type\":[";
        for (std::size_t index = 0; index < realTimeTypes.size(); ++index) {
            if (index > 0) out << ',';
            out << json_lite::EscapeString(realTimeTypes[index]);
        }

        out << "]}]}";
        return out.str();
    }
}
