// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/ead/Ead.h>

namespace Euclid::CDK::EAD {

    Ead::Ead(const EAM::Session &session) : ModuleClient(session, std::string(EAD::Target)) {}

    AuditPage Ead::ListEvents(const ListEventsOptions &options) const {

        const boost::json::object payload{
                {"userId", options.userId},
                // "module" on the wire: the module that answered the command, which is not the
                // x-euclid-target this request itself carries.
                {"module", options.module},
                {"command", options.command},
                {"pageSize", options.pageSize},
                {"pageIndex", options.pageIndex},
        };
        return ToAuditPage(Call("list-events", payload));
    }

    long Ead::CountEvents(const std::string &userId, const std::string &module, const std::string &command) const {
        const boost::json::object payload{{"userId", userId}, {"module", module}, {"command", command}};
        return Json::Number(Call("count-events", payload), "count");
    }

    long Ead::PurgeEvents(const long olderThanDays) const {

        // Refused here rather than on arrival, because of what the mistake would cost: zero is what
        // a caller who left the window out sends, and a server reading that as "everything" would
        // take the whole trail. The server refuses it too; this says so without the round trip.
        if (olderThanDays <= 0) {
            throw EuclidError("olderThanDays has to be a positive number of days - a purge with no window "
                              "would remove the whole trail");
        }

        return Json::Number(Call("purge-events", {{"olderThanDays", olderThanDays}}), "removed");
    }

    boost::json::object Ead::Metrics() const {
        return Call("get-metrics");
    }

}// namespace Euclid::CDK::EAD
