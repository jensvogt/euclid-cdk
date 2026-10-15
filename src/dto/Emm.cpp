// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Emm.h>

namespace Euclid::CDK::EMM {

    ModuleInstance ToModuleInstance(const boost::json::value &value) {
        return {
                .instanceId = Json::Text(value, "instanceId"),
                .pid = Json::Number(value, "pid"),
                .host = Json::Text(value, "host"),
                .state = Json::Text(value, "state"),
                .socketPath = Json::Text(value, "socketPath"),
                .httpPort = Json::Number(value, "httpPort"),
                .restartCount = Json::Number(value, "restartCount"),
                // -1 is "never reported", which is not the same as reporting nothing - so the
                // default has to be -1 rather than 0, or a server that omitted the field would be
                // read as an instance reporting itself idle.
                .utilisation = Json::Number(value, "utilisation", -1),
                .backlog = Json::Number(value, "backlog", -1),
                .loadReportedAt = Json::Text(value, "loadReportedAt"),
                .backgroundTasks = Json::Number(value, "backgroundTasks"),
                .started = Json::Text(value, "started"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Module ToModule(const boost::json::value &value) {

        Module module{
                .name = Json::Text(value, "name"),
                .executable = Json::Text(value, "executable"),
                .socketPath = Json::Text(value, "socketPath"),
                .active = Json::Flag(value, "active"),
                .core = Json::Flag(value, "core"),
                .desiredStopped = Json::Flag(value, "desiredStopped"),
                .autoRestart = Json::Flag(value, "autoRestart"),
                .maxRestarts = Json::Number(value, "maxRestarts"),
                .minInstances = Json::Number(value, "minInstances"),
                .maxInstances = Json::Number(value, "maxInstances"),
                // -1 is "nothing pending" here, the same as the server writes.
                .desiredMinInstances = Json::Number(value, "desiredMinInstances", -1),
                .desiredMaxInstances = Json::Number(value, "desiredMaxInstances", -1),
                .desiredThreads = Json::Number(value, "desiredThreads", -1),
                .logLevel = Json::Text(value, "logLevel"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
                // Sent as null rather than omitted for a module that has never started, which
                // Json::Text reads as empty either way.
                .lastStartTime = Json::Text(value, "lastStartTime"),
                .instances = {},
        };

        for (const auto &document: Json::Documents(value, "instances")) {
            module.instances.push_back(ToModuleInstance(document));
        }
        return module;
    }

    ModulesResult ToModulesResult(const boost::json::value &value) {

        ModulesResult result{.modules = {}, .total = Json::Number(value, "total")};
        for (const auto &document: Json::Documents(value, "modules")) {
            result.modules.push_back(ToModule(document));
        }
        return result;
    }

    InstanceLimits ToInstanceLimits(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .minInstances = Json::Number(value, "minInstances"),
                .maxInstances = Json::Number(value, "maxInstances"),
        };
    }

    ThreadCount ToThreadCount(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .threads = Json::Number(value, "threads"),
                .runningInstances = Json::Number(value, "runningInstances"),
        };
    }

    ModuleLogLevel ToModuleLogLevel(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .logLevel = Json::Text(value, "logLevel"),
                .channel = Json::Text(value, "channel"),
        };
    }

    ModuleStateResult ToModuleStateResult(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .stopped = Json::Flag(value, "stopped"),
                .runningInstances = Json::Number(value, "runningInstances"),
        };
    }

}// namespace Euclid::CDK::EMM
