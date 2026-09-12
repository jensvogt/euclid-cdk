// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Page.h>

namespace Euclid::CDK {

    boost::json::object ListPayload(const ListOptions &options, const std::string &defaultSortColumn) {
        return {
                {"prefix", options.prefix},
                {"pageSize", options.pageSize},
                {"pageIndex", options.pageIndex},
                {"sortColumn", options.sortColumn.empty() ? defaultSortColumn : options.sortColumn},
                {"sortDirection", options.sortDirection.empty() ? "asc" : options.sortDirection},
        };
    }

}// namespace Euclid::CDK
