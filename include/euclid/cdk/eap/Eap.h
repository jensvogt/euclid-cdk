// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Eap.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EAP {


    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "eap";

    /**
     * @brief What an artifact is handed to.
     *
     * @par
     * Matched exactly, in upper case, and anything else is refused with HTTP 400 - "JAVA21" is a
     * runtime, "java21" and "JAVA 21" are typos.
     *
     * @par
     * RuntimeJava is whichever java the host calls java, which is what every application deployed
     * before the versioned ones said. RuntimeJava21 and RuntimeJava25 name a version and are
     * started with the executable that host has configured for it - a jar built for 25 does not
     * start on 21, and leaving it to whichever java resolved first made the version an accident of
     * the manager's PATH.
     */
    inline constexpr std::string_view RuntimeJava = "JAVA";
    inline constexpr std::string_view RuntimeJava21 = "JAVA21";
    inline constexpr std::string_view RuntimeJava25 = "JAVA25";
    inline constexpr std::string_view RuntimePython = "PYTHON";
    inline constexpr std::string_view RuntimeNodeJs = "NODEJS";

    /**
     * @brief Anything already executable, which is where a C++ application built with this SDK
     * lands.
     */
    inline constexpr std::string_view RuntimeBinary = "BINARY";

    /**
     * @brief The levels SetLogLevel() accepts.
     *
     * @par
     * An unrecognised one is refused rather than defaulted: "warnign" quietly meaning "info" is an
     * application logging more than somebody asked for, and quietly meaning "off" is silence nobody
     * asked for at all.
     */
    inline constexpr std::string_view LogTrace = "trace";
    inline constexpr std::string_view LogDebug = "debug";
    inline constexpr std::string_view LogInfo = "info";
    inline constexpr std::string_view LogWarning = "warning";
    inline constexpr std::string_view LogError = "error";
    inline constexpr std::string_view LogFatal = "fatal";
    inline constexpr std::string_view LogOff = "off";

    /**
     * @brief What an application's desiredState and state read as.
     */
    inline constexpr std::string_view StateRunning = "RUNNING";
    inline constexpr std::string_view StateStopped = "STOPPED";

    /**
     * @brief What a pool is sized at unless the deployment says otherwise.
     */
    inline constexpr long DefaultMinInstances = 1;
    inline constexpr long DefaultMaxInstances = 1;

    /**
     * @brief How long an instance has to become ready before the manager gives up on it, in
     * milliseconds.
     */
    inline constexpr long DefaultReadyTimeoutMs = 30000;

    /**
     * @brief The environment variable the manager hands every instance its own id in.
     */
    inline constexpr std::string_view InstanceIdVariable = "EUCLID_INSTANCE_ID";

    /**
     * @brief How long an egress permit holds its slot unless it says otherwise.
     */
    inline constexpr std::chrono::seconds DefaultEgressLease{900};

    /**
     * @brief The longest lease the server will grant; anything above is clamped to it.
     */
    inline constexpr std::chrono::seconds MaxEgressLease{6 * 3600};

    /**
     * @brief The platform a declaration is applied under when none is named.
     */
    inline constexpr std::string_view DefaultPlatform = "platform";

    /**
     * @brief What an application's type reads as.
     *
     * @par
     * TypeJob matters to more than scheduling: a job's instance is a *run* of it, which is why
     * RebalanceApplication() refuses one - moving a run would start it again.
     */
    inline constexpr std::string_view TypeProcess = "PROCESS";
    inline constexpr std::string_view TypeJob = "JOB";

    /**
     * @brief The ceilings one instance of an application runs under; -1 leaves one as it stands.
     *
     * @par
     * Zero removes a ceiling, which is a different thing from -1 leaving it alone. At least one of
     * the two has to be given.
     */
    struct EUCLID_CDK_API ApplicationLimitOptions {

        /**
         * @brief Memory, in MiB. 0 removes the ceiling.
         */
        long memoryLimitMb{-1};

        /**
         * @brief CPU, in cores - 1.5 is a core and a half. 0 removes the ceiling.
         */
        double cpuLimit{-1.0};
    };

    /**
     * @brief What a worker says about itself when it registers.
     */
    struct EUCLID_CDK_API RegisterNodeOptions {

        /**
         * @brief Where the manager can reach it.
         */
        std::string address;
        std::string version;
        std::string os;
        std::string arch;

        /**
         * @brief Labels placement can select on.
         */
        std::map<std::string, std::string> labels;
    };

    /**
     * @brief What a heartbeat carries about the machine.
     *
     * @par
     * All of it optional, and all of it travelling on the heartbeat rather than in calls of its own:
     * one request per node per tick is the point. Absent readings are recorded as idle, which is
     * optimistic and the right direction - they only ever break a tie between nodes already running
     * the same number of instances.
     */
    struct EUCLID_CDK_API RenewNodeOptions {

        /**
         * @brief The one-minute load average.
         */
        double loadAverage{};

        /**
         * @brief Installed memory, in MiB; 0 says nothing.
         */
        long memoryTotalMb{};

        /**
         * @brief Memory in use, in MiB; 0 says nothing.
         */
        long memoryUsedMb{};
    };

    /**
     * @brief What a worker reports about one instance it is running.
     */
    struct EUCLID_CDK_API ReportInstanceOptions {

        /**
         * @brief The state it is in; empty means RUNNING.
         */
        std::string state;

        /**
         * @brief When the process started, ISO 8601.
         */
        std::string startedAt;

        /**
         * @brief Which machine it is on.
         */
        std::string host;

        /**
         * @brief The port it was given for its own HTTP listener.
         */
        long httpPort{};
        long pid{};
    };

    /**
     * @brief How long a permit is wanted for.
     */
    struct EUCLID_CDK_API EgressPermitOptions {

        /**
         * @brief How long the permit holds its slot if it is never released, in seconds. Clamped by
         * the server to at most MaxEgressLease.
         *
         * @par
         * This is a safety net rather than a plan: a permit that is released when the call finishes
         * never reaches it. The lease is what stops a crashed instance holding a concurrency slot
         * for ever.
         */
        std::chrono::seconds leaseSeconds{DefaultEgressLease};
    };

    /**
     * @brief Everything a deployment says beyond what it runs and where the artifact is.
     */
    struct EUCLID_CDK_API CreateApplicationOptions {

        /**
         * @brief What to record as the deployed version. Left empty, the server reads it out of the
         * artifact's name and refuses the deployment if it cannot.
         */
        std::string version;

        /**
         * @brief What to run, when the runtime's own interpreter is not it. Empty means the runtime
         * decides, resolved through PATH.
         */
        std::string command;

        /**
         * @brief What follows the command.
         */
        std::vector<std::string> arguments;

        /**
         * @brief The environment the process is given.
         */
        std::map<std::string, std::string> environment;

        /**
         * @brief The buckets this application may reach, by name; euclid resolves them and grants
         * them to the identity it runs as.
         */
        std::vector<std::string> buckets;

        /**
         * @brief Likewise for queues.
         */
        std::vector<std::string> queues;

        /**
         * @brief An existing user to run as. Left empty, euclid creates a technical principal for
         * the application - which is the better answer, and why this is not required.
         */
        std::string user;

        /**
         * @brief The smallest the pool goes; at least one.
         */
        long minInstances{DefaultMinInstances};

        /**
         * @brief The largest it goes; never below minInstances.
         */
        long maxInstances{DefaultMaxInstances};

        /**
         * @brief How long an instance has to become ready; at least 1000.
         */
        long readyTimeoutMs{DefaultReadyTimeoutMs};
    };

    /**
     * @brief The instance bounds ScaleApplication() sets.
     *
     * @par
     * -1 leaves a bound as it stands, which is what lets a ceiling be raised without touching
     * the floor. The same number for both pins the pool at that size and leaves the autoscaler
     * nothing to decide.
     */
    struct EUCLID_CDK_API ScaleApplicationOptions {
        long minInstances = -1;
        long maxInstances = -1;
    };

    /**
     * @brief What an update changes - and only what it names.
     *
     * @par
     * The distinction the server draws is between a field being sent and not being sent rather than
     * between its values: an unset command leaves the stored one alone, while one set to an empty
     * string clears it and hands the artifact back to the runtime's own interpreter. That is what
     * std::optional says here.
     *
     * @par
     * "buckets" and "queues" are re-resolved together whenever either is named, so naming one and
     * not the other revokes what the other used to grant. Name both, or neither. They are resolved
     * in the namespace the application is in *after* this update, which is what makes moving one and
     * re-granting its resources a single call.
     *
     * @par
     * "nameSpace" is a move rather than a field change, and the one way an application deployed
     * before applications carried a namespace can acquire one without being deleted and made again.
     */
    struct EUCLID_CDK_API UpdateApplicationOptions {
        std::optional<std::string> runtime;
        std::optional<std::string> artifact;
        std::optional<std::string> version;
        std::optional<std::string> command;
        std::optional<std::vector<std::string>> arguments;
        std::optional<std::map<std::string, std::string>> environment;
        std::optional<std::vector<std::string>> buckets;
        std::optional<std::vector<std::string>> queues;
        std::optional<long> minInstances;
        std::optional<long> maxInstances;
        std::optional<long> readyTimeoutMs;
        std::optional<std::string> nameSpace;
    };

    /**
     * @brief What an instance says about how busy it is.
     */
    struct EUCLID_CDK_API LoadReportOptions {

        /**
         * @brief How much of this instance is in use, from 0 to 100. The server holds it to that
         * range rather than refusing what is outside it.
         */
        double utilisation{0};

        /**
         * @brief Work waiting to be started.
         */
        long backlog{0};

        /**
         * @brief Work started and not finished.
         *
         * @par
         * Absent from an older SDK's report and read as zero, which is what makes a client that says
         * nothing about it scale down as it did before the field existed.
         */
        long active{0};

        /**
         * @brief Which instance is reporting. Empty reads EUCLID_INSTANCE_ID, which is where the
         * manager puts it.
         */
        std::string instanceId;
    };

    /**
     * @brief EAP - euclid's application platform: what euclid runs, from what, and as whom.
     *
     * @par
     * @code
     * const EAP::Eap eap(session);
     *
     * eap.CreateApplication("order-service", std::string(EAP::RuntimeBinary), "artifacts", "order-service-1.4.0",
     *                       {.queues = {"orders"}});
     * eap.StartApplication("order-service");
     * @endcode
     *
     * @par
     * An application is deployed from an artifact already in a bucket - ESM puts it there, and EAP
     * names it. The deployment says which buckets and queues it may reach, and euclid grants those
     * to the identity it runs as: a technical principal it creates for the application unless one is
     * named, with no password, no login and one access key. Nothing an application leaks is then a
     * person's credential.
     *
     * @par
     * Two names for the same things, and the asymmetry is the server's: a deployment names a bucket
     * and an artifact, and the application that comes back describes a bucketErn and an artifactKey.
     * Likewise the buckets and queues it is granted come back resolved into resources.
     *
     * @par
     * Every action here is administrator-only, server-side - except ReportLoad(), which an
     * application calls for itself.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Eap final : public ModuleClient {
    public:

        /**
         * @brief Builds EAP's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Eap(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Eap(EAM::Session &&) = delete;

        // -- deploying --------------------------------------------------------------------------

        /**
         * @brief Deploys an application, stopped, and answers with it as it was stored.
         *
         * @par
         * Nothing runs yet: a new application's desired state is StateStopped, so StartApplication()
         * is what puts it in service. Refused with HTTP 409 if this account and namespace already
         * have an application of that ID, and with 404 if the bucket, the artifact, a named resource
         * or a named user is not there - a deployment pointing at nothing would otherwise become an
         * application that fails to start for a reason nobody can see.
         *
         * @param applicationId the application's ID, unique within the account and namespace.
         * @param runtime       RuntimeJava, RuntimePython, RuntimeNodeJs or RuntimeBinary.
         * @param bucket        the name of the bucket holding the artifact - a name, not an ERN.
         * @param artifact      the artifact's object key within that bucket.
         * @param options       the version, what to run, what it may reach, and how many of it.
         */
        [[nodiscard]]
        Application CreateApplication(const std::string &applicationId, const std::string &runtime,
                                      const std::string &bucket, const std::string &artifact,
                                      const CreateApplicationOptions &options = {}) const;

        /**
         * @brief Changes a deployed application. Only what options names changes.
         *
         * @par
         * Changing the artifact is a change of what will run next; RedeployApplication() is what a
         * new build of the same application usually wants.
         *
         * @par
         * Changing the namespace moves the application: its ERN is rebuilt from the namespace it
         * lands in, its row moves, and the grant of the technical principal it runs as follows it -
         * while its runtimeName stays as it was, so the directory, socket and log channel on the
         * host do not move underneath a running instance. An empty string moves it back to the
         * account root, which is why unset and empty mean different things here.
         */
        [[nodiscard]]
        Application UpdateApplication(const std::string &applicationId, const UpdateApplicationOptions &options = {}) const;

        /**
         * @brief Defines the same application again in another namespace, leaving the original.
         *
         * @par
         * The sibling of moving one with UpdateApplicationOptions::nameSpace, and the difference is
         * the point: a move takes the definition with it, so what ran in the old namespace stops
         * running there. A copy is how a build is promoted - development to integration,
         * integration to production - while the namespace it came from goes on serving.
         *
         * @par
         * The copy runs the same artifact, down to the checksum, so it is the same bytes rather
         * than a rebuild that happens to share a version. It is given its own runtime name and its
         * own technical principal with its own access key, both being installation-wide and
         * unshareable, so revoking the copy's credentials leaves the original running. An
         * application told to run as a named user keeps that user.
         *
         * @par
         * What it may reach is re-resolved rather than copied: a bucket or queue ERN carries the
         * namespace it was resolved in, so copying the list would point the new application at the
         * old namespace's data. The same names are looked up in the target namespace, and one with
         * no counterpart there fails the copy rather than quietly leaving the application with less
         * access than the original.
         *
         * @par
         * The copy is created stopped, whatever the original is doing.
         *
         * @param applicationId the application to copy, in the namespace this session works in
         * @param targetNameSpace the namespace to copy it into; it has to exist already
         * @param targetApplicationId the name the copy is defined under; the original's when empty,
         * which is how an application is copied beside itself within one namespace
         * @return the stored definition of the copy
         */
        [[nodiscard]]
        Application CopyApplication(const std::string &applicationId, const std::string &targetNameSpace,
                                    const std::string &targetApplicationId = "") const;


        /**
         * @brief Changes how many instances an application runs, without restarting the ones it has.
         *
         * @par
         * UpdateApplication() can set the same two fields, but it writes the whole definition and
         * stamps the modification date - and the manager restarts a pool whose application changed
         * since it started it. Scaling that way stops every running instance and starts it again,
         * which is the opposite of what asking for capacity means and worst at the moment it is
         * asked for.
         *
         * @par
         * What is set is the range the autoscaler works within, not a count: the manager scales
         * toward it on its next reconcile. Nothing is started or stopped by this call.
         *
         * @par
         * The bounds are checked against each other as they *will* stand rather than as they are,
         * so raising only the floor is refused when it would pass the stored ceiling. A floor of
         * zero is refused for its own reason - an application desired RUNNING with no instances
         * reads everywhere as a pool that failed to start, and StopApplication() is how one is
         * taken out of service.
         *
         * @param applicationId the application to scale
         * @param options the bounds to set; -1 leaves one as it stands
         * @return the stored definition after the change
         */
        [[nodiscard]]
        Application ScaleApplication(const std::string &applicationId, const ScaleApplicationOptions &options = {}) const;

        /**
         * @brief Points an application at a new build of itself.
         *
         * @par
         * The artifact defaults to the one already deployed - which is what a rebuilt artifact
         * stored under the same key wants - and the version to whatever the artifact's name says. A
         * redeploy that would change neither the version nor the checksum is refused with HTTP 409:
         * it would restart the instances for nothing, and usually means the new artifact never
         * reached the bucket.
         */
        [[nodiscard]]
        Application RedeployApplication(const std::string &applicationId, const std::string &artifact = {}, const std::string &version = {}) const;

        /**
         * @brief Removes an application. Stop it first - this does not.
         */
        void DeleteApplication(const std::string &applicationId) const;

        // -- running ----------------------------------------------------------------------------

        /**
         * @brief Asks for an application to run, and answers with it as it stands.
         *
         * @par
         * Asking is all this does: the desired state changes here and the manager acts on it, so the
         * application in the answer is usually still StateStopped - it says what was asked for, not
         * what has happened yet.
         */
        [[nodiscard]]
        Application StartApplication(const std::string &applicationId) const;

        /**
         * @brief Asks for an application to stop, and answers with it as it stands.
         */
        [[nodiscard]]
        Application StopApplication(const std::string &applicationId) const;

        /**
         * @brief Asks for a running application's instances to be started again.
         *
         * @par
         * The manager stops the whole pool on its next reconcile and starts it straight back up from
         * the current definition - the same thing it does after a redeploy, with nothing new to pick
         * up. The artifact, the environment and the credentials all come back as they were, so this
         * is for an instance that has to do its startup again rather than a way to deploy anything.
         *
         * @par
         * Deliberately not a stop followed by a start: between those two the desired state is
         * stopped, so a caller that fails in between leaves the application down. Here it stays
         * running throughout, and one that is already stopped is refused with HTTP 400 rather than
         * started.
         */
        [[nodiscard]]
        RestartResult RestartApplication(const std::string &applicationId) const;

        /**
         * @brief The applications whose ID starts with a prefix; an empty prefix lists them all.
         *
         * @par
         * The session's own account and namespace, not the installation: every other action here
         * resolves an application ID in the namespace the request was made in, so a listing that
         * crossed namespaces would show applications the caller cannot then address.
         * EAM::Session::ChangeNamespace() is how to look elsewhere.
         *
         * @par
         * A plain list rather than a page: EAP answers with every match at once, since an
         * installation has tens of applications rather than thousands.
         */
        [[nodiscard]]
        std::vector<Application> ListApplications(const std::string &prefix = {}) const;

        /**
         * @brief One application, by its ID, with the instances that are answering for it.
         */
        [[nodiscard]]
        Application GetApplication(const std::string &applicationId) const;

        /**
         * @brief Sets the memory and CPU ceilings one instance runs under.
         *
         * @par
         * Applied as instances are replaced rather than at once - a ceiling is the cgroup a process
         * is started in. Deliberately not part of an application's revision, so setting one
         * restarts nothing by itself.
         *
         * @param applicationId the application.
         * @param options       the ceilings; -1 leaves one as it stands, 0 removes it.
         * @throws EuclidError if neither ceiling is given. ServiceError 400 if a value is out of
         * range, 404 if there is no such application.
         */
        [[nodiscard]]
        Application SetApplicationLimits(const std::string &applicationId, const ApplicationLimitOptions &options) const;

        /**
         * @brief Asks for an application's instances to be spread again over the nodes.
         *
         * @par
         * For after a node joins or is drained, when the placement that was right when the pool
         * started no longer is. Nothing moves when this is answered: the manager replaces instances
         * over its next passes, so the pool keeps serving throughout.
         *
         * @throws ServiceError 400 if the application is a job - a job's instance is a *run* of it,
         * and moving one would start it again. Also 404 if there is no such application.
         */
        [[nodiscard]]
        RebalanceResult RebalanceApplication(const std::string &applicationId) const;

        /**
         * @brief What ApplyInfrastructure() would do, without doing it.
         *
         * @par
         * The same engine and the same answer, with InfrastructureResult::dryRun true and nothing
         * changed. Worth running before a release depends on a declaration, because the lists it
         * returns include what a full reconcile would **delete**.
         */
        [[nodiscard]]
        InfrastructureResult PlanInfrastructure(const std::string &applicationId) const;

        // -- platforms ----------------------------------------------------------------------------

        /**
         * @brief Applies a declaration that belongs to no application.
         *
         * @par
         * A platform is the shared half of an installation - the queues, topics and buckets that
         * exist for everything rather than for one application. Applied by the same engine as an
         * application's declaration, with three differences: it is handed over here rather than read
         * from beside an artifact, since a platform has no artifact; it owns and never uses, having
         * no principal to grant to; and what it owns is tagged with its own name.
         *
         * @param name        what the platform is managed under; DefaultPlatform when it is the
         * only one.
         * @param declaration the declaration itself.
         * @param prune       whether to delete what this platform owns and the declaration no
         * longer names. False leaves such resources alone and reports them under
         * InfrastructureResult::kept.
         * @throws ServiceError 400 if the name or the declaration will not do.
         */
        [[nodiscard]]
        InfrastructureResult ApplyPlatform(const std::string &name, const boost::json::value &declaration,
                                           bool prune = false) const;

        /**
         * @brief What ApplyPlatform() would do, without doing it.
         */
        [[nodiscard]]
        InfrastructureResult PlanPlatform(const std::string &name, const boost::json::value &declaration,
                                          bool prune = false) const;

        /**
         * @brief One platform record: what it owns, and the declaration it was applied from.
         *
         * @param name the platform; DefaultPlatform when none was named at apply time.
         * @throws ServiceError 404 if there is no such platform.
         */
        [[nodiscard]]
        Platform GetPlatform(const std::string &name = std::string(DefaultPlatform)) const;

        /**
         * @brief Every platform of this account and namespace.
         */
        [[nodiscard]]
        std::vector<Platform> ListPlatforms() const;

        // -- egress ------------------------------------------------------------------------------

        /**
         * @brief Asks permission to call an outside site, and waits for nobody.
         *
         * @par
         * An application calls this about itself before reaching out to something that rate limits
         * it. The point is that several instances of one application share one budget: a limit kept
         * in each process would be multiplied by the pool size, which is how a fleet gets itself
         * blocked.
         *
         * @par
         * **A refusal is an ordinary answer, not an error** - EgressPermit::granted is the field to
         * branch on, and EgressPermit::retryAfterMs is how long to wait. Being rate limited is not
         * a fault.
         *
         * @param site    the host name about to be called.
         * @param options how long the permit should hold its slot.
         */
        [[nodiscard]]
        EgressPermit AcquireEgressPermit(const std::string &site, const EgressPermitOptions &options = {}) const;

        /**
         * @brief Gives a permit's slot back, as soon as the call it covered is done.
         *
         * @par
         * The other half of AcquireEgressPermit(), and worth doing promptly: a slot held until its
         * lease lapses is a slot no other instance can use meanwhile.
         */
        [[nodiscard]]
        EgressRelease ReleaseEgressPermit(const std::string &site, const std::string &permitId) const;

        /**
         * @brief Says a site asked to be left alone, so that nothing else tries it meanwhile.
         *
         * @par
         * For when a site answers 429 or 503 with a Retry-After: telling euclid turns one instance's
         * refusal into the whole pool backing off, which is the difference between recovering and
         * every instance discovering the same thing in turn.
         *
         * @param site              the host that refused.
         * @param retryAfterSeconds how long it asked for; at least one second.
         */
        [[nodiscard]]
        EgressThrottled ReportEgressThrottled(const std::string &site, long retryAfterSeconds = 60) const;

        /**
         * @brief Sets how fast an application may call one site.
         *
         * @param site          the host name, or "*" for every site this application calls.
         * @param limits        the limits; at least one of the three has to say something.
         * @param applicationId whose limits, or empty for the calling application's own.
         * @throws ServiceError 400 if no limit is given, 503 if the egress state is momentarily
         * busy - which is worth retrying rather than reporting.
         */
        [[nodiscard]]
        EgressSite SetEgressLimit(const std::string &site, const EgressLimits &limits,
                                  const std::string &applicationId = {}) const;

        /**
         * @brief Removes a site's own limits, putting it back under the installation's default.
         *
         * @throws ServiceError 404 if that site had no limit of its own - which
         * EgressSite::limitsSet is how to tell beforehand.
         */
        [[nodiscard]]
        EgressSite DeleteEgressLimit(const std::string &site, const std::string &applicationId = {}) const;

        /**
         * @brief Every site an application has called, and what applies to each.
         *
         * @param applicationId whose sites, or empty for the calling application's own.
         */
        [[nodiscard]]
        EgressSites ListEgressSites(const std::string &applicationId = {}) const;

        /**
         * @brief Forgets a site entirely - its limits and the record that it was ever called.
         *
         * @par
         * Unlike DeleteEgressLimit(), which leaves the site listed under the default limits.
         */
        void DeleteEgressSite(const std::string &site, const std::string &applicationId = {}) const;

        // -- nodes --------------------------------------------------------------------------------

        /**
         * @brief The hosts registered to run instances for this account.
         *
         * @par
         * Nodes are not configured here: a worker announces itself and renews on a lease, so this is
         * a snapshot of what each last said about itself. An installation running everything on the
         * manager's own host has none, and that is not a fault.
         *
         * @par
         * Node::applications is empty from this call - see GetNode() for why.
         */
        [[nodiscard]]
        std::vector<Node> ListNodes() const;

        /**
         * @brief One node, and what it is holding slots for.
         *
         * @par
         * The only call that fills Node::applications, because what a node is running is the one
         * thing about it that is not on the node's own record - working it out means walking every
         * module pool, which is a cost a listing should not pay per node.
         *
         * @param node the node's name.
         * @throws ServiceError 404 if no node of that name is registered.
         */
        [[nodiscard]]
        Node GetNode(const std::string &node) const;

        /**
         * @brief Removes a node's registration, freeing the name for another principal.
         *
         * @par
         * Administrator only, unlike the other node calls, and for a specific reason: a worker able
         * to delete another's registration could re-register under its name and be handed its
         * assignments and credentials. Binding the name to the principal that registered it is what
         * closes that, and this is the one way the binding is released.
         *
         * @par
         * **This does not stop a node.** The leases are left alone, so a worker that is still running
         * finds itself unregistered on its next renewal and simply registers again. Draining the
         * node and then stopping the worker is how one is taken out of service.
         *
         * @param node the node's name.
         * @throws ServiceError 403 without administrator rights, 404 if no node of that name is
         * registered.
         */
        void DeleteNode(const std::string &node) const;

        /**
         * @brief Announces a node, or announces it again.
         *
         * @par
         * The worker's side of the lease protocol, and the first call it makes. **Idempotent, and
         * that matters**: a restarted worker has to come back as the node it was, or it abandons
         * the instances it is still running.
         *
         * @par
         * The name is bound to the principal that registered it, so another principal cannot take
         * it over while the registration stands - DeleteNode() is the only way to release it.
         *
         * @param node    the name this worker is known by.
         * @param options what it says about itself.
         * @throws ServiceError 409 if the name is registered to another principal.
         */
        [[nodiscard]]
        NodeRegistration RegisterNode(const std::string &node, const RegisterNodeOptions &options = {}) const;

        /**
         * @brief The heartbeat: renews the lease and collects what this node should be running.
         *
         * @par
         * Called on a timer, well inside NodeRegistration::leaseSeconds - a lease that lapses is a
         * node the manager stops placing on. One call does both jobs deliberately: a separate action
         * for the machine's readings would be a second request per node per tick.
         *
         * @par
         * NodeRenewal::instances is the **whole** set assigned to this node, not what changed, so a
         * worker reconciles against it: start what is listed and not running, stop what is running
         * and not listed. AssignedInstance::revision is what says an instance has to be replaced
         * rather than left alone.
         *
         * @throws ServiceError 409 if the node is registered to another principal.
         */
        [[nodiscard]]
        NodeRenewal RenewNode(const std::string &node, const RenewNodeOptions &options = {}) const;

        /**
         * @brief Stops new instances being placed on a node, or puts it back into service.
         *
         * @par
         * **Not a stop.** A drained node keeps what it is running and keeps renewing, and is only
         * refused *new* placements - so its instances leave as they are replaced rather than all at
         * once. That is the whole difference between draining a node and stopping one, and it is why
         * draining is not an outage.
         *
         * @param node    the node.
         * @param drained false puts it back into service.
         * @throws ServiceError 404 if no node of that name is registered.
         */
        [[nodiscard]]
        DrainResult DrainNode(const std::string &node, bool drained = true) const;

        /**
         * @brief Gives one instance slot to a node.
         *
         * @par
         * The operator's half of placement, for an installation with no automatic placement yet: the
         * manager is told by hand which node a slot belongs to.
         *
         * @param runtimeName the pool the slot is in.
         * @param instanceId  the slot.
         * @param node        the node to give it to, or empty to take it back to the manager's own
         * host - which is a legitimate thing to ask for, so empty and absent differ here.
         * @throws ServiceError 404 if the node is not registered or there is no such instance, 409
         * if the node is drained - a drained node is refused new assignments and keeps what it has.
         */
        [[nodiscard]]
        InstanceAssignment AssignInstance(const std::string &runtimeName, const std::string &instanceId,
                                          const std::string &node = {}) const;

        /**
         * @brief Reports what a worker has actually done with one slot.
         *
         * @par
         * How the manager learns an instance is running, and on which host - the counterpart to
         * RenewNode() telling the worker what it should be running.
         *
         * @throws ServiceError 409 if the slot belongs to another node.
         */
        void ReportNodeInstance(const std::string &runtimeName, const std::string &instanceId,
                                const ReportInstanceOptions &options = {}) const;

        /**
         * @brief Takes the credentials one instance runs as.
         *
         * @par
         * What a worker fetches before starting an instance, so that the application comes up as its
         * own technical principal. Answered unparsed: it is a credentials bundle whose shape belongs
         * to whatever consumes it, and writing it into a typed struct here would mean this SDK
         * deciding what a credential is.
         *
         * @par
         * Only the node holding that slot may ask, which is what makes the registration's binding of
         * name to principal worth having.
         *
         * @throws ServiceError 403 if this caller does not hold the slot, 404 if there is no such
         * pool or no application behind it, 409 if the slot belongs to another node.
         */
        [[nodiscard]]
        boost::json::object IssueInstanceCredentials(const std::string &runtimeName, const std::string &instanceId) const;

        // -- infrastructure ---------------------------------------------------------------------

        /**
         * @brief Makes the installation match the application's own infrastructure declaration.
         *
         * @par
         * The declaration is a file the application has already stored beside its artifact -
         * `<applicationId>.euclid.json` in the bucket it deploys from - which names the queues,
         * topics and buckets it owns and the ones belonging to others that it reaches. This applies
         * it: creates what is missing, grants the access it asks for, and **deletes what this
         * application created and the declaration no longer names**.
         *
         * @par
         * Applying is idempotent and changes nothing about the running instances - the modification
         * date is deliberately not stamped, so the manager does not read it as a new revision and
         * cycle the pool. It happens on its own whenever the application is created, updated or
         * redeployed; this is for reconciling without a deploy.
         *
         * @par
         * An application with no declaration stored is answered rather than refused -
         * InfrastructureResult::declared is false. A declaration that cannot be applied is an error:
         * one that names a resource belonging to another application, or claims one it does not own,
         * throws rather than being partly applied.
         *
         * @param applicationId the application, in the session's namespace.
         */
        [[nodiscard]]
        InfrastructureResult ApplyInfrastructure(const std::string &applicationId) const;

        // -- logging ----------------------------------------------------------------------------

        /**
         * @brief Sets what one application logs at, without restarting or redeploying it.
         *
         * @param applicationId the application.
         * @param level         LogTrace, LogDebug, LogInfo, LogWarning, LogError, LogFatal or
         * LogOff. An empty one takes the setting back - see ResetLogLevel(), which says that in a
         * word.
         */
        [[nodiscard]]
        LogLevelResult SetLogLevel(const std::string &applicationId, const std::string &level) const;

        /**
         * @brief Puts an application back under the installation's own logging configuration.
         *
         * @par
         * Which is not the same as setting it to whatever that configuration says: this removes the
         * override, so the application follows the configuration as it changes from here on.
         */
        [[nodiscard]]
        LogLevelResult ResetLogLevel(const std::string &applicationId) const;

        // -- reporting for itself ---------------------------------------------------------------

        /**
         * @brief Says how busy this instance is, so that the autoscaler can size the pool.
         *
         * @par
         * The one action here an application calls for itself rather than an administrator calling
         * about it - and it may only report for itself: the server checks that the caller is the
         * identity the named application runs as, since otherwise one application could drive
         * another's pool to its ceiling or its floor.
         *
         * @par
         * Meant to be called on a schedule while the application works - every fifteen seconds or
         * so, which is the interval the autoscaler's other signals arrive on. Utilisation is what
         * this instance makes of itself, from 0 to 100; the backlog is work waiting and "active" is
         * work in hand.
         *
         * @par
         * The applicationId is always sent, even though the server can guess it from a caller named
         * "app-<something>". That guess only holds while an application's ID and the identity it
         * runs as agree, and nothing makes them: a pool whose names differed had every report
         * written to a pool that did not exist, answered 200, and left the autoscaler blind for days.
         *
         * @param applicationId the application this instance belongs to.
         * @param options       what to report, and which instance is reporting.
         * @throws EuclidError if no instance id was given and EUCLID_INSTANCE_ID is not set - a
         * report that cannot say which slot it came from cannot be attributed to one.
         */
        [[nodiscard]]
        LoadReport ReportLoad(const std::string &applicationId, const LoadReportOptions &options) const;

        /**
         * @brief This instance's id, out of EUCLID_INSTANCE_ID.
         *
         * @par
         * What the manager hands every process it starts, and what ReportLoad() uses when it is not
         * told otherwise. Empty when nothing set it, which is what running outside euclid looks
         * like.
         */
        [[nodiscard]]
        static std::string InstanceId();

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief EAP's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to EAP.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

    private:

        /**
         * @brief The actions that answer with one application, which is most of them.
         */
        [[nodiscard]]
        Application ApplicationOf(const std::string &action, const boost::json::object &payload) const;

        /**
         * @brief The body apply-platform and plan-platform share.
         */
        [[nodiscard]]
        static boost::json::object PlatformPayload(const std::string &name, const boost::json::value &declaration,
                                                   bool prune);
    };

}// namespace Euclid::CDK::EAP
