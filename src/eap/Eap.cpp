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
