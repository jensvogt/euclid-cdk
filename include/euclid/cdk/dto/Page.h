// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>

/**
 * @file
 * @brief How a listing is asked for, and what it answers with.
 *
 * @par
 * Every euclid module pages the same way - a prefix, a page size and index, a sort column and a
 * direction in, a page of rows and a total out - so these live next to neither of them. EAM::Session
 * and the module clients take and answer the same two types.
 */

namespace Euclid::CDK {

    /**
     * @brief How a listing is paged and ordered.
     *
     * @par
     * An empty sortColumn means "whatever this listing sorts by", which each call fills in with the
     * server's own default for it - "userId" for users, "name" for groups, namespaces and buckets,
     * "created" for messages.
     */
    struct EUCLID_CDK_API PageOptions {
        long pageSize{10};
        long pageIndex{0};
        std::string sortColumn;
        std::string sortDirection{"asc"};
    };

    /**
     * @brief The same, for the listings that also narrow by prefix.
     *
     * @par
     * A separate struct rather than one built on PageOptions, so that a caller can keep writing
     * @code {.prefix = "j", .pageSize = 25} @endcode - a designated initializer cannot name a field
     * a base class holds. The listings that page messages take PageOptions, because the server has
     * nothing to match a message against.
     */
    struct EUCLID_CDK_API ListOptions {
        std::string prefix;
        long pageSize{10};
        long pageIndex{0};
        std::string sortColumn;
        std::string sortDirection{"asc"};
    };

    /**
     * @brief A listing's paging and ordering, as the server reads it, with its own defaults filled
     * in where the caller said nothing.
     *
     * @param options           what the caller asked for.
     * @param defaultSortColumn what this listing sorts by when the caller did not say.
     * @return the payload, to be sent as it is or with the listing's own fields added to it.
     */
    [[nodiscard]]
    EUCLID_CDK_API boost::json::object PagePayload(const PageOptions &options, const std::string &defaultSortColumn);

    /**
     * @brief The same, plus the prefix a listing is narrowed by.
     */
    [[nodiscard]]
    EUCLID_CDK_API boost::json::object ListPayload(const ListOptions &options, const std::string &defaultSortColumn);

    /**
     * @brief One page of something, and how many exist in total.
     *
     * @par
     * "total" counts everything the listing matched, not what this page holds - it is what a caller
     * pages through.
     */
    template<typename T>
    struct Page {
        long total{};
        std::vector<T> items;
    };

    /**
     * @brief Reads one page of whatever a listing returns, under the field the server puts it in.
     *
     * @param value  the response.
     * @param field  the field holding the array, e.g. "users".
     * @param parse  reader for one element.
     * @return the page.
     */
    template<typename T, typename Parser>
    [[nodiscard]] Page<T> ToPage(const boost::json::value &value, const std::string &field, Parser parse) {
        Page<T> page;
        page.total = Json::Number(value, "total");
        for (const auto &document: Json::Documents(value, field)) {
            page.items.push_back(parse(document));
        }
        return page;
    }

}// namespace Euclid::CDK
