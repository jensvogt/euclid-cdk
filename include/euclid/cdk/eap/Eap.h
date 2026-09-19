// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
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
     * Matched exactly, in upper case, and anything else is refused with HTTP 400. A runtime is a
     * category rather than a version, so a JDK 17 and a JDK 25 application are both RuntimeJava and
     * it is the command or the PATH that decides which one runs.
     */
    inline constexpr std::string_view RuntimeJava = "JAVA";
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
    };

}// namespace Euclid::CDK::EAP
