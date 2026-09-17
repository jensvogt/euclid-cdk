// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <utility>

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

}// namespace Euclid::CDK::EMO
