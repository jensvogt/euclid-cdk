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
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Emo.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EMO {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "emo";

    /**
     * @brief Which alert states to list.
     */
    struct EUCLID_CDK_API ListAlertsOptions {

        /**
         * @brief Only the series of this rule, or empty for all of them.
         */
        std::string ruleId;

        /**
         * @brief Only series in this state - LevelWarning and its siblings - or empty for any.
         */
        std::string state;

        /**
         * @brief Leave out what is healthy, which is what a "what is wrong right now" view wants.
         */
        bool activeOnly{false};
    };

    /**
     * @brief Which part of the alert history to read.
     */
    struct EUCLID_CDK_API AlertHistoryOptions {

        /**
         * @brief Only this rule's transitions, or empty for all of them.
         */
        std::string ruleId;

        /**
         * @brief ISO 8601; empty leaves that end of the window open.
         */
        std::string from;
        std::string to;

        /**
         * @brief At most this many, most recent first.
         */
        long limit{100};
    };

    /**
     * @brief EMO - euclid's monitoring module: the metrics an installation keeps, and the way an
     * application adds its own to them.
     *
     * @par
     * @code
     * const EMO::Emo emo(session);
     * emo.PushMetrics("invoice-parser", {EMO::Metric::Rate("invoices.parsed", 41),
     *                                    EMO::Metric::Gauge("queue.depth", 7)});
     * @endcode
     *
     * @par What this is for
     * euclid's own modules push their samples here on their own schedule rather than being polled,
     * because a module the autoscaler is tearing down cannot answer a poll - it simply stops
     * pushing. An application is in the same position, and pushing puts the decision about what is
     * worth publishing where it belongs: in the application, which is the only thing that knows.
     *
     * @par
     * What lands here lands in the same rows EMO's own collectors write, and therefore in the same
     * rollups, the same retention and the same graphs as CPU, memory and the module gauges.
     *
     * @par Measuring rather than pushing
     * PushMetrics() takes numbers that are already final. An application that wants to count
     * requests and time them wants EMO::MeterRegistry instead, which accumulates meters in the
     * process and pushes them on a step - what a Micrometer registry does in euclid-jdk, and what
     * euclid's own C++ modules do with Core::Monitoring.
     *
     * @par Reading
     * List() and Average() are administrator-only, server-side: an application publishes its own
     * numbers without special rights, and reading everybody's is a different question.
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Emo final : public ModuleClient {
    public:

        /**
         * @brief Builds EMO's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Emo(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Emo(EAM::Session &&) = delete;

        /**
         * @brief Pushes a batch of measurements, reported under one name.
         *
         * @par
         * An empty batch is not sent at all: there is nothing to record, and a request that says so
         * is a round trip for nothing.
         *
         * @par
         * Every metric's type decides how it is rolled up - a rate is summed and a gauge averaged -
         * so a counter pushed as a gauge is averaged into nonsense. Metric::Rate() and
         * Metric::Gauge() are how that is said.
         *
         * @param module  what is reporting. An application's own id is the useful value: it is how a
         * reader tells one pool's numbers from another's, and it is what a listing filters on.
         * @param metrics the batch.
         */
        void PushMetrics(const std::string &module, const std::vector<Metric> &metrics) const;

        /**
         * @brief The rows a query matches, most recent first.
         *
         * @par
         * Administrator-only, server-side.
         *
         * @param query which series, which window, which resolution.
         */
        [[nodiscard]]
        std::vector<MetricSample> List(const MetricQuery &query = {}) const;

        /**
         * @brief The mean of the values a query matches, over the window it names.
         *
         * @par
         * One number rather than the rows behind it, for the question a dashboard tile asks.
         * Administrator-only, as List() is.
         */
        [[nodiscard]]
        double Average(const MetricQuery &query = {}) const;

        /**
         * @brief What metric names the installation has seen, and what shape each is.
         *
         * @par
         * The catalogue behind a query rather than the rows: what names exist, which labels they
         * carry and how many series each has. What a chooser needs before it can offer a query at
         * all, and the honest answer to "what is being recorded" - which nothing else gives,
         * because a listing can only confirm what it was already asked about.
         *
         * @par
         * Administrator-only, as List() is.
         *
         * @param hours how far back to look; at least 1. A name last seen before that is not
         * listed, so this is the live catalogue rather than everything ever pushed.
         * @param prefix only names starting with this, or empty for all of them.
         * @throws EuclidError if hours is below 1, which the server refuses anyway.
         */
        [[nodiscard]]
        std::vector<MetricInfo> ListMetrics(long hours = 24, const std::string &prefix = {}) const;

        // -- alert rules ------------------------------------------------------------------------

        /**
         * @brief Defines a rule that watches a metric.
         *
         * @par
         * A rule names a metric and optionally labels that narrow it, and is measured once per step
         * of its resolution. The period counts are what keep one noisy step from raising an alert -
         * see AlertRule::forPeriods.
         *
         * @par
         * Both thresholds are optional and independent: a rule may warn without ever going critical.
         * Leaving both unset is refused, since such a rule can never fire.
         *
         * @param rule the rule; AlertRule::ruleId names it.
         * @throws ServiceError 400 if the rule is not coherent, 409 if the account already has a
         * rule of that ID, and 400 if the rule would cover more series than the installation allows
         * one rule to watch.
         */
        [[nodiscard]]
        AlertRule CreateAlertRule(const AlertRule &rule) const;

        /**
         * @brief Changes a rule. Only the fields present in `rule` change.
         *
         * @par
         * Read the rule, change what you mean, send it back - the server merges onto what is stored,
         * so a default-constructed AlertRule with only a ruleId and a threshold changes that
         * threshold alone. AlertRule::labels is replaced wholesale when it is given at all, because
         * a label set is one value rather than a set of fields.
         *
         * @par
         * Changing the metric name or the labels moves the rule to other series, and the alert
         * states of the ones it used to watch are retired rather than left behind.
         *
         * @throws ServiceError 400 if the result is not coherent, 404 if there is no such rule.
         */
        [[nodiscard]]
        AlertRule UpdateAlertRule(const AlertRule &rule) const;

        /**
         * @brief Removes a rule, and the alert states of every series it watched.
         *
         * @throws ServiceError 404 if there is no such rule.
         */
        void DeleteAlertRule(const std::string &ruleId) const;

        /**
         * @brief The account's alert rules.
         *
         * @param ruleId only this one, or empty for all of them.
         */
        [[nodiscard]]
        std::vector<AlertRule> ListAlertRules(const std::string &ruleId = {}) const;

        /**
         * @brief Where each watched series stands now - one entry per series, not per rule.
         *
         * @par
         * A rule covering forty series has forty entries here, because the state is the series'
         * rather than the rule's.
         *
         * @param options which rule, which state, and whether to leave out what is healthy.
         */
        [[nodiscard]]
        std::vector<Alert> ListAlerts(const ListAlertsOptions &options = {}) const;

        /**
         * @brief Every change of state, most recent first.
         *
         * @par
         * What ListAlerts() cannot answer: an alert that fired and cleared while nobody was looking
         * leaves nothing in the current state and a pair of entries here.
         *
         * @param options which rule, which window, and how many.
         */
        [[nodiscard]]
        std::vector<AlertTransition> ListAlertHistory(const AlertHistoryOptions &options = {}) const;

        // -- dashboards -------------------------------------------------------------------------

        /**
         * @brief Saves a dashboard layout under a name, replacing one of that name.
         *
         * @par
         * The layout is any JSON object and EMO does not interpret it - panels, queries and
         * arrangement belong to whatever draws them. Re-saving keeps the original creation time, so
         * an edited dashboard does not look new.
         *
         * @par
         * Dashboards belong to the user that saved them: this session's own, with no way to name
         * another.
         *
         * @param name   what to save it as.
         * @param layout the layout.
         * @throws ServiceError 400 if the name is empty.
         */
        [[nodiscard]]
        Dashboard SaveDashboard(const std::string &name, const boost::json::object &layout) const;

        /**
         * @brief One dashboard, with its layout.
         *
         * @throws ServiceError 404 if this user has no dashboard of that name.
         */
        [[nodiscard]]
        Dashboard GetDashboard(const std::string &name) const;

        /**
         * @brief This user's dashboards, names and times only - Dashboard::layout is empty.
         */
        [[nodiscard]]
        std::vector<Dashboard> ListDashboards() const;

        /**
         * @brief Deletes one of this user's dashboards.
         *
         * @throws ServiceError 404 if there is no dashboard of that name.
         */
        void DeleteDashboard(const std::string &name) const;

    private:

        /**
         * @brief An alert rule as a payload: everything for a create, only what was set for an
         * update.
         */
        [[nodiscard]]
        static boost::json::object RuleFields(const AlertRule &rule, bool whole);
    };

}// namespace Euclid::CDK::EMO
