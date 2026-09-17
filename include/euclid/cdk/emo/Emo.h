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
    };

}// namespace Euclid::CDK::EMO
