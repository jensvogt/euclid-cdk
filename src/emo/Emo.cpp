// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/emo/Emo.h>

namespace Euclid::CDK::EMO {

    Emo::Emo(const EAM::Session &session) : ModuleClient(session, std::string(EMO::Target)) {}

    void Emo::PushMetrics(const std::string &module, const std::vector<Metric> &metrics) const {

        // Nothing to record, and a request that says so is a round trip for nothing - which matters
        // here more than elsewhere, since this one runs on a timer forever.
        if (metrics.empty()) return;

        boost::json::array items;
        items.reserve(metrics.size());
        for (const auto &metric: metrics) items.push_back(metric.ToJson());

        std::ignore = Call("push-metrics", {{"module", module}, {"items", items}});
    }

    std::vector<MetricSample> Emo::List(const MetricQuery &query) const {

        std::vector<MetricSample> samples;
        for (const auto &document: Json::Documents(Call("list", query.ToJson()), "items")) {
            samples.push_back(ToMetricSample(document));
        }
        return samples;
    }

    double Emo::Average(const MetricQuery &query) const {
        return Json::Real(Call("average", query.ToJson()), "average");
    }

    std::vector<MetricInfo> Emo::ListMetrics(const long hours, const std::string &prefix) const {

        if (hours < 1) throw EuclidError("hours has to be at least 1");

        std::vector<MetricInfo> metrics;
        const boost::json::object payload{{"hours", hours}, {"prefix", prefix}};
        for (const auto &document: Json::Documents(Call("list-metrics", payload), "items")) {
            metrics.push_back(ToMetricInfo(document));
        }
        return metrics;
    }

    // -- alert rules --------------------------------------------------------------------------------

    AlertRule Emo::CreateAlertRule(const AlertRule &rule) const {
        return ToAlertRule(Call("create-alert-rule", RuleFields(rule, true)));
    }

    AlertRule Emo::UpdateAlertRule(const AlertRule &rule) const {
        return ToAlertRule(Call("update-alert-rule", RuleFields(rule, false)));
    }

    void Emo::DeleteAlertRule(const std::string &ruleId) const {
        std::ignore = Call("delete-alert-rule", {{"ruleId", ruleId}});
    }

    std::vector<AlertRule> Emo::ListAlertRules(const std::string &ruleId) const {

        std::vector<AlertRule> rules;
        for (const auto &document: Json::Documents(Call("list-alert-rules", {{"ruleId", ruleId}}), "items")) {
            rules.push_back(ToAlertRule(document));
        }
        return rules;
    }

    std::vector<Alert> Emo::ListAlerts(const ListAlertsOptions &options) const {

        const boost::json::object payload{
                {"ruleId", options.ruleId},
                {"state", options.state},
                {"active", options.activeOnly},
        };

        std::vector<Alert> alerts;
        for (const auto &document: Json::Documents(Call("list-alerts", payload), "items")) {
            alerts.push_back(ToAlert(document));
        }
        return alerts;
    }

    std::vector<AlertTransition> Emo::ListAlertHistory(const AlertHistoryOptions &options) const {

        boost::json::object payload{{"ruleId", options.ruleId}, {"limit", options.limit}};
        // Left out when empty rather than sent empty: an empty string is not a timestamp, and the
        // server reads an absent bound as "no bound at this end" rather than as the epoch.
        if (!options.from.empty()) payload["from"] = options.from;
        if (!options.to.empty()) payload["to"] = options.to;

        std::vector<AlertTransition> history;
        for (const auto &document: Json::Documents(Call("list-alert-history", payload), "items")) {
            history.push_back(ToAlertTransition(document));
        }
        return history;
    }

    // -- dashboards ---------------------------------------------------------------------------------

    Dashboard Emo::SaveDashboard(const std::string &name, const boost::json::object &layout) const {
        // The answer carries no layout - it is the one that was just sent - so the name and times
        // come from the response and the layout from the argument.
        auto saved = ToDashboard(Call("save-dashboard", {{"name", name}, {"dashboard", layout}}));
        saved.layout = layout;
        return saved;
    }

    Dashboard Emo::GetDashboard(const std::string &name) const {
        return ToDashboard(Call("get-dashboard", {{"name", name}}));
    }

    std::vector<Dashboard> Emo::ListDashboards() const {

        std::vector<Dashboard> dashboards;
        for (const auto &document: Json::Documents(Call("list-dashboards"), "items")) {
            dashboards.push_back(ToDashboard(document));
        }
        return dashboards;
    }

    void Emo::DeleteDashboard(const std::string &name) const {
        std::ignore = Call("delete-dashboard", {{"name", name}});
    }

    boost::json::object Emo::RuleFields(const AlertRule &rule, const bool whole) {

        boost::json::object payload{{"ruleId", rule.ruleId}};

        // An update merges onto the stored rule, so a field that says nothing has to stay away or
        // it would overwrite what is there with a default. A create sends everything, because
        // there is nothing to merge onto.
        const auto wanted = [whole](const bool given) { return whole || given; };

        if (wanted(!rule.name.empty())) payload["name"] = rule.name;
        if (wanted(!rule.labels.empty())) {
            boost::json::object labels;
            for (const auto &[key, value]: rule.labels) labels[key] = value;
            payload["labels"] = labels;
        }
        if (wanted(!rule.resolution.empty())) payload["resolution"] = rule.resolution;
        if (wanted(!rule.statistic.empty())) payload["statistic"] = rule.statistic;
        if (wanted(!rule.comparison.empty())) payload["comparison"] = rule.comparison;
        if (wanted(!rule.description.empty())) payload["description"] = rule.description;

        // null rather than left out when a threshold is unset on a create: that is how the server
        // is told there is no threshold at this level, and leaving it out would read the same but
        // only by luck. On an update, unset means "leave it alone" and nothing is sent.
        if (rule.warning.has_value()) {
            payload["warning"] = *rule.warning;
        } else if (whole) {
            payload["warning"] = nullptr;
        }
        if (rule.critical.has_value()) {
            payload["critical"] = *rule.critical;
        } else if (whole) {
            payload["critical"] = nullptr;
        }

        // The counts have no "unset": every value is meaningful, so they travel on a create and on
        // an update alike. A caller changing one field reads the rule first, which is what
        // UpdateAlertRule() says to do.
        if (whole || rule.forPeriods > 0) payload["forPeriods"] = rule.forPeriods;
        if (whole || rule.clearPeriods > 0) payload["clearPeriods"] = rule.clearPeriods;
        if (whole || rule.noDataAfterPeriods > 0) payload["noDataAfterPeriods"] = rule.noDataAfterPeriods;
        payload["enabled"] = rule.enabled;

        return payload;
    }

}// namespace Euclid::CDK::EMO
