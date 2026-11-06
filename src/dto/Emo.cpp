// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <utility>

// C++ includes
#include <optional>

// Euclid includes
#include <euclid/cdk/dto/Emo.h>

namespace Euclid::CDK::EMO {

    namespace {

        /**
         * @brief Labels as the server reads them: an object of strings.
         */
        boost::json::object labelsToJson(const Labels &labels) {
            boost::json::object json;
            for (const auto &[name, value]: labels) json[name] = value;
            return json;
        }

    }// namespace

    Metric Metric::Gauge(const std::string &name, const double value, Labels labels) {
        return {.name = name, .labels = std::move(labels), .value = value, .type = std::string(TypeGauge)};
    }

    Metric Metric::Rate(const std::string &name, const double value, Labels labels) {
        return {.name = name, .labels = std::move(labels), .value = value, .type = std::string(TypeRate)};
    }

    boost::json::object Metric::ToJson() const {
        return {
                {"name", name},
                // The map rather than the older labelName/labelValue pair, which the server still
                // accepts and reads as one more dimension. Sending both would only repeat the first.
                {"labels", labelsToJson(labels)},
                {"value", value},
                {"type", type},
        };
    }

    boost::json::object MetricQuery::ToJson() const {

        boost::json::object json;
        // Each field is left out entirely when it says nothing: the server reads an absent field as
        // "do not narrow by this", and an empty string as a name that matches nothing.
        if (!name.empty()) json["name"] = name;
        if (!labels.empty()) json["labels"] = labelsToJson(labels);
        if (limit > 0) json["limit"] = limit;
        if (!from.empty()) json["from"] = from;
        if (!to.empty()) json["to"] = to;
        if (!resolution.empty()) json["resolution"] = resolution;
        return json;
    }

    Labels ToLabels(const boost::json::value &value, const std::string &name) {
        Labels labels;
        for (const auto &[key, held]: Json::Object(Json::Child(value, name))) {
            if (held.is_string()) labels.emplace(key, held.as_string());
        }
        return labels;
    }

    MetricSample ToMetricSample(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .labels = ToLabels(value, "labels"),
                .value = Json::Real(value, "value"),
                .minValue = Json::Real(value, "minValue"),
                .maxValue = Json::Real(value, "maxValue"),
                .samples = Json::Number(value, "samples"),
                .type = Json::Text(value, "type"),
                .resolution = Json::Text(value, "resolution"),
                .timestamp = Json::Text(value, "timestamp"),
        };
    }

    namespace {

        /**
         * @brief A threshold, which is absent on a rule that has none and null on the wire.
         *
         * @par
         * Told apart from zero deliberately: zero is a threshold somebody set, and reading an
         * absent one as zero would make every rule without a warning level warn on everything.
         */
        std::optional<double> optionalNumber(const boost::json::value &value, const std::string &name) {
            const auto object = Json::Object(value);
            const auto *field = object.if_contains(name);
            if (field == nullptr || field->is_null() || !field->is_number()) return std::nullopt;
            return field->to_number<double>();
        }

    }// namespace

    MetricInfo ToMetricInfo(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .type = Json::Text(value, "type"),
                .series = Json::Number(value, "series"),
                .labelNames = Json::Strings(value, "labelNames"),
                .lastSeen = Json::Text(value, "lastSeen"),
        };
    }

    AlertRule ToAlertRule(const boost::json::value &value) {
        return {
                .ruleId = Json::Text(value, "ruleId"),
                .name = Json::Text(value, "name"),
                .labels = Json::StringMap(value, "labels"),
                .resolution = Json::Text(value, "resolution"),
                .statistic = Json::Text(value, "statistic"),
                .comparison = Json::Text(value, "comparison"),
                .warning = optionalNumber(value, "warning"),
                .critical = optionalNumber(value, "critical"),
                // The server's own defaults, so a rule that said nothing reads as one step rather
                // than as zero - which would raise and clear on every step.
                .forPeriods = Json::Number(value, "forPeriods", 1),
                .clearPeriods = Json::Number(value, "clearPeriods", 1),
                .noDataAfterPeriods = Json::Number(value, "noDataAfterPeriods"),
                .enabled = Json::Flag(value, "enabled", true),
                .description = Json::Text(value, "description"),
                .accountId = Json::Text(value, "accountId"),
                // "namespace" on the wire here too.
                .nameSpace = Json::Text(value, "namespace"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Alert ToAlert(const boost::json::value &value) {
        return {
                .ruleId = Json::Text(value, "ruleId"),
                .name = Json::Text(value, "name"),
                .labels = Json::StringMap(value, "labels"),
                .state = Json::Text(value, "state"),
                .severity = Json::Text(value, "severity"),
                .since = Json::Text(value, "since"),
                .value = Json::Real(value, "value"),
                .lastSeen = Json::Text(value, "lastSeen"),
                .statistic = Json::Text(value, "statistic"),
                .comparison = Json::Text(value, "comparison"),
                .warning = optionalNumber(value, "warning"),
                .critical = optionalNumber(value, "critical"),
                .description = Json::Text(value, "description"),
                .nameSpace = Json::Text(value, "namespace"),
        };
    }

    AlertTransition ToAlertTransition(const boost::json::value &value) {
        return {
                .ruleId = Json::Text(value, "ruleId"),
                .name = Json::Text(value, "name"),
                .labels = Json::StringMap(value, "labels"),
                .previous = Json::Text(value, "previous"),
                .state = Json::Text(value, "state"),
                .severity = Json::Text(value, "severity"),
                .value = Json::Real(value, "value"),
                .threshold = optionalNumber(value, "threshold"),
                .statistic = Json::Text(value, "statistic"),
                .reason = Json::Text(value, "reason"),
                .timestamp = Json::Text(value, "timestamp"),
                .accountId = Json::Text(value, "accountId"),
                .nameSpace = Json::Text(value, "namespace"),
        };
    }

    Dashboard ToDashboard(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                // Absent on a listing, which carries names and times only.
                .layout = Json::Object(Json::Child(value, "dashboard")),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

}// namespace Euclid::CDK::EMO
