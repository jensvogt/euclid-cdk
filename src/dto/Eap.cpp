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

    InfrastructureChange ToInfrastructureChange(const boost::json::value &value) {
        return {.ern = Json::Text(value, "ern"), .changes = Json::Strings(value, "changes")};
    }

    InfrastructureKept ToInfrastructureKept(const boost::json::value &value) {
        return {.ern = Json::Text(value, "ern"), .reason = Json::Text(value, "reason")};
    }

    InfrastructureResult ToInfrastructureResult(const boost::json::value &value) {

        InfrastructureResult result{
                .applicationId = Json::Text(value, "applicationId"),
                .platform = Json::Text(value, "platform"),
                .declared = Json::Flag(value, "declared"),
                .dryRun = Json::Flag(value, "dryRun"),
                .created = Json::Strings(value, "created"),
                .adopted = Json::Strings(value, "adopted"),
                .updated = {},
                .deleted = Json::Strings(value, "deleted"),
                .kept = {},
                .granted = Json::Strings(value, "granted"),
                .revoked = Json::Strings(value, "revoked"),
        };

        for (const auto &document: Json::Documents(value, "updated")) {
            result.updated.push_back(ToInfrastructureChange(document));
        }
        for (const auto &document: Json::Documents(value, "kept")) {
            result.kept.push_back(ToInfrastructureKept(document));
        }
        return result;
    }

    Platform ToPlatform(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .nameSpace = Json::Text(value, "namespace"),
                .owns = Json::Strings(value, "owns"),
                // A value rather than an object: the server sends null for a stored declaration
                // whose text will not parse, which is a record worth seeing rather than hiding.
                .declaration = Json::Child(value, "declaration"),
                .modified = Json::Text(value, "modified"),
        };
    }

    EgressLimits ToEgressLimits(const boost::json::value &value) {
        return {
                .requestsPerSecond = Json::Real(value, "requestsPerSecond"),
                .burst = Json::Real(value, "burst"),
                .concurrent = Json::Number(value, "concurrent"),
        };
    }

    EgressSite ToEgressSite(const boost::json::value &value) {
        return {
                .applicationId = Json::Text(value, "applicationId"),
                .site = Json::Text(value, "site"),
                .limits = ToEgressLimits(Json::Child(value, "limits")),
                .limitsSet = Json::Flag(value, "limitsSet"),
                .lastUsed = Json::Text(value, "lastUsed"),
        };
    }

    EgressSites ToEgressSites(const boost::json::value &value) {

        EgressSites sites{.defaultLimits = ToEgressLimits(Json::Child(value, "defaultLimits")), .sites = {}};
        for (const auto &document: Json::Documents(value, "sites")) {
            sites.sites.push_back(ToEgressSite(document));
        }
        return sites;
    }

    EgressPermit ToEgressPermit(const boost::json::value &value) {
        return {
                .applicationId = Json::Text(value, "applicationId"),
                .site = Json::Text(value, "site"),
                .granted = Json::Flag(value, "granted"),
                .limits = ToEgressLimits(Json::Child(value, "limits")),
                // Sent on a grant only; the next two on a refusal only.
                .permitId = Json::Text(value, "permitId"),
                .expiresAt = Json::Text(value, "expiresAt"),
                .reason = Json::Text(value, "reason"),
                .retryAfterMs = Json::Number(value, "retryAfterMs"),
        };
    }

    EgressRelease ToEgressRelease(const boost::json::value &value) {
        return {
                .applicationId = Json::Text(value, "applicationId"),
                .site = Json::Text(value, "site"),
                .released = Json::Flag(value, "released"),
        };
    }

    EgressThrottled ToEgressThrottled(const boost::json::value &value) {
        return {
                .applicationId = Json::Text(value, "applicationId"),
                .site = Json::Text(value, "site"),
                .blockedUntil = Json::Text(value, "blockedUntil"),
        };
    }

    RebalanceResult ToRebalanceResult(const boost::json::value &value) {
        return {
                .applicationId = Json::Text(value, "applicationId"),
                .rebalancing = Json::Flag(value, "rebalancing"),
                .instances = Json::Number(value, "instances"),
        };
    }

    DrainResult ToDrainResult(const boost::json::value &value) {
        return {.node = Json::Text(value, "node"), .drained = Json::Flag(value, "drained")};
    }

    NodeRegistration ToNodeRegistration(const boost::json::value &value) {
        return {
                .node = ToNode(Json::Child(value, "node")),
                .leaseSeconds = Json::Number(value, "leaseSeconds"),
        };
    }

    AssignedInstance ToAssignedInstance(const boost::json::value &value) {
        return {
                .instanceId = Json::Text(value, "instanceId"),
                .applicationId = Json::Text(value, "applicationId"),
                .runtimeName = Json::Text(value, "runtimeName"),
                .revision = Json::Text(value, "revision"),
                .leaseExpiresAt = Json::Text(value, "leaseExpiresAt"),
                .bucketErn = Json::Text(value, "bucketErn"),
                .artifactKey = Json::Text(value, "artifactKey"),
                .artifactSize = Json::Number(value, "artifactSize"),
                .md5Sum = Json::Text(value, "md5Sum"),
                .runtime = Json::Text(value, "runtime"),
                .environment = Json::StringMap(value, "environment"),
                .accountId = Json::Text(value, "accountId"),
                // "nameSpace" on this one, unlike the rest of this module - it is what the worker
                // labels the instance's output with rather than a field of the application row.
                .nameSpace = Json::Text(value, "nameSpace"),
                .logLevel = Json::Text(value, "logLevel"),
                .memoryLimitMb = Json::Number(value, "memoryLimitMb"),
                .cpuLimit = Json::Real(value, "cpuLimit"),
        };
    }

    NodeRenewal ToNodeRenewal(const boost::json::value &value) {

        NodeRenewal renewal{
                .node = Json::Text(value, "node"),
                .leaseSeconds = Json::Number(value, "leaseSeconds"),
                .leaseExpiresAt = Json::Text(value, "leaseExpiresAt"),
                .instances = {},
        };
        for (const auto &document: Json::Documents(value, "instances")) {
            renewal.instances.push_back(ToAssignedInstance(document));
        }
        return renewal;
    }

    InstanceAssignment ToInstanceAssignment(const boost::json::value &value) {
        return {
                .runtimeName = Json::Text(value, "runtimeName"),
                .instanceId = Json::Text(value, "instanceId"),
                .node = Json::Text(value, "node"),
                .leaseExpiresAt = Json::Text(value, "leaseExpiresAt"),
        };
    }

    NodeApplication ToNodeApplication(const boost::json::value &value) {
        return {
                .applicationId = Json::Text(value, "applicationId"),
                .runtimeName = Json::Text(value, "runtimeName"),
                // "namespace" here, as everywhere in this module.
                .nameSpace = Json::Text(value, "namespace"),
                .runtime = Json::Text(value, "runtime"),
                .instances = Json::Number(value, "instances"),
                .running = Json::Number(value, "running"),
        };
    }

    Node ToNode(const boost::json::value &value) {

        Node node{
                .name = Json::Text(value, "name"),
                .address = Json::Text(value, "address"),
                .principal = Json::Text(value, "principal"),
                .labels = Json::StringMap(value, "labels"),
                .cpuCount = Json::Number(value, "cpuCount"),
                // Real rather than Number: a load average of 0.5 is a real reading, and reading it
                // as a long would report it as nothing at all.
                .loadAverage = Json::Real(value, "loadAverage"),
                .version = Json::Text(value, "version"),
                .os = Json::Text(value, "os"),
                .arch = Json::Text(value, "arch"),
                .drained = Json::Flag(value, "drained"),
                .live = Json::Flag(value, "live"),
                .lastSeen = Json::Text(value, "lastSeen"),
                .applications = {},
        };

        for (const auto &document: Json::Documents(value, "applications")) {
            node.applications.push_back(ToNodeApplication(document));
        }
        return node;
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
