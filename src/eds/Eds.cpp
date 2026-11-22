// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/eds/Eds.h>

namespace Euclid::CDK::EDS {

    Eds::Eds(const EAM::Session &session) : ModuleClient(session, std::string(EDS::Target)) {}

    Zone Eds::CreateZone(const std::string &name, const long ttl, const std::string &description) const {
        const boost::json::object payload{{"name", name}, {"ttl", ttl}, {"description", description}};
        return ToZone(Call("create-zone", payload));
    }

    Zone Eds::GetZone(const std::string &name) const {
        return ToZone(Call("get-zone", {{"name", name}}));
    }

    Page<Zone> Eds::ListZones(const ListZonesOptions &options) const {

        // No sort column or direction here, unlike the other modules' listings: zones come back by
        // name, which is the only order a zone listing has.
        const boost::json::object payload{
                {"prefix", options.prefix},
                {"pageSize", options.pageSize},
                {"pageIndex", options.pageIndex},
        };
        return ToPage<Zone>(Call("list-zones", payload), "zones", ToZone);
    }

    long Eds::DeleteZone(const std::string &name) const {
        return Json::Number(Call("delete-zone", {{"name", name}}), "deletedRecords");
    }

    Record Eds::PutRecord(const std::string &zone, const std::string &name, const std::string_view type,
                          const long ttl, const std::vector<std::string> &values) const {

        // Refused here rather than on arrival, because of what an empty set would have to mean: a
        // record set with no values is not a record, and a server reading it as "remove the set"
        // would make a typo into a deletion. The server refuses it too; this says so without the
        // round trip, and names the call that does mean it.
        if (values.empty()) {
            throw EuclidError("values cannot be empty - a record set is its values, and DeleteRecord() is "
                              "what removes one");
        }

        boost::json::array array;
        for (const auto &value: values) {
            array.emplace_back(value);
        }

        const boost::json::object payload{
                {"zone", zone},
                {"name", name},
                {"type", type},
                {"ttl", ttl},
                {"values", array},
        };
        return ToRecord(Call("put-record", payload));
    }

    Page<Record> Eds::ListRecords(const std::string &zone, const ListRecordsOptions &options) const {
        const boost::json::object payload{
                {"zone", zone},
                {"name", options.name},
                {"type", options.type},
                {"pageSize", options.pageSize},
                {"pageIndex", options.pageIndex},
        };
        return ToPage<Record>(Call("list-records", payload), "records", ToRecord);
    }

    void Eds::DeleteRecord(const std::string &zone, const std::string &name, const std::string_view type) const {
        const boost::json::object payload{{"zone", zone}, {"name", name}, {"type", type}};
        // The answer is {"deleted": true} and nothing else - a constant, which a 404 for a set that
        // is not there has already ruled out.
        std::ignore = Call("delete-record", payload);
    }

    Answer Eds::GetAnswer(const std::string &name, const std::string_view type) const {
        const boost::json::object payload{{"name", name}, {"type", type}};
        return ToAnswer(Call("get-answer", payload));
    }

    boost::json::object Eds::Metrics() const {
        return Call("get-metrics");
    }

}// namespace Euclid::CDK::EDS
