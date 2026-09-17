// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>

/**
 * @file
 * @brief The shapes EMO takes and sends back, and the readers that parse them.
 *
 * @par
 * The type of a metric is the one field here worth reading twice. It is not decoration: EMO rolls
 * five-minute rows into hourly ones and hourly into daily, and the type is what says whether that
 * is a sum or a mean. A counter pushed as a gauge is averaged into nonsense, and a gauge pushed as
 * a rate is summed into more of it.
 *
 * @par
 * The same type is spelled two ways, which is the server's doing rather than this SDK's: a push
 * sends "gauge" or "rate" in lower case - and reads anything that is not exactly "rate" as a gauge -
 * while a listing answers with "GAUGE" or "RATE" in upper case. Both spellings are named below.
 */

namespace Euclid::CDK::EMO {

    /**
     * @brief The dimensions of a metric, which is what splits one series into several.
     *
     * @par
     * Values are strings because that is what a dimension is. A number here would split a series in
     * two on nothing but its JSON spelling, which is why the server takes them as strings whatever
     * they arrived as.
     */
    using Labels = std::map<std::string, std::string>;

    /**
     * @brief What a push calls a value that stands on its own - a queue depth, a heap size. Rolled
     * up by averaging.
     */
    inline constexpr std::string_view TypeGauge = "gauge";

    /**
     * @brief What a push calls a value accumulated over the interval it covers - requests served,
     * bytes written. Rolled up by summing.
     */
    inline constexpr std::string_view TypeRate = "rate";

    /**
     * @brief The same two, as a listing spells them back.
     */
    inline constexpr std::string_view StoredGauge = "GAUGE";
    inline constexpr std::string_view StoredRate = "RATE";

    /**
     * @brief How coarse the rows a listing reads are: as they were pushed...
     */
    inline constexpr std::string_view ResolutionRaw = "RAW";

    /**
     * @brief ...rolled into hours, or into days.
     */
    inline constexpr std::string_view ResolutionHour = "HOUR";
    inline constexpr std::string_view ResolutionDay = "DAY";

    /**
     * @brief One measurement, on its way to EMO.
     *
     * @par
     * Built through Gauge() and Rate() rather than by filling the type in by hand, because those are
     * the only two values the server reads and the difference between them is what a rollup is.
     */
    struct EUCLID_CDK_API Metric {
        std::string name;
        Labels labels;
        double value{};

        /**
         * @brief TypeGauge or TypeRate.
         */
        std::string type = std::string(TypeGauge);

        /**
         * @brief A value that stands on its own at the moment it was read.
         */
        [[nodiscard]]
        static Metric Gauge(const std::string &name, double value, Labels labels = {});

        /**
         * @brief A value accumulated over the interval this push covers.
         */
        [[nodiscard]]
        static Metric Rate(const std::string &name, double value, Labels labels = {});

        /**
         * @brief This metric as one item of a push.
         */
        [[nodiscard]]
        boost::json::object ToJson() const;
    };

    /**
     * @brief One row a listing answered with: what a series read, and over how many samples.
     *
     * @par
     * "value" is the mean for a gauge and the sum for a rate, which is what the type on the row is
     * for. "minValue" and "maxValue" are the extremes those samples reached, and survive a rollup -
     * so an hourly row still knows the worst second inside it.
     */
    struct EUCLID_CDK_API MetricSample {
        std::string name;
        Labels labels;
        double value{};
        double minValue{};
        double maxValue{};

        /**
         * @brief How many pushed samples this row was made of.
         */
        long samples{};

        /**
         * @brief StoredGauge or StoredRate - upper case, unlike what a push sends.
         */
        std::string type;

        /**
         * @brief ResolutionRaw, ResolutionHour or ResolutionDay.
         */
        std::string resolution;
        std::string timestamp;
    };

    /**
     * @brief Which rows a listing or an average reads.
     *
     * @par
     * Everything is optional and narrows what is read: a query that names nothing takes the most
     * recent rows of every series, which is what a first look at an installation wants and not what
     * a graph does.
     */
    struct EUCLID_CDK_API MetricQuery {

        /**
         * @brief The series' name, matched exactly.
         */
        std::string name;

        /**
         * @brief The dimensions a row has to carry. A row may carry more.
         */
        Labels labels;

        /**
         * @brief The most rows to answer with; the server's own default is 100.
         */
        long limit{0};

        /**
         * @brief The window, as ISO 8601 timestamps. Empty for "as far back as there is" and "up to
         * now".
         */
        std::string from;
        std::string to;

        /**
         * @brief ResolutionRaw, ResolutionHour or ResolutionDay. Empty leaves the choice to the
         * server.
         */
        std::string resolution;

        /**
         * @brief This query as a request body.
         */
        [[nodiscard]]
        boost::json::object ToJson() const;
    };

    /**
     * @brief Reads one row of a listing.
     */
    [[nodiscard]]
    EUCLID_CDK_API MetricSample ToMetricSample(const boost::json::value &value);

    /**
     * @brief Reads a map of labels.
     */
    [[nodiscard]]
    EUCLID_CDK_API Labels ToLabels(const boost::json::value &value, const std::string &name);

}// namespace Euclid::CDK::EMO
