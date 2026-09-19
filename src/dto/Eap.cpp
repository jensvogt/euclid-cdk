// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Eap.h>
#include <euclid/cdk/eap/Eap.h>

namespace Euclid::CDK::EAP {

    bool Application::IsRunning() const { return state == StateRunning; }

    Endpoint ToEndpoint(const boost::json::value &value) {
        return {
                .instanceId = Json::Text(value, "instanceId"),
                .pid = Json::Number(value, "pid"),
                .httpPort = Json::Number(value, "httpPort"),
        };
    }

    Application ToApplication(const boost::json::value &value) {

        Application application{
                .applicationId = Json::Text(value, "applicationId"),
                .runtimeName = Json::Text(value, "runtimeName"),
                .ern = Json::Text(value, "ern"),
                .accountId = Json::Text(value, "accountId"),
                // "namespace" here and in ETS; every other module spells it "nameSpace".
                .nameSpace = Json::Text(value, "namespace"),
                .region = Json::Text(value, "region"),
                .runtime = Json::Text(value, "runtime"),
                .bucketErn = Json::Text(value, "bucketErn"),
                .artifactKey = Json::Text(value, "artifactKey"),
                .version = Json::Text(value, "version"),
                .md5Sum = Json::Text(value, "md5Sum"),
                .command = Json::Text(value, "command"),
                .arguments = Json::Strings(value, "arguments"),
                .environment = Json::StringMap(value, "environment"),
                .resources = Json::Strings(value, "resources"),
                .userId = Json::Text(value, "userId"),
                .logLevel = Json::Text(value, "logLevel"),
                .minInstances = Json::Number(value, "minInstances"),
                .maxInstances = Json::Number(value, "maxInstances"),
                .readyTimeoutMs = Json::Number(value, "readyTimeoutMs"),
                .desiredState = Json::Text(value, "desiredState"),
                .state = Json::Text(value, "state"),
                .instances = Json::Number(value, "instances"),
                .endpoints = {},
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };

        for (const auto &document: Json::Documents(value, "endpoints")) {
            application.endpoints.push_back(ToEndpoint(document));
        }
        return application;
    }

    LogLevelResult ToLogLevelResult(const boost::json::value &value) {
        return {
                .applicationId = Json::Text(value, "applicationId"),
                .logLevel = Json::Text(value, "logLevel"),
                .channel = Json::Text(value, "channel"),
        };
    }

    RestartResult ToRestartResult(const boost::json::value &value) {
        return {
                .applicationId = Json::Text(value, "applicationId"),
                .restarting = Json::Flag(value, "restarting"),
                .instances = Json::Number(value, "instances"),
        };
    }

    LoadReport ToLoadReport(const boost::json::value &value) {
        return {
                .instanceId = Json::Text(value, "instanceId"),
                // A measurement rather than a count, so read as one: 42.5% is not 42%.
                .utilisation = Json::Real(value, "utilisation"),
                .backlog = Json::Number(value, "backlog"),
                .active = Json::Number(value, "active"),
        };
    }

}// namespace Euclid::CDK::EAP
