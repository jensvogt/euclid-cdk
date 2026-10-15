// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <string_view>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Ead.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EAD {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "ead";

    /**
     * @brief The furthest ListEvents() counts before it stops and says the total is a floor.
     *
     * @par
     * Not a limit on what is listed, which is the page size - a limit on the counting that goes
     * beside it. See AuditPage::totalExact.
     */
    inline constexpr long ListTotalLimit = 100000;

    /**
     * @brief Which entries of the trail to read.
     *
     * @par
     * Every filter is optional and they combine; naming none reads the whole trail for the account.
     */
    struct EUCLID_CDK_API ListEventsOptions {

        /**
         * @brief Only what this user ran.
         */
        std::string userId;

        /**
         * @brief Only what this module answered, e.g. "esm".
         */
        std::string module;

        /**
         * @brief Only this command, by the action name as it travelled, e.g. "delete-object".
         */
        std::string command;
        long pageSize{10};
        long pageIndex{0};
    };

    /**
     * @brief EAD - euclid's audit module: what was run, by whom, in which account.
     *
     * @par
     * @code
     * const auto session = EAM::Eam::ForServer(url).Credentials("jens", "secret").Login();
     * const EAD::Ead ead(session);
     *
     * const auto page = ead.ListEvents({.module = "esm", .command = "delete-object", .pageSize = 50});
     * @endcode
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client.
     *
     * @par This module only reads
     * Nothing here writes the trail. Every module records its own commands as it answers them, which
     * is deliberate: an audit whose write path ran through a separate module would be missing
     * entries exactly when that module was down or stopped - both the likeliest moment for something
     * worth auditing and the worst moment to lose it. So there is no "record event" call, and there
     * is nothing an application has to do to be audited.
     *
     * @par
     * The trail is scoped to the caller's account. Commands are recorded with the namespace they ran
     * in, so AuditEvent::nameSpace is what separates one namespace's activity from another's in the
     * answer - this client does not filter by it, because the server does not.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Ead final : public ModuleClient {
    public:

        /**
         * @brief Builds EAD's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Ead(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Ead(EAM::Session &&) = delete;

        /**
         * @brief A page of the trail.
         *
         * @par
         * The total that comes back is capped - see AuditPage::totalExact for why, and CountEvents()
         * for the exact figure when one is actually wanted.
         *
         * @param options which entries, and which page of them.
         */
        [[nodiscard]]
        AuditPage ListEvents(const ListEventsOptions &options = {}) const;

        /**
         * @brief Exactly how many entries match, however long that takes.
         *
         * @par
         * The count ListEvents() deliberately does not make on every page. Worth the cost when the
         * number itself is the answer; not worth it to draw a pager.
         *
         * @param userId  only what this user ran, or empty for all.
         * @param module  only what this module answered, or empty for all.
         * @param command only this command, or empty for all.
         */
        [[nodiscard]]
        long CountEvents(const std::string &userId = {}, const std::string &module = {},
                         const std::string &command = {}) const;

        /**
         * @brief Removes entries older than a window, and says how many went.
         *
         * @par
         * The window is required and has to be positive: a purge with none would take the whole
         * trail, which is not something to be able to ask for by leaving a field out. The server
         * refuses it, and so does this.
         *
         * @par
         * Note that the window is the only filter - a purge takes everything older than it, across
         * every user and module, rather than narrowing the way ListEvents() does.
         *
         * @par
         * This call is itself recorded. A command that removes audit records is exactly the kind of
         * thing an audit is kept for, and the trail it prunes is where its own entry lives.
         *
         * @param olderThanDays how old an entry has to be to go, in days; must be positive.
         * @return how many entries were removed.
         * @throws EuclidError if the window is not positive.
         */
        [[nodiscard]]
        long PurgeEvents(long olderThanDays) const;

        /**
         * @brief EAD's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to EAD.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;
    };

}// namespace Euclid::CDK::EAD
