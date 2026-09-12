// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Page.h>

namespace Euclid::CDK {

    boost::json::object PagePayload(const PageOptions &options, const std::string &defaultSortColumn) {
        return {
                {"pageSize", options.pageSize},
                {"pageIndex", options.pageIndex},
                {"sortColumn", options.sortColumn.empty() ? defaultSortColumn : options.sortColumn},
                {"sortDirection", options.sortDirection.empty() ? "asc" : options.sortDirection},
        };
    }

    boost::json::object ListPayload(const ListOptions &options, const std::string &defaultSortColumn) {
        auto payload = PagePayload({.pageSize = options.pageSize,
                                    .pageIndex = options.pageIndex,
                                    .sortColumn = options.sortColumn,
                                    .sortDirection = options.sortDirection},
                                   defaultSortColumn);
        payload["prefix"] = options.prefix;
        return payload;
    }

}// namespace Euclid::CDK
