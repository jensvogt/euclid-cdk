// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/ekv/Ekv.h>

namespace Euclid::CDK::EKV {

    namespace {

        /**
         * @brief HTTP 404, which is how a read says the item is not there - see Ekv::FindItem().
         */
        constexpr int kNotFound = 404;

    }// namespace

    Ekv::Ekv(const EAM::Session &session) : ModuleClient(session, std::string(EKV::Target)) {}

    // -- tables -------------------------------------------------------------------------------------

    TableDescription Ekv::CreateTable(const std::string &name, const std::string &partitionKey, const CreateTableOptions &options) const {
        const boost::json::object payload{
                {"name", name},
                {"partitionKey", partitionKey},
                {"partitionKeyType", options.partitionKeyType},
                {"sortKey", options.sortKey},
                {"sortKeyType", options.sortKeyType},
        };
        return ToTableDescription(Call("create-table", payload));
    }

    TableDescription Ekv::GetTable(const std::string &name) const {
        return ToTableDescription(Call("get-table", {{"name", name}}));
    }

    bool Ekv::ExistsTable(const std::string &name) const {
        try {
            std::ignore = GetTable(name);
        } catch (const ServiceError &error) {
            if (error.Status() == 404) return false;
            throw;
        }
        return true;
    }

    TableDescription Ekv::DescribeTable(const std::string &name) const {
        return GetTable(name);
    }

    Page<TableDescription> Ekv::ListTables(const ListOptions &options) const {
        return ToPage<TableDescription>(Call("list-tables", ListPayload(options, "name")), "tables", ToTableDescription);
    }

    long Ekv::DeleteTable(const std::string &name) const {
        return NumberOf("delete-table", {{"name", name}}, "deletedItems");
    }

    // -- items --------------------------------------------------------------------------------------

    Item Ekv::PutItem(const std::string &table, const boost::json::object &item) const {
        return ToItem(Call("put-item", {{"table", table}, {"item", item}}));
    }

    Item Ekv::GetItem(const std::string &table, const boost::json::object &key) const {
        return ToItem(Call("get-item", {{"table", table}, {"key", key}}));
    }

    std::optional<Item> Ekv::FindItem(const std::string &table, const boost::json::object &key) const {
        try {
            return GetItem(table, key);
        } catch (const ServiceError &ex) {
            // Only a 404 is "not there". A refusal, a malformed key or a table that does not exist
            // are all still failures, and swallowing them here would hide them.
            if (ex.Status() == kNotFound) return std::nullopt;
            throw;
        }
    }

    bool Ekv::DeleteItem(const std::string &table, const boost::json::object &key) const {
        return Json::Flag(Call("delete-item", {{"table", table}, {"key", key}}), "deleted");
    }

    // -- reading many -------------------------------------------------------------------------------

    QueryResult Ekv::Query(const std::string &table, const boost::json::value &partitionKey, const QueryOptions &options) const {

        if (options.sortOperator == SortBetween && (options.sortValue.is_null() || options.sortUpper.is_null())) {
            throw EuclidError("between needs both a sortValue and a sortUpper");
        }

        const boost::json::object payload{
                {"table", table},
                {"partitionKey", partitionKey},
                {"sortOperator", options.sortOperator},
                // Null rather than absent for the two bounds: the server reads the pair as sent
                // rather than as defaulted.
                {"sortValue", options.sortValue},
                {"sortUpper", options.sortUpper},
                // Always sent: an absent flag reads as descending rather than as "unspecified".
                {"forward", options.forward},
                {"pageSize", options.pageSize},
                {"pageIndex", options.pageIndex},
        };
        return ToQueryResult(Call("query", payload));
    }

    ScanResult Ekv::Scan(const std::string &table, const ScanOptions &options) const {
        const boost::json::object payload{{"table", table}, {"pageSize", options.pageSize}, {"pageIndex", options.pageIndex}};
        return ToScanResult(Call("scan", payload));
    }

    // -- monitoring ---------------------------------------------------------------------------------

    boost::json::object Ekv::Metrics() const {
        return Call("get-metrics");
    }

}// namespace Euclid::CDK::EKV
