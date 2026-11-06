// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <optional>
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
     * @brief Which figure of a step an alert rule is measured on.
     *
     * @par
     * StatisticValue is the value itself, and the right answer for a gauge. StatisticMax and
     * StatisticMin matter on a rate or a rolled-up row, where one step holds several readings and
     * "did it ever go above" is a different question from "was the average above".
     */
    inline constexpr std::string_view StatisticValue = "value";
    inline constexpr std::string_view StatisticMax = "max";
    inline constexpr std::string_view StatisticMin = "min";

    /**
     * @brief Which way a threshold is crossed.
     */
    inline constexpr std::string_view ComparisonAbove = "above";
    inline constexpr std::string_view ComparisonBelow = "below";

    /**
     * @brief What an alert's state reads as.
     *
     * @par
     * LevelNoData is not an error and not OK: it is a series that has stopped arriving, which is
     * worth its own level because a metric that went silent looks healthy to anything that only
     * tests thresholds.
     */
    inline constexpr std::string_view LevelOk = "OK";
    inline constexpr std::string_view LevelWarning = "WARNING";
    inline constexpr std::string_view LevelCritical = "CRITICAL";
    inline constexpr std::string_view LevelNoData = "NO_DATA";

    /**
     * @brief One metric series the installation has seen, as a catalogue entry.
     */
    struct EUCLID_CDK_API MetricInfo {
        std::string name;

        /**
         * @brief StoredGauge or StoredRate.
         */
        std::string type;

        /**
         * @brief How many distinct label combinations this name has been pushed under.
         */
        long series{};

        /**
         * @brief The label names it carries, which is what a query can filter on.
         */
        std::vector<std::string> labelNames;
        std::string lastSeen;
    };

    /**
     * @brief A rule saying when a metric is in trouble.
     *
     * @par
     * The two thresholds are optional and independent: a rule may warn without ever going critical,
     * or go straight to critical with no warning level at all. Unset is "no threshold at this
     * level" rather than zero.
     *
     * @par
     * The three period counts are what keep a single noisy step from raising an alert. A rule is
     * measured once per step of its resolution, and nothing changes state until the count is met.
     */
    struct EUCLID_CDK_API AlertRule {

        /**
         * @brief What this rule is managed under, unique within the account.
         */
        std::string ruleId;

        /**
         * @brief The metric name it watches.
         */
        std::string name;

        /**
         * @brief The labels a series has to carry to be covered; empty covers every series of the
         * name.
         */
        std::map<std::string, std::string> labels;

        /**
         * @brief ResolutionRaw, ResolutionHour or ResolutionDay - the step it is measured on.
         */
        std::string resolution;

        /**
         * @brief StatisticValue, StatisticMax or StatisticMin.
         */
        std::string statistic;

        /**
         * @brief ComparisonAbove or ComparisonBelow.
         */
        std::string comparison;

        /**
         * @brief The warning threshold, unset for a rule that has none.
         */
        std::optional<double> warning;

        /**
         * @brief The critical threshold, unset for a rule that has none.
         */
        std::optional<double> critical;

        /**
         * @brief How many consecutive breaching steps raise the alert.
         */
        long forPeriods{1};

        /**
         * @brief How many consecutive healthy steps clear it again.
         */
        long clearPeriods{1};

        /**
         * @brief After how many steps with nothing arriving the series is called LevelNoData; 0
         * never does.
         */
        long noDataAfterPeriods{};
        bool enabled{true};
        std::string description;
        std::string accountId;
        std::string nameSpace;
        std::string created;
        std::string modified;
    };

    /**
     * @brief Where one series of one rule stands now.
     */
    struct EUCLID_CDK_API Alert {
        std::string ruleId;
        std::string name;

        /**
         * @brief The labels of the series this is about - not the rule's filter, which may be
         * broader.
         */
        std::map<std::string, std::string> labels;

        /**
         * @brief LevelOk, LevelWarning, LevelCritical or LevelNoData.
         */
        std::string state;

        /**
         * @brief The same thing in lower case, as a severity word.
         */
        std::string severity;

        /**
         * @brief When it entered this state, which is what says how long it has been wrong.
         */
        std::string since;

        /**
         * @brief The reading that put it here.
         */
        double value{};
        std::string lastSeen;
        std::string statistic;
        std::string comparison;
        std::optional<double> warning;
        std::optional<double> critical;
        std::string description;
        std::string nameSpace;
    };

    /**
     * @brief One change of state, as the history recorded it.
     */
    struct EUCLID_CDK_API AlertTransition {
        std::string ruleId;
        std::string name;
        std::map<std::string, std::string> labels;

        /**
         * @brief The state it came from.
         */
        std::string previous;

        /**
         * @brief The state it went to.
         */
        std::string state;
        std::string severity;
        double value{};

        /**
         * @brief The threshold that was crossed, unset where none applies - entering LevelNoData
         * crosses nothing.
         */
        std::optional<double> threshold;
        std::string statistic;

        /**
         * @brief Why it moved, in the server's own words.
         */
        std::string reason;
        std::string timestamp;
        std::string accountId;
        std::string nameSpace;
    };

    /**
     * @brief A saved dashboard layout.
     *
     * @par
     * The layout is whatever JSON object a client stores, and EMO does not interpret it: panels,
     * queries and arrangement all belong to whatever draws them. A dashboard belongs to the user
     * that saved it.
     */
    struct EUCLID_CDK_API Dashboard {
        std::string name;

        /**
         * @brief The layout, as it was saved. Empty on a listing, which carries names and times
         * only - sending every panel of every dashboard to draw a chooser is a waste of a wire.
         */
        boost::json::object layout;
        std::string created;
        std::string modified;
    };

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
     * @brief Reads one metric catalogue entry.
     */
    [[nodiscard]]
    EUCLID_CDK_API MetricInfo ToMetricInfo(const boost::json::value &value);

    /**
     * @brief Reads an alert rule.
     */
    [[nodiscard]]
    EUCLID_CDK_API AlertRule ToAlertRule(const boost::json::value &value);

    /**
     * @brief Reads one alert state.
     */
    [[nodiscard]]
    EUCLID_CDK_API Alert ToAlert(const boost::json::value &value);

    /**
     * @brief Reads one alert transition.
     */
    [[nodiscard]]
    EUCLID_CDK_API AlertTransition ToAlertTransition(const boost::json::value &value);

    /**
     * @brief Reads a dashboard, with or without its layout.
     */
    [[nodiscard]]
    EUCLID_CDK_API Dashboard ToDashboard(const boost::json::value &value);

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
