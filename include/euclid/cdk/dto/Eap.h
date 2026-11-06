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
     * @brief One resource a declaration altered, and what about it changed.
     */
    struct EUCLID_CDK_API InfrastructureChange {
        std::string ern;
        std::vector<std::string> changes;
    };

    /**
     * @brief One resource a reconcile left alone, and why.
     */
    struct EUCLID_CDK_API InfrastructureKept {
        std::string ern;
        std::string reason;
    };

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
         * @brief "JAVA", "JAVA21", "JAVA25", "PYTHON", "NODEJS" or "BINARY" - see EAP::RuntimeJava
         * and its siblings.
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
     * @brief What applying an application's infrastructure declaration came to.
     *
     * @par
     * `declared` false means the application has no declaration stored - not that applying one
     * failed. Nothing was created, deleted or granted, and the four lists are empty; an application
     * that provisions its resources by hand reads this way every time and is not in error.
     *
     * @par
     * `deleted` is the half worth reading before trusting a declaration: a reconcile is full, so a
     * resource this application created and the declaration no longer names is removed, taking a
     * queue's messages or a bucket's objects with it. It is named here rather than counted so that a
     * removal nobody intended is visible in the answer.
     *
     * @par
     * `granted` and `revoked` name roles rather than permissions, and a re-apply that changes
     * nothing still reports every `access-` role in both: they are replaced wholesale rather than
     * diffed, so the same role appears as revoked and granted again.
     */
    struct EUCLID_CDK_API InfrastructureResult {

        /**
         * @brief The application this was about. Empty for a platform, which sets `platform`.
         */
        std::string applicationId;

        /**
         * @brief The platform this was about, for ApplyPlatform() and PlanPlatform(). Empty for an
         * application.
         */
        std::string platform;
        bool declared{};

        /**
         * @brief Whether this was a dry run - true from PlanInfrastructure() and PlanPlatform(),
         * where nothing was changed and the lists are what *would* happen.
         */
        bool dryRun{};
        std::vector<std::string> created;

        /**
         * @brief What already existed and was taken over, rather than created.
         *
         * @par
         * Adoption is how a declaration takes charge of something provisioned by hand; it is not
         * creation, and separating the two is what makes an apply over existing resources readable.
         */
        std::vector<std::string> adopted;

        /**
         * @brief What existed and was altered, with what changed about each.
         */
        std::vector<InfrastructureChange> updated;
        std::vector<std::string> deleted;

        /**
         * @brief What was left alone, and why - a resource the declaration no longer names but
         * which this application does not own is kept rather than deleted, and says so here.
         */
        std::vector<InfrastructureKept> kept;
        std::vector<std::string> granted;
        std::vector<std::string> revoked;
    };

    /**
     * @brief One application a node is holding slots for, as GetNode() reports it.
     *
     * @par
     * "instances" is the slots this node holds and "running" is the ones actually serving out of
     * them. Both, because a node holding four slots and running none is the state worth seeing, and
     * one count cannot say it.
     */
    struct EUCLID_CDK_API NodeApplication {
        std::string applicationId;
        std::string runtimeName;
        std::string nameSpace;
        std::string runtime;
        long instances{};
        long running{};
    };

    /**
     * @brief A host registered to run application instances.
     *
     * @par
     * A node announces itself and renews on a lease; nothing here is configured by hand. What the
     * SDK reads is therefore a snapshot of what a worker last said about itself.
     *
     * @par
     * "live" is whether the lease is still good, and is the field to read rather than comparing
     * lastSeen against a clock - the lease period is the server's to decide. "drained" is the other
     * half of the picture and the two are independent: a drained node is live, keeps what it runs
     * and keeps renewing, and is only refused *new* placements. That is the whole difference between
     * draining a node and stopping one.
     */
    struct EUCLID_CDK_API Node {
        std::string name;
        std::string address;

        /**
         * @brief The user the worker registered as, which is what binds this name to it - another
         * principal cannot re-register under it while the registration stands.
         */
        std::string principal;
        std::map<std::string, std::string> labels;
        long cpuCount{};

        /**
         * @brief The one-minute load average from the node's last heartbeat, as current as lastSeen
         * says it is.
         *
         * @par
         * Zero for a heartbeat that carried no reading, which is also what an idle machine reports -
         * the two are not told apart here, any more than placement tells them apart.
         */
        double loadAverage{};
        std::string version;
        std::string os;
        std::string arch;

        /**
         * @brief Whether new instances are being kept off this node.
         */
        bool drained{};

        /**
         * @brief Whether the node's lease is still good.
         */
        bool live{};
        std::string lastSeen;

        /**
         * @brief What this node is holding slots for - answered by GetNode() alone.
         *
         * @par
         * Empty from ListNodes(), which does not ask: it is the one thing about a node that is not
         * on the node's own record, so it costs a walk of every module pool to work out.
         */
        std::vector<NodeApplication> applications;
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
     * @brief A platform declaration, as it was applied.
     *
     * @par
     * A platform is a declaration with no application behind it: it owns resources and uses none,
     * since it has no principal to grant anything to. What it owns is tagged with its own name.
     */
    struct EUCLID_CDK_API Platform {
        std::string name;
        std::string nameSpace;

        /**
         * @brief The ERNs this platform owns, which is what a later apply reconciles against.
         */
        std::vector<std::string> owns;

        /**
         * @brief The declaration as it was handed over. Null on a record whose stored text will not
         * parse, which is why this is a value rather than an object.
         */
        boost::json::value declaration;
        std::string modified;
    };

    /**
     * @brief How fast an application may call one site.
     */
    struct EUCLID_CDK_API EgressLimits {

        /**
         * @brief Sustained rate, in requests per second.
         */
        double requestsPerSecond{};

        /**
         * @brief How much of that rate may be spent at once, as a multiple of a second's worth - so
         * a burst of 1 is no burst at all.
         */
        double burst{};

        /**
         * @brief How many calls may be in flight together; 0 is no ceiling.
         */
        long concurrent{};
    };

    /**
     * @brief One site an application calls, and the limits in force on it.
     */
    struct EUCLID_CDK_API EgressSite {
        std::string applicationId;

        /**
         * @brief The host name, or "*" for every site this application calls.
         */
        std::string site;
        EgressLimits limits;

        /**
         * @brief Whether those limits were set for this site, or are the installation's default.
         *
         * @par
         * Worth reading before changing one: a site showing the default has no limit of its own,
         * and DeleteEgressLimit() on it would answer 404.
         */
        bool limitsSet{};
        std::string lastUsed;
    };

    /**
     * @brief Every site an application has called, and what applies when nothing was set.
     */
    struct EUCLID_CDK_API EgressSites {
        EgressLimits defaultLimits;
        std::vector<EgressSite> sites;
    };

    /**
     * @brief Permission to make one call to one site - or the refusal, with when to try again.
     */
    struct EUCLID_CDK_API EgressPermit {
        std::string applicationId;
        std::string site;

        /**
         * @brief Whether the call may go ahead. **This is the field to branch on**: a refusal is an
         * ordinary answer rather than an error, because being rate limited is not a fault.
         */
        bool granted{};
        EgressLimits limits;

        /**
         * @brief What ReleaseEgressPermit() takes. Empty on a refusal.
         */
        std::string permitId;

        /**
         * @brief When the permit lapses on its own if it is never released. Empty on a refusal.
         */
        std::string expiresAt;

        /**
         * @brief Why it was refused - which limit was reached. Empty on a grant.
         */
        std::string reason;

        /**
         * @brief How long to wait before asking again, in milliseconds. Zero on a grant.
         */
        long retryAfterMs{};
    };

    /**
     * @brief What releasing a permit came to.
     */
    struct EUCLID_CDK_API EgressRelease {
        std::string applicationId;
        std::string site;

        /**
         * @brief Whether a permit of that id was actually holding a slot. False is not an error:
         * a permit that had already lapsed releases nothing.
         */
        bool released{};
    };

    /**
     * @brief How long a site is being left alone after it asked to be.
     */
    struct EUCLID_CDK_API EgressThrottled {
        std::string applicationId;
        std::string site;

        /**
         * @brief Until when no permit for this site will be granted.
         */
        std::string blockedUntil;
    };

    /**
     * @brief What a rebalance request came to.
     *
     * @par
     * "rebalancing" says the request was recorded, not that anything has moved: where each instance
     * ends up is the manager's to decide over its next passes. "instances" is what there was to
     * move when the request was answered.
     */
    struct EUCLID_CDK_API RebalanceResult {
        std::string applicationId;
        bool rebalancing{};
        long instances{};
    };

    /**
     * @brief Whether a node is being kept clear of new instances.
     */
    struct EUCLID_CDK_API DrainResult {
        std::string node;
        bool drained{};
    };

    /**
     * @brief A node's registration, and how long its lease lasts.
     */
    struct EUCLID_CDK_API NodeRegistration {
        Node node;

        /**
         * @brief How long the lease is good for. A worker renews well inside this, because a lease
         * that lapses is a node the manager stops placing on.
         */
        long leaseSeconds{};
    };

    /**
     * @brief One instance a node has been told to run, with everything needed to run it.
     */
    struct EUCLID_CDK_API AssignedInstance {
        std::string instanceId;
        std::string applicationId;
        std::string runtimeName;

        /**
         * @brief What revision of the application this assignment is for.
         *
         * @par
         * A worker compares it against what it is running: a change means the instance has to be
         * replaced. The fields deliberately left out of it - the log level, the memory and CPU
         * ceilings - are the ones that can change without a restart.
         */
        std::string revision;
        std::string leaseExpiresAt;
        std::string bucketErn;
        std::string artifactKey;
        long artifactSize{};
        std::string md5Sum;
        std::string runtime;
        std::map<std::string, std::string> environment;
        std::string accountId;
        std::string nameSpace;
        std::string logLevel;
        long memoryLimitMb{};
        double cpuLimit{};
    };

    /**
     * @brief What a heartbeat came back with: the lease, and what this node should be running.
     */
    struct EUCLID_CDK_API NodeRenewal {
        std::string node;
        long leaseSeconds{};
        std::string leaseExpiresAt;

        /**
         * @brief Every instance assigned to this node now - the whole set, not what changed. A
         * worker reconciles against it: start what is here and not running, stop what is running
         * and not here.
         */
        std::vector<AssignedInstance> instances;
    };

    /**
     * @brief One slot given to a node.
     */
    struct EUCLID_CDK_API InstanceAssignment {
        std::string runtimeName;
        std::string instanceId;

        /**
         * @brief The node it went to, empty for the manager's own host.
         */
        std::string node;
        std::string leaseExpiresAt;
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
     * @brief Reads an apply-infrastructure response.
     */
    [[nodiscard]]
    EUCLID_CDK_API InfrastructureResult ToInfrastructureResult(const boost::json::value &value);

    /**
     * @brief Reads one application of a node.
     */
    [[nodiscard]]
    EUCLID_CDK_API NodeApplication ToNodeApplication(const boost::json::value &value);

    /**
     * @brief Reads a node.
     */
    [[nodiscard]]
    EUCLID_CDK_API Node ToNode(const boost::json::value &value);

    /**
     * @brief Reads one altered resource.
     */
    [[nodiscard]]
    EUCLID_CDK_API InfrastructureChange ToInfrastructureChange(const boost::json::value &value);

    /**
     * @brief Reads one kept resource.
     */
    [[nodiscard]]
    EUCLID_CDK_API InfrastructureKept ToInfrastructureKept(const boost::json::value &value);

    /**
     * @brief Reads a platform record.
     */
    [[nodiscard]]
    EUCLID_CDK_API Platform ToPlatform(const boost::json::value &value);

    /**
     * @brief Reads a set of egress limits.
     */
    [[nodiscard]]
    EUCLID_CDK_API EgressLimits ToEgressLimits(const boost::json::value &value);

    /**
     * @brief Reads one egress site.
     */
    [[nodiscard]]
    EUCLID_CDK_API EgressSite ToEgressSite(const boost::json::value &value);

    /**
     * @brief Reads a list-egress-sites response.
     */
    [[nodiscard]]
    EUCLID_CDK_API EgressSites ToEgressSites(const boost::json::value &value);

    /**
     * @brief Reads an acquire-egress-permit response.
     */
    [[nodiscard]]
    EUCLID_CDK_API EgressPermit ToEgressPermit(const boost::json::value &value);

    /**
     * @brief Reads a release-egress-permit response.
     */
    [[nodiscard]]
    EUCLID_CDK_API EgressRelease ToEgressRelease(const boost::json::value &value);

    /**
     * @brief Reads a report-egress-throttled response.
     */
    [[nodiscard]]
    EUCLID_CDK_API EgressThrottled ToEgressThrottled(const boost::json::value &value);

    /**
     * @brief Reads a rebalance-application response.
     */
    [[nodiscard]]
    EUCLID_CDK_API RebalanceResult ToRebalanceResult(const boost::json::value &value);

    /**
     * @brief Reads a drain-node response.
     */
    [[nodiscard]]
    EUCLID_CDK_API DrainResult ToDrainResult(const boost::json::value &value);

    /**
     * @brief Reads a register-node response.
     */
    [[nodiscard]]
    EUCLID_CDK_API NodeRegistration ToNodeRegistration(const boost::json::value &value);

    /**
     * @brief Reads one assigned instance.
     */
    [[nodiscard]]
    EUCLID_CDK_API AssignedInstance ToAssignedInstance(const boost::json::value &value);

    /**
     * @brief Reads a renew-node response.
     */
    [[nodiscard]]
    EUCLID_CDK_API NodeRenewal ToNodeRenewal(const boost::json::value &value);

    /**
     * @brief Reads an assign-instance response.
     */
    [[nodiscard]]
    EUCLID_CDK_API InstanceAssignment ToInstanceAssignment(const boost::json::value &value);

    /**
     * @brief Reads a report-load response.
     */
    [[nodiscard]]
    EUCLID_CDK_API LoadReport ToLoadReport(const boost::json::value &value);

}// namespace Euclid::CDK::EAP
