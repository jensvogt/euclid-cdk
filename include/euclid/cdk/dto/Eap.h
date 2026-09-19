// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>

/**
 * @file
 * @brief The shapes EAP sends back, and the readers that parse them.
 *
 * @par
 * The field names on this module are worth reading twice, because a request and the answer to it
 * call some of the same things by different names. An application is deployed from a "bucket" and an
 * "artifact", and comes back describing a "bucketErn" and an "artifactKey"; the "buckets" and
 * "queues" it is granted come back resolved into "resources". Names are what an operator has in
 * hand, ERNs are what euclid stores, and the server resolves the one into the other.
 *
 * @par
 * This is also the second module whose wire spells the namespace "namespace" rather than "nameSpace"
 * - ETS is the other.
 */

namespace Euclid::CDK::EAP {

    /**
     * @brief One running instance of an application, and where it can be reached.
     *
     * @par
     * A pool's instances are started and stopped as it grows and shrinks, and each is given a port
     * of its own when it starts - so this is a snapshot rather than a setting.
     */
    struct EUCLID_CDK_API Endpoint {
        std::string instanceId;
        long pid{};
        long httpPort{};
    };

    /**
     * @brief A deployed application: what euclid runs, as what, and how much of it.
     *
     * @par
     * "desiredState" is what somebody asked for and "state" is what is actually running, which reads
     * RUNNING exactly when at least one instance answers. The two differing is the ordinary picture
     * of an application starting up, and the lasting picture of one that cannot.
     *
     * @par
     * "userId" is the identity the application runs as. Unless one was named at deployment it is a
     * technical principal euclid made for it - no password, no login, one access key - so that
     * nothing an application leaks is a person's credential.
     */
    struct EUCLID_CDK_API Application {
        std::string applicationId;

        /**
         * @brief What everything belonging to this application on a host is actually called: its
         * directory, its row in the module list, its socket, its log channel, and the technical
         * principal named after it ("app-<runtimeName>").
         *
         * @par
         * Distinct from applicationId because an ID is only unique within an account and namespace,
         * while these names are installation-wide - so the server issues a short unique name at
         * deployment and keeps it across a move between namespaces.
         */
        std::string runtimeName;
        std::string ern;
        std::string accountId;

        /**
         * @brief The other half of what identifies this application: an applicationId is unique
         * within an account and a namespace. Empty for one at the account root, and the only way to
         * see where an application ended up after a move.
         */
        std::string nameSpace;
        std::string region;

        /**
         * @brief "JAVA", "PYTHON", "NODEJS" or "BINARY" - see EAP::RuntimeJava and its siblings.
         */
        std::string runtime;

        /**
         * @brief The bucket the artifact was deployed from, as an ERN. Deployed by name.
         */
        std::string bucketErn;

        /**
         * @brief The object key of the artifact within that bucket.
         */
        std::string artifactKey;
        std::string version;

        /**
         * @brief ESM's checksum of the artifact - the same hash the manager compares the copy on the
         * host against, and the one a redeploy has to differ from.
         */
        std::string md5Sum;
        std::string command;
        std::vector<std::string> arguments;
        std::map<std::string, std::string> environment;

        /**
         * @brief The ERNs of the buckets and queues this application was granted, resolved from the
         * names it was deployed with.
         */
        std::vector<std::string> resources;
        std::string userId;

        /**
         * @brief The level this application logs at, or empty when it is under the configured
         * default.
         */
        std::string logLevel;
        long minInstances{};
        long maxInstances{};
        long readyTimeoutMs{};

        /**
         * @brief "RUNNING" or "STOPPED", as asked for.
         */
        std::string desiredState;

        /**
         * @brief "RUNNING" or "STOPPED", as observed - which is not the same as having been started.
         */
        std::string state;

        /**
         * @brief How many instances are running - the length of endpoints.
         */
        long instances{};
        std::vector<Endpoint> endpoints;
        std::string created;
        std::string modified;

        /**
         * @brief Whether an instance is actually answering, which is not the same as having been
         * started.
         */
        [[nodiscard]]
        bool IsRunning() const;
    };

    /**
     * @brief What an application logs at now, and the channel it logs on.
     *
     * @par
     * An empty logLevel means the level was taken back rather than changed: the application is under
     * whatever the installation's logging configuration says again.
     */
    struct EUCLID_CDK_API LogLevelResult {
        std::string applicationId;
        std::string logLevel;
        std::string channel;
    };

    /**
     * @brief What a restart request came to.
     *
     * @par
     * "restarting" says the request was recorded, not that anything has happened: the manager stops
     * and starts the instances on its next reconcile. "instances" is what was running when the
     * request was answered, so it is the size of the pool about to be cycled rather than the one
     * that came back.
     */
    struct EUCLID_CDK_API RestartResult {
        std::string applicationId;
        bool restarting{};
        long instances{};
    };

    /**
     * @brief A load report as the server recorded it.
     *
     * @par
     * The figures come back as they were stored rather than as they were sent: utilisation is held
     * to 0..100 and the two counts to zero or more, so a report that overshot says here what was
     * actually written.
     */
    struct EUCLID_CDK_API LoadReport {
        std::string instanceId;
        double utilisation{};
        long backlog{};
        long active{};
    };

    /**
     * @brief Reads one instance of an application.
     */
    [[nodiscard]]
    EUCLID_CDK_API Endpoint ToEndpoint(const boost::json::value &value);

    /**
     * @brief Reads an application.
     */
    [[nodiscard]]
    EUCLID_CDK_API Application ToApplication(const boost::json::value &value);

    /**
     * @brief Reads a set-log-level response.
     */
    [[nodiscard]]
    EUCLID_CDK_API LogLevelResult ToLogLevelResult(const boost::json::value &value);

    /**
     * @brief Reads a restart-application response.
     */
    [[nodiscard]]
    EUCLID_CDK_API RestartResult ToRestartResult(const boost::json::value &value);

    /**
     * @brief Reads a report-load response.
     */
    [[nodiscard]]
    EUCLID_CDK_API LoadReport ToLoadReport(const boost::json::value &value);

}// namespace Euclid::CDK::EAP
