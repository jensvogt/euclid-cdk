// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <cstdlib>
#include <tuple>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/eap/Eap.h>

namespace Euclid::CDK::EAP {

    namespace {

        /**
         * @brief A JSON array of strings, for the payload fields that take a list.
         */
        boost::json::array stringsOf(const std::vector<std::string> &values) {
            boost::json::array array;
            for (const auto &value: values) array.emplace_back(value);
            return array;
        }

        /**
         * @brief A JSON object of strings, for the environment.
         */
        boost::json::object mapOf(const std::map<std::string, std::string> &values) {
            boost::json::object json;
            for (const auto &[name, value]: values) json[name] = value;
            return json;
        }

        /**
         * @brief A field of an update, sent only when the caller named it.
         *
         * @par
         * The server distinguishes a field being sent from one that is not, rather than one value
         * from another, so an empty string that was asked for has to travel and one that was not has
         * to stay away.
         */
        template<typename T>
        void put(boost::json::object &payload, const std::string &field, const std::optional<T> &value) {
            if (!value.has_value()) return;
            if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                payload[field] = stringsOf(*value);
            } else if constexpr (std::is_same_v<T, std::map<std::string, std::string>>) {
                payload[field] = mapOf(*value);
            } else {
                payload[field] = *value;
            }
        }

    }// namespace

    Eap::Eap(const EAM::Session &session) : ModuleClient(session, std::string(EAP::Target)) {}

    // -- deploying ----------------------------------------------------------------------------------

    Application Eap::CreateApplication(const std::string &applicationId, const std::string &runtime,
                                       const std::string &bucket, const std::string &artifact,
                                       const CreateApplicationOptions &options) const {
        const boost::json::object payload{
                {"applicationId", applicationId},
                {"runtime", runtime},
                {"bucket", bucket},
                {"artifact", artifact},
                {"version", options.version},
                {"command", options.command},
                {"arguments", stringsOf(options.arguments)},
                {"environment", mapOf(options.environment)},
                {"buckets", stringsOf(options.buckets)},
                {"queues", stringsOf(options.queues)},
                {"user", options.user},
                {"minInstances", options.minInstances},
                {"maxInstances", options.maxInstances},
                {"readyTimeoutMs", options.readyTimeoutMs},
        };
        return ApplicationOf("create-application", payload);
    }

    Application Eap::UpdateApplication(const std::string &applicationId, const UpdateApplicationOptions &options) const {

        boost::json::object payload{{"applicationId", applicationId}};
        put(payload, "runtime", options.runtime);
        put(payload, "artifact", options.artifact);
        put(payload, "version", options.version);
        put(payload, "command", options.command);
        put(payload, "arguments", options.arguments);
        put(payload, "environment", options.environment);
        put(payload, "buckets", options.buckets);
        put(payload, "queues", options.queues);
        put(payload, "minInstances", options.minInstances);
        put(payload, "maxInstances", options.maxInstances);
        put(payload, "readyTimeoutMs", options.readyTimeoutMs);
        // "namespace" on the wire, as everywhere in this module.
        put(payload, "namespace", options.nameSpace);

        return ApplicationOf("update-application", payload);
    }

    Application Eap::CopyApplication(const std::string &applicationId, const std::string &targetNameSpace,
                                     const std::string &targetApplicationId) const {

        boost::json::object payload{
                {"applicationId", applicationId},
                {"targetNamespace", targetNameSpace},
        };
        // Sent only when named: the server reads an absent one as "the original's name", and an
        // empty string is not the same thing.
        if (!targetApplicationId.empty()) payload["targetApplicationId"] = targetApplicationId;

        return ApplicationOf("copy-application", payload);
    }

    Application Eap::ScaleApplication(const std::string &applicationId, const ScaleApplicationOptions &options) const {

        boost::json::object payload{{"applicationId", applicationId}};

        // Sent only when asked for: the server reads an absent bound as "leave it as it stands",
        // and -1 is this struct's way of saying the same thing.
        if (options.minInstances >= 0) payload["minInstances"] = options.minInstances;
        if (options.maxInstances >= 0) payload["maxInstances"] = options.maxInstances;

        return ApplicationOf("scale-application", payload);
    }

    Application Eap::RedeployApplication(const std::string &applicationId, const std::string &artifact, const std::string &version) const {

        boost::json::object payload{{"applicationId", applicationId}};
        // Left out rather than sent empty: absent is what asks for the artifact already deployed and
        // the version its name implies, which is what a rebuild under the same key wants.
        if (!artifact.empty()) payload["artifact"] = artifact;
        if (!version.empty()) payload["version"] = version;

        return ApplicationOf("redeploy-application", payload);
    }

    void Eap::DeleteApplication(const std::string &applicationId) const {
        std::ignore = Call("delete-application", {{"applicationId", applicationId}});
    }

    // -- running ------------------------------------------------------------------------------------

    Application Eap::StartApplication(const std::string &applicationId) const {
        return ApplicationOf("start-application", {{"applicationId", applicationId}});
    }

    Application Eap::StopApplication(const std::string &applicationId) const {
        return ApplicationOf("stop-application", {{"applicationId", applicationId}});
    }

    RestartResult Eap::RestartApplication(const std::string &applicationId) const {
        return ToRestartResult(Call("restart-application", {{"applicationId", applicationId}}));
    }

    std::vector<Application> Eap::ListApplications(const std::string &prefix) const {
        std::vector<Application> applications;
        for (const auto &document: Json::Documents(Call("list-applications", {{"prefix", prefix}}), "applications")) {
            applications.push_back(ToApplication(document));
        }
        return applications;
    }

    Application Eap::GetApplication(const std::string &applicationId) const {
        return ApplicationOf("get-application", {{"applicationId", applicationId}});
    }

    Application Eap::SetApplicationLimits(const std::string &applicationId, const ApplicationLimitOptions &options) const {

        // Refused here because the server refuses it too, and the argument says so without a round
        // trip: -1 for both asks for no change at all.
        if (options.memoryLimitMb < 0 && options.cpuLimit < 0.0) {
            throw EuclidError("set at least one of memoryLimitMb or cpuLimit - -1 leaves a ceiling as it stands, "
                              "and 0 removes it");
        }

        boost::json::object payload{{"applicationId", applicationId}};
        // Sent only when asked for: the server reads an absent ceiling as "leave it alone", and
        // zero is a decision - it removes the ceiling - so the test is against zero.
        if (options.memoryLimitMb >= 0) payload["memoryLimitMb"] = options.memoryLimitMb;
        if (options.cpuLimit >= 0.0) payload["cpuLimit"] = options.cpuLimit;

        return ApplicationOf("set-application-limits", payload);
    }

    RebalanceResult Eap::RebalanceApplication(const std::string &applicationId) const {
        return ToRebalanceResult(Call("rebalance-application", {{"applicationId", applicationId}}));
    }

    InfrastructureResult Eap::PlanInfrastructure(const std::string &applicationId) const {
        return ToInfrastructureResult(Call("plan-infrastructure", {{"applicationId", applicationId}}));
    }

    // -- platforms ----------------------------------------------------------------------------------

    InfrastructureResult Eap::ApplyPlatform(const std::string &name, const boost::json::value &declaration,
                                            const bool prune) const {
        return ToInfrastructureResult(Call("apply-platform", PlatformPayload(name, declaration, prune)));
    }

    InfrastructureResult Eap::PlanPlatform(const std::string &name, const boost::json::value &declaration,
                                           const bool prune) const {
        return ToInfrastructureResult(Call("plan-platform", PlatformPayload(name, declaration, prune)));
    }

    Platform Eap::GetPlatform(const std::string &name) const {
        return ToPlatform(Call("get-platform", {{"name", name}}));
    }

    std::vector<Platform> Eap::ListPlatforms() const {
        std::vector<Platform> platforms;
        for (const auto &document: Json::Documents(Call("list-platforms"), "platforms")) {
            platforms.push_back(ToPlatform(document));
        }
        return platforms;
    }

    // -- egress -------------------------------------------------------------------------------------

    EgressPermit Eap::AcquireEgressPermit(const std::string &site, const EgressPermitOptions &options) const {
        const boost::json::object payload{{"site", site}, {"leaseSeconds", options.leaseSeconds.count()}};
        return ToEgressPermit(Call("acquire-egress-permit", payload));
    }

    EgressRelease Eap::ReleaseEgressPermit(const std::string &site, const std::string &permitId) const {
        const boost::json::object payload{{"site", site}, {"permitId", permitId}};
        return ToEgressRelease(Call("release-egress-permit", payload));
    }

    EgressThrottled Eap::ReportEgressThrottled(const std::string &site, const long retryAfterSeconds) const {
        const boost::json::object payload{{"site", site}, {"retryAfterSeconds", retryAfterSeconds}};
        return ToEgressThrottled(Call("report-egress-throttled", payload));
    }

    EgressSite Eap::SetEgressLimit(const std::string &site, const EgressLimits &limits,
                                   const std::string &applicationId) const {

        boost::json::object payload{
                {"site", site},
                {"requestsPerSecond", limits.requestsPerSecond},
                {"burst", limits.burst},
                {"concurrent", limits.concurrent},
        };
        // Left out when empty: absent is what asks about the calling application itself, which is
        // what an application setting its own limits means.
        if (!applicationId.empty()) payload["applicationId"] = applicationId;

        return ToEgressSite(Call("set-egress-limit", payload));
    }

    EgressSite Eap::DeleteEgressLimit(const std::string &site, const std::string &applicationId) const {
        boost::json::object payload{{"site", site}};
        if (!applicationId.empty()) payload["applicationId"] = applicationId;
        return ToEgressSite(Call("delete-egress-limit", payload));
    }

    EgressSites Eap::ListEgressSites(const std::string &applicationId) const {
        boost::json::object payload;
        if (!applicationId.empty()) payload["applicationId"] = applicationId;
        return ToEgressSites(Call("list-egress-sites", payload));
    }

    void Eap::DeleteEgressSite(const std::string &site, const std::string &applicationId) const {
        boost::json::object payload{{"site", site}};
        if (!applicationId.empty()) payload["applicationId"] = applicationId;
        std::ignore = Call("delete-egress-site", payload);
    }

    // -- nodes --------------------------------------------------------------------------------------

    std::vector<Node> Eap::ListNodes() const {
        std::vector<Node> nodes;
        for (const auto &document: Json::Documents(Call("list-nodes"), "nodes")) {
            nodes.push_back(ToNode(document));
        }
        return nodes;
    }

    Node Eap::GetNode(const std::string &node) const {
        return ToNode(Call("get-node", {{"node", node}}));
    }

    void Eap::DeleteNode(const std::string &node) const {
        std::ignore = Call("delete-node", {{"node", node}});
    }

    NodeRegistration Eap::RegisterNode(const std::string &node, const RegisterNodeOptions &options) const {

        boost::json::object labels;
        for (const auto &[key, value]: options.labels) labels[key] = value;

        const boost::json::object payload{
                {"node", node},
                {"address", options.address},
                {"version", options.version},
                {"os", options.os},
                {"arch", options.arch},
                {"labels", std::move(labels)},
        };
        return ToNodeRegistration(Call("register-node", payload));
    }

    NodeRenewal Eap::RenewNode(const std::string &node, const RenewNodeOptions &options) const {

        boost::json::object payload{{"node", node}, {"loadAverage", options.loadAverage}};
        // Sent only when the worker measured them: the server reads an absent figure as "a worker
        // older than the field" rather than as a machine with no memory, and zero would be read as
        // the former anyway.
        if (options.memoryTotalMb > 0) payload["memoryTotalMb"] = options.memoryTotalMb;
        if (options.memoryUsedMb > 0) payload["memoryUsedMb"] = options.memoryUsedMb;

        return ToNodeRenewal(Call("renew-node", payload));
    }

    DrainResult Eap::DrainNode(const std::string &node, const bool drained) const {
        // Always sent, both ways: absent means drain, because that is what the action is called, so
        // putting a node back has to say so explicitly.
        return ToDrainResult(Call("drain-node", {{"node", node}, {"drained", drained}}));
    }

    InstanceAssignment Eap::AssignInstance(const std::string &runtimeName, const std::string &instanceId,
                                           const std::string &node) const {

        // The node travels even when empty, because empty is what takes the slot back to the
        // manager's own host - a legitimate thing to ask for, and not the same as not saying.
        const boost::json::object payload{
                {"runtimeName", runtimeName},
                {"instanceId", instanceId},
                {"node", node},
        };
        return ToInstanceAssignment(Call("assign-instance", payload));
    }

    void Eap::ReportNodeInstance(const std::string &runtimeName, const std::string &instanceId,
                                 const ReportInstanceOptions &options) const {

        boost::json::object payload{
                {"runtimeName", runtimeName},
                {"instanceId", instanceId},
                {"httpPort", options.httpPort},
                {"pid", options.pid},
        };
        // Left out when empty: the server reads an absent state as RUNNING, and an empty string
        // would parse as no state at all.
        if (!options.state.empty()) payload["state"] = options.state;
        if (!options.startedAt.empty()) payload["startedAt"] = options.startedAt;
        if (!options.host.empty()) payload["host"] = options.host;

        std::ignore = Call("report-node-instance", payload);
    }

    boost::json::object Eap::IssueInstanceCredentials(const std::string &runtimeName, const std::string &instanceId) const {
        const boost::json::object payload{{"runtimeName", runtimeName}, {"instanceId", instanceId}};
        return Call("issue-instance-credentials", payload);
    }

    boost::json::object Eap::PlatformPayload(const std::string &name, const boost::json::value &declaration,
                                             const bool prune) {
        return {{"name", name}, {"declaration", declaration}, {"prune", prune}};
    }

    // -- infrastructure -----------------------------------------------------------------------------

    InfrastructureResult Eap::ApplyInfrastructure(const std::string &applicationId) const {
        return ToInfrastructureResult(Call("apply-infrastructure", {{"applicationId", applicationId}}));
    }

    // -- logging ------------------------------------------------------------------------------------

    LogLevelResult Eap::SetLogLevel(const std::string &applicationId, const std::string &level) const {
        return ToLogLevelResult(Call("set-log-level", {{"applicationId", applicationId}, {"level", level}}));
    }

    LogLevelResult Eap::ResetLogLevel(const std::string &applicationId) const {
        // An empty level is what takes the override off rather than setting one.
        return SetLogLevel(applicationId, "");
    }

    // -- reporting for itself -----------------------------------------------------------------------

    LoadReport Eap::ReportLoad(const std::string &applicationId, const LoadReportOptions &options) const {

        const auto instanceId = options.instanceId.empty() ? InstanceId() : options.instanceId;
        if (instanceId.empty()) {
            throw EuclidError("a load report has to say which instance it came from - set " +
                              std::string(InstanceIdVariable) + ", which euclid's manager does for an "
                                                                "application it started, or name one in LoadReportOptions::instanceId");
        }

        const boost::json::object payload{
                // Always sent. The server can guess it from a caller named "app-<something>", and
                // that guess is wrong whenever an application's id and the identity it runs as
                // differ - which is silent, because the guess still answers 200.
                {"applicationId", applicationId},
                {"instanceId", instanceId},
                {"utilisation", options.utilisation},
                {"backlog", options.backlog},
                {"active", options.active},
        };
        return ToLoadReport(Call("report-load", payload));
    }

    std::string Eap::InstanceId() {
        const char *value = std::getenv(std::string(InstanceIdVariable).c_str());
        return value == nullptr ? std::string{} : std::string(value);
    }

    // -- monitoring ---------------------------------------------------------------------------------

    boost::json::object Eap::Metrics() const {
        return Call("get-metrics");
    }

    Application Eap::ApplicationOf(const std::string &action, const boost::json::object &payload) const {
        return ToApplication(Call(action, payload));
    }

}// namespace Euclid::CDK::EAP
