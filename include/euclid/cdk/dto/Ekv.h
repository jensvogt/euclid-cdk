// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/dto/Page.h>

/**
 * @file
 * @brief The shapes EKV sends back, and the readers that parse them.
 *
 * @par
 * One of them is not just parsed but rearranged: the server stores an item's timestamps as two
 * ordinary attributes, "_created" and "_modified", and ToItem() lifts them out. Leaving them in
 * would mean an item read, changed and written back acquires two attributes it never had - and
 * since a write replaces rather than merges, they would stick.
 */

namespace Euclid::CDK::EKV {

    /**
     * @brief The attributes the server keeps an item's timestamps in, and which ToItem() takes back
     * out.
     *
     * @par
     * Named with a leading underscore so they cannot collide with an attribute of the caller's: "$"
     * and "." are refused in attribute names and an underscore is not, so this is a convention
     * rather than a guarantee - the server documents it as one too.
     */
    inline constexpr std::string_view CreatedAttribute = "_created";
    inline constexpr std::string_view ModifiedAttribute = "_modified";

    /**
     * @brief One stored item: its attributes, and when it was written.
     *
     * @par
     * The attributes are plain JSON - strings, numbers, booleans, arrays, nested objects - not the
     * tagged COM::Variant that EQS, ENS and ESM attributes use. EKV stores documents rather than
     * typed attribute maps, and only a table's key attributes have a declared type.
     *
     * @par
     * "attributes" is the document itself, which is what to pass back to EKV::Ekv::PutItem() - the
     * timestamps are deliberately not in it.
     */
    struct EUCLID_CDK_API Item {
        boost::json::object attributes;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A table: what it is keyed on, and how many items it holds.
     *
     * @par
     * "sortKey" is empty for a table that has none, which is also what says that EKV::Ekv::Query()
     * cannot narrow by sort key on this table.
     *
     * @par
     * "itemCount" is counted rather than looked up, so describing a large table is not free.
     */
    struct EUCLID_CDK_API TableDescription {
        std::string name;
        std::string ern;
        std::string partitionKey;

        /**
         * @brief "string", "number" or "binary" - see EKV::KeyString and its siblings.
         */
        std::string partitionKeyType;
        std::string sortKey;
        std::string sortKeyType;
        long itemCount{};
        std::string created;
        std::string modified;
    };

    /**
     * @brief The items of one partition that matched, in the order they were asked for.
     *
     * @par
     * "count" is how many came back rather than how many exist, which is why this is not one of the
     * pages the rest of this SDK answers listings with: a query knows what it returned, and the
     * server does not count the partition to say what it did not.
     */
    struct EUCLID_CDK_API QueryResult {
        std::vector<Item> items;
        long count{};
    };

    /**
     * @brief A page of a table's items, and how many the table holds in total.
     *
     * @par
     * "count" is what came back and "total" what there is - the pair a caller pages against.
     */
    struct EUCLID_CDK_API ScanResult {
        std::vector<Item> items;
        long count{};
        long total{};
    };

    /**
     * @brief Reads one item, with its two timestamp attributes lifted out of the rest.
     */
    [[nodiscard]]
    EUCLID_CDK_API Item ToItem(const boost::json::value &value);

    /**
     * @brief Reads a table description.
     */
    [[nodiscard]]
    EUCLID_CDK_API TableDescription ToTableDescription(const boost::json::value &value);

    /**
     * @brief Reads a query response.
     */
    [[nodiscard]]
    EUCLID_CDK_API QueryResult ToQueryResult(const boost::json::value &value);

    /**
     * @brief Reads a scan response.
     */
    [[nodiscard]]
    EUCLID_CDK_API ScanResult ToScanResult(const boost::json::value &value);

}// namespace Euclid::CDK::EKV
