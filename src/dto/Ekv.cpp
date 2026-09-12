// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Ekv.h>

namespace Euclid::CDK::EKV {

    namespace {

        /**
         * @brief The items of a response, however many came back.
         */
        std::vector<Item> itemsOf(const boost::json::value &value) {
            std::vector<Item> items;
            for (const auto &document: Json::Documents(value, "items")) items.push_back(ToItem(document));
            return items;
        }

    }// namespace

    Item ToItem(const boost::json::value &value) {

        Item item{
                .attributes = Json::Object(value),
                .created = Json::Text(value, std::string(CreatedAttribute)),
                .modified = Json::Text(value, std::string(ModifiedAttribute)),
        };
        // Out of the attributes rather than alongside them: a write replaces an item rather than
        // merging into it, so a timestamp left in here would be written back as an attribute of the
        // caller's own and stay there.
        item.attributes.erase(CreatedAttribute);
        item.attributes.erase(ModifiedAttribute);
        return item;
    }

    TableDescription ToTableDescription(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .partitionKey = Json::Text(value, "partitionKey"),
                .partitionKeyType = Json::Text(value, "partitionKeyType"),
                .sortKey = Json::Text(value, "sortKey"),
                .sortKeyType = Json::Text(value, "sortKeyType"),
                .itemCount = Json::Number(value, "itemCount"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    QueryResult ToQueryResult(const boost::json::value &value) {
        auto items = itemsOf(value);
        // Falling back to what arrived keeps a sparse answer self-consistent.
        const auto count = Json::Number(value, "count", static_cast<long>(items.size()));
        return {.items = std::move(items), .count = count};
    }

    ScanResult ToScanResult(const boost::json::value &value) {
        auto items = itemsOf(value);
        const auto count = Json::Number(value, "count", static_cast<long>(items.size()));
        return {.items = std::move(items), .count = count, .total = Json::Number(value, "total")};
    }

}// namespace Euclid::CDK::EKV
