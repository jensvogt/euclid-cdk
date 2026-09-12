// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <optional>
#include <string>
#include <string_view>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Ekv.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EKV {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "ekv";

    /**
     * @brief The types a key attribute can have.
     *
     * @par
     * A table's keys are the only attributes with a declared type, and the type is what a comparison
     * is made under: a number sort key orders 2, 9, 10, 100 rather than putting "10" before "9".
     */
    inline constexpr std::string_view KeyString = "string";
    inline constexpr std::string_view KeyNumber = "number";
    inline constexpr std::string_view KeyBinary = "binary";

    /**
     * @brief How Query() narrows by sort key: not at all, which takes the whole partition...
     */
    inline constexpr std::string_view WholePartition = "";

    /**
     * @brief ...or by one of the comparisons, which need a table that declares a sort key. Asking one
     * that does not is refused with HTTP 400.
     */
    inline constexpr std::string_view SortEq = "eq";
    inline constexpr std::string_view SortLt = "lt";
    inline constexpr std::string_view SortLe = "le";
    inline constexpr std::string_view SortGt = "gt";
    inline constexpr std::string_view SortGe = "ge";

    /**
     * @brief Takes a lower and an upper bound, both inclusive - see EKV::Ekv::Query().
     */
    inline constexpr std::string_view SortBetween = "between";

    /**
     * @brief The one operator that is not a comparison, and so the one that applies to a string sort
     * key only: a prefix of a number or of a blob is not a thing.
     */
    inline constexpr std::string_view SortBeginsWith = "begins-with";

    /**
     * @brief What a table is keyed on, beyond the partition key every table has.
     */
    struct EUCLID_CDK_API CreateTableOptions {

        /**
         * @brief KeyString, KeyNumber or KeyBinary.
         */
        std::string partitionKeyType = std::string(KeyString);

        /**
         * @brief The attribute items sharing a partition key are ordered by, which is what makes a
         * partition readable as a range. Left empty, the table has none and Query() can only take
         * whole partitions.
         */
        std::string sortKey;

        /**
         * @brief The sort key's type, which decides what its ordering means.
         */
        std::string sortKeyType = std::string(KeyString);
    };

    /**
     * @brief How a query narrows a partition, and in what order and quantity it reads it.
     */
    struct EUCLID_CDK_API QueryOptions {

        /**
         * @brief SortEq, SortLt, SortLe, SortGt, SortGe, SortBetween or SortBeginsWith - or
         * WholePartition for all of it.
         */
        std::string sortOperator = std::string(WholePartition);

        /**
         * @brief What to compare the sort key against - the lower bound for SortBetween. Null when
         * there is nothing to compare against.
         */
        boost::json::value sortValue;

        /**
         * @brief The upper bound, for SortBetween only.
         */
        boost::json::value sortUpper;

        /**
         * @brief Whether to read in ascending sort-key order. Always sent, because the server reads
         * an absent flag as descending rather than as "unspecified".
         */
        bool forward{true};

        /**
         * @brief The most items to return; zero means no limit.
         */
        long pageSize{0};

        /**
         * @brief The zero-based page, applied when pageSize is set.
         */
        long pageIndex{0};
    };

    /**
     * @brief How much of a table a scan reads at a time.
     */
    struct EUCLID_CDK_API ScanOptions {
        long pageSize{0};
        long pageIndex{0};
    };

    /**
     * @brief EKV - euclid's key-value store: tables of items, read by key rather than searched.
     *
     * @par
     * @code
     * const EKV::Ekv ekv(session);
     * ekv.CreateTable("sessions", "userId", {.sortKey = "startedAt", .sortKeyType = std::string(EKV::KeyNumber)});
     *
     * ekv.PutItem("sessions", {{"userId", "jens"}, {"startedAt", 1757462400}, {"host", "laptop"}});
     * const auto recent = ekv.Query("sessions", "jens", {.sortOperator = std::string(EKV::SortGe), .sortValue = 1757462400});
     * @endcode
     *
     * @par
     * A table is keyed on one attribute, or on two: a partition key that identifies an item, and
     * optionally a sort key that orders the items sharing a partition key - which is what makes a
     * partition readable as a range. The key attributes have declared types, and they cannot be
     * changed after the table is created.
     *
     * @par
     * An item's other attributes are free-form JSON - scalars, arrays, nested objects - and are
     * declared nowhere. They are also not typed the way a queue message's attributes are: EKV stores
     * what JSON can express, so nothing here takes a COM::Variant.
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API Ekv final : public ModuleClient {
    public:

        /**
         * @brief Builds EKV's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Ekv(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Ekv(EAM::Session &&) = delete;

        // -- tables -----------------------------------------------------------------------------

        /**
         * @brief Creates a table, and answers with it as it was created - with an item count of
         * zero.
         *
         * @par
         * Refused with HTTP 409 if this account and namespace already hold a table of that name -
         * the pair a table name is unique within, so the same name in another namespace is another
         * table - and with HTTP 400 if a key attribute is empty, starts with "$", contains "." or if
         * the sort key names the same attribute as the partition key.
         *
         * @param name         the table's name.
         * @param partitionKey the attribute every item is identified by.
         * @param options      the key types, and the sort key if the table has one.
         */
        [[nodiscard]]
        TableDescription CreateTable(const std::string &name, const std::string &partitionKey, const CreateTableOptions &options = {}) const;

        /**
         * @brief A table's key, and how many items it holds.
         *
         * @par
         * The count is counted rather than looked up, so this is not free on a large table.
         */
        [[nodiscard]]
        TableDescription DescribeTable(const std::string &name) const;

        /**
         * @brief One page of tables, each described as DescribeTable() would describe it.
         *
         * @par
         * The session's own account and namespace, and "total" counts that scope rather than the
         * account.
         */
        [[nodiscard]]
        Page<TableDescription> ListTables(const ListOptions &options = {}) const;

        /**
         * @brief Deletes a table and every item in it, and answers with how many items went with it.
         *
         * @par
         * There is no confirmation and nothing is kept.
         */
        [[nodiscard]]
        long DeleteTable(const std::string &name) const;

        // -- items ------------------------------------------------------------------------------

        /**
         * @brief Writes an item, replacing whatever was stored under its key.
         *
         * @par
         * It replaces rather than merges: an item written with two attributes has two attributes
         * afterwards, whatever it had before. So changing one field means reading the item, changing
         * it and writing the whole thing back - which is why EKV::Item keeps the server's timestamps
         * out of its attributes, where they would otherwise be written back as two attributes of the
         * caller's own.
         *
         * @par
         * The item has to carry the table's key attributes with the types the table declared for
         * them, and no attribute name may be empty, start with "$" or contain ".".
         */
        [[nodiscard]]
        Item PutItem(const std::string &table, const boost::json::object &item) const;

        /**
         * @brief Reads one item by its key.
         *
         * @par
         * The key names the table's key attributes and only those - the partition key alone where
         * the table has no sort key, both where it has one.
         *
         * @throws ServiceError with status 404 when there is no such item: "there is no such item"
         * and "here is an item with nothing in it" are different, and a caller should not have to
         * tell them apart. Where a miss is an ordinary outcome, FindItem() is the one to reach for.
         */
        [[nodiscard]]
        Item GetItem(const std::string &table, const boost::json::object &key) const;

        /**
         * @brief The same read as GetItem(), answering nothing rather than throwing when there is no
         * such item.
         *
         * @par
         * Only a 404 becomes an empty optional. A refusal, a malformed key or a table that does not
         * exist still throws, because none of those mean "not there".
         */
        [[nodiscard]]
        std::optional<Item> FindItem(const std::string &table, const boost::json::object &key) const;

        /**
         * @brief Removes one item by its key, and says whether there was one to remove.
         *
         * @par
         * False rather than an error for a key that names nothing: deleting what is not there has
         * already achieved what the caller asked for.
         */
        [[nodiscard]]
        bool DeleteItem(const std::string &table, const boost::json::object &key) const;

        // -- reading many -----------------------------------------------------------------------

        /**
         * @brief Reads the items of one partition, in sort-key order.
         *
         * @par
         * This is the lookup EKV is for: it addresses a partition by key rather than reading the
         * table. Narrowing by sort key needs a table that declares one, and asking one that does not
         * for anything but WholePartition is refused with HTTP 400.
         *
         * @param table        the table's name.
         * @param partitionKey the partition key's value, of the type the table declared for it.
         * @param options      how to narrow, order and page the partition.
         * @throws EuclidError if SortBetween was asked for without both bounds, which the server
         * refuses anyway - this just says so before the round trip.
         */
        [[nodiscard]]
        QueryResult Query(const std::string &table, const boost::json::value &partitionKey, const QueryOptions &options = {}) const;

        /**
         * @brief Reads a table's items without regard to their key.
         *
         * @par
         * This reads the table rather than an index: fine for a small table or an export, the wrong
         * tool for a lookup - Query() is that. A pageSize of zero means no limit, so a scan of a
         * large table with no paging brings all of it back.
         */
        [[nodiscard]]
        ScanResult Scan(const std::string &table, const ScanOptions &options = {}) const;

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief EKV's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to EKV.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;
    };

}// namespace Euclid::CDK::EKV
