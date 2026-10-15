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
 * @brief The shapes EAD sends back, and the readers that parse them.
 */

namespace Euclid::CDK::EAD {

    /**
     * @brief One command euclid answered, as the trail recorded it.
     *
     * @par
     * Written by the module that answered the command rather than by EAD, which only reads - an
     * audit whose write path ran through a separate module would be missing entries exactly when
     * that module was down, which is both the likeliest moment for something worth auditing and the
     * worst moment to lose it.
     */
    struct EUCLID_CDK_API AuditEvent {
        std::string accountId;
        std::string nameSpace;

        /**
         * @brief Who ran it.
         */
        std::string userId;

        /**
         * @brief Which module answered it - "esm", "eqs", and so on.
         */
        std::string module;

        /**
         * @brief What was run, which is the action name as it travelled.
         */
        std::string command;

        /**
         * @brief What it was called with, as the module recorded it.
         */
        boost::json::value parameters;

        /**
         * @brief How it ended.
         */
        std::string status;
        std::string created;
    };

    /**
     * @brief A page of the trail, and how many entries match.
     *
     * @par
     * "totalExact" is what makes a capped total honest rather than wrong: false means there are *at
     * least* this many, which is all a pager needs to know there is another page. An exact figure
     * means reading an index entry per matching event, and on a trail of tens of millions that is
     * seconds of database time beside a page of fifty - paid again on every page somebody turns.
     * Ead::CountEvents() is where the real number comes from.
     */
    struct EUCLID_CDK_API AuditPage {
        std::vector<AuditEvent> events;
        long total{};
        bool totalExact{};
    };

    /**
     * @brief Reads one audit event.
     */
    [[nodiscard]]
    EUCLID_CDK_API AuditEvent ToAuditEvent(const boost::json::value &value);

    /**
     * @brief Reads a list-events response.
     */
    [[nodiscard]]
    EUCLID_CDK_API AuditPage ToAuditPage(const boost::json::value &value);

}// namespace Euclid::CDK::EAD
