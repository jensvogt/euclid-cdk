// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>

/**
 * @file
 * @brief The shapes EMM sends back, and the readers that parse them.
 */

namespace Euclid::CDK::EMM {

    /**
     * @brief One running process of a module, as the manager records it.
     */
    struct EUCLID_CDK_API ModuleInstance {
        std::string instanceId;
        long pid{};

        /**
         * @brief Which machine that pid is on. Empty on a single-host installation, where it says
         * nothing anybody did not already know, and on a record written before the field existed.
         */
        std::string host;
        std::string state;
        std::string socketPath;

        /**
         * @brief The port this instance was given for its own HTTP listener, 0 for a module with
         * none.
         *
         * @par
         * For an application pool this is the answer to "where is this one actually serving", and
         * nothing else exposes it.
         */
        long httpPort{};
        long restartCount{};

        /**
         * @brief How loaded the instance says it is, and how much work is waiting.
         *
         * @par
         * Written by the instance about itself - the manager never writes these and cannot observe
         * them - and they are the whole of what the autoscaler acts on for an application.
         *
         * @par
         * **-1 means the instance has never reported**, which is a different thing from reporting no
         * load: a module that does not report, an SDK too old to know how, or a call being refused.
         * Zero is a real reading.
         */
        long utilisation{};
        long backlog{};

        /**
         * @brief When it last reported, or empty if it never has.
         */
        std::string loadReportedAt;

        /**
         * @brief Work the instance is doing that no request is waiting on - a background purge, say.
         * Scale-down passes over an instance reporting any.
         */
        long backgroundTasks{};

        /**
         * @brief When the running process started, which is what uptime is counted from; empty when
         * nothing is running. Distinct from created, which belongs to the slot and outlives every
         * process in it.
         */
        std::string started;
        std::string created;
        std::string modified;
    };

    /**
     * @brief One module the manager knows about, and the processes it is running for it.
     *
     * @par
     * "module" here is wider than it sounds: euclid's own services are modules, and so is every
     * application pool EAP runs and every transfer server ETS runs. Module::core is what separates
     * them, and it decides what the control actions will accept.
     */
    struct EUCLID_CDK_API Module {
        std::string name;
        std::string executable;
        std::string socketPath;
        bool active{};

        /**
         * @brief Whether this is one of euclid's own services rather than an application pool or a
         * transfer server.
         *
         * @par
         * Worth reading before asking: only a core module can be started and stopped here, because
         * an application's or a transfer server's desired state belongs to EAP and ETS and anything
         * recorded here would be undone within seconds. Reported so a caller can say so rather than
         * learn it from a refusal.
         */
        bool core{};

        /**
         * @brief Whether somebody has asked for this module to stay stopped.
         */
        bool desiredStopped{};
        bool autoRestart{};
        long maxRestarts{};
        long minInstances{};
        long maxInstances{};

        /**
         * @brief Limits asked for but not yet reconciled into the three above; -1 for nothing
         * pending.
         *
         * @par
         * Without these, a caller adjusting a limit twice in quick succession would compute the
         * second change from the value the first one has already replaced.
         */
        long desiredMinInstances{};
        long desiredMaxInstances{};
        long desiredThreads{};

        /**
         * @brief The level this module logs at, or empty when it is under the configured default.
         */
        std::string logLevel;
        std::string created;
        std::string modified;

        /**
         * @brief When the module was last started, or empty if never.
         */
        std::string lastStartTime;
        std::vector<ModuleInstance> instances;
    };

    /**
     * @brief Every module the manager knows about.
     */
    struct EUCLID_CDK_API ModulesResult {
        std::vector<Module> modules;
        long total{};
    };

    /**
     * @brief What a change to a module's instance limits came to.
     *
     * @par
     * The figures are what will stand once the manager reconciles, which is the pair the next change
     * should be computed from.
     */
    struct EUCLID_CDK_API InstanceLimits {
        std::string name;
        long minInstances{};
        long maxInstances{};
    };

    /**
     * @brief What a change to a module's worker-thread count came to.
     */
    struct EUCLID_CDK_API ThreadCount {
        std::string name;
        long threads{};

        /**
         * @brief How many instances the manager still has to cycle through before every one of them
         * runs with the new count - a thread count is fixed when a process starts.
         */
        long runningInstances{};
    };

    /**
     * @brief What a module logs at now, and the channel it logs on.
     *
     * @par
     * An empty logLevel means the level was taken back rather than changed: the module is under
     * whatever the installation's logging configuration says again.
     */
    struct EUCLID_CDK_API ModuleLogLevel {
        std::string name;
        std::string logLevel;
        std::string channel;
    };

    /**
     * @brief What a start, stop or restart was recorded as.
     *
     * @par
     * None of the three waits: the manager acts on its next reconcile, so "runningInstances" is the
     * pool as it was when the request was answered rather than as it will be.
     */
    struct EUCLID_CDK_API ModuleStateResult {
        std::string name;

        /**
         * @brief What the module's desired state now is. Always false from RestartModule(), which
         * changes no desired state.
         */
        bool stopped{};
        long runningInstances{};
    };

    /**
     * @brief Reads one instance of a module.
     */
    [[nodiscard]]
    EUCLID_CDK_API ModuleInstance ToModuleInstance(const boost::json::value &value);

    /**
     * @brief Reads a module.
     */
    [[nodiscard]]
    EUCLID_CDK_API Module ToModule(const boost::json::value &value);

    /**
     * @brief Reads a list-modules response.
     */
    [[nodiscard]]
    EUCLID_CDK_API ModulesResult ToModulesResult(const boost::json::value &value);

    /**
     * @brief Reads a set-instances response.
     */
    [[nodiscard]]
    EUCLID_CDK_API InstanceLimits ToInstanceLimits(const boost::json::value &value);

    /**
     * @brief Reads a set-threads response.
     */
    [[nodiscard]]
    EUCLID_CDK_API ThreadCount ToThreadCount(const boost::json::value &value);

    /**
     * @brief Reads a set-log-level response.
     */
    [[nodiscard]]
    EUCLID_CDK_API ModuleLogLevel ToModuleLogLevel(const boost::json::value &value);

    /**
     * @brief Reads a start-module, stop-module or restart-module response.
     */
    [[nodiscard]]
    EUCLID_CDK_API ModuleStateResult ToModuleStateResult(const boost::json::value &value);

}// namespace Euclid::CDK::EMM
