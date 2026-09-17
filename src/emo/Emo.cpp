// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>

// Euclid includes
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

}// namespace Euclid::CDK::EMO
