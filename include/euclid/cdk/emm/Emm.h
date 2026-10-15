// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Emm.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EMM {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "emm";

    /**
     * @brief The levels SetLogLevel() accepts, as everywhere else in euclid.
     */
    inline constexpr std::string_view LogTrace = "trace";
    inline constexpr std::string_view LogDebug = "debug";
    inline constexpr std::string_view LogInfo = "info";
    inline constexpr std::string_view LogWarning = "warning";
    inline constexpr std::string_view LogError = "error";
    inline constexpr std::string_view LogFatal = "fatal";
    inline constexpr std::string_view LogOff = "off";

    /**
     * @brief The most worker threads a module will accept.
     *
     * @par
     * The same figure the modules themselves clamp to. A number above it is refused rather than
     * silently corrected, because a stored limit the module will not honour is worse than an error -
     * nothing afterwards would ever say so.
     */
    inline constexpr long MaxWorkerThreads = 256;

    /**
     * @brief New instance limits for a module; -1 leaves one as it stands.
     *
     * @par
     * At least one of the two has to be given. The pair is checked against whatever the other will
     * end up being - the one being set now, or the one already stored - so they can never be left
     * crossed.
     */
    struct EUCLID_CDK_API InstanceLimitOptions {

        /**
         * @brief The smallest the pool goes. Zero is meaningful and allowed: it lets a module scale
         * away entirely when idle.
         */
        long minInstances{-1};

        /**
         * @brief The largest it goes; at least one when it is given at all.
         */
        long maxInstances{-1};
    };

    /**
     * @brief What an export covers, and what seals it.
     */
    struct EUCLID_CDK_API ExportOptions {

        /**
         * @brief Which modules to export; empty exports all of them.
         *
         * @par
         * Exactly one of "all" and a module list - so leaving this empty is what asks for
         * everything, and naming modules is what narrows it.
         */
        std::vector<std::string> modules;

        /**
         * @brief Whether to include the collections only a full export carries - the bulk, rather
         * than the definitions.
         */
        bool full{false};

        /**
         * @brief The passphrase the archive is sealed with; empty leaves it readable.
         *
         * @par
         * **Required when the export covers EKM**, which carries key material itself: the server
         * refuses to write that in the clear. The envelope stays readable either way - which
         * modules, when, and how to derive the key - so a file can be identified and the right
         * passphrase asked for without giving away a byte of what it holds.
         */
        std::string passphrase;
    };

    /**
     * @brief EMM - euclid's module manager: the live process pools behind every module.
     *
     * @par
     * @code
     * const auto session = EAM::Eam::ForServer(url).Credentials("jens", "secret").Login();
     * const EMM::Emm emm(session);
     *
     * for (const auto &module: emm.ListModules().modules) {
     *     std::cout << module.name << " " << module.instances.size() << "\n";
     * }
     * @endcode
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client.
     *
     * @par What a "module" is here
     * Wider than it sounds. euclid's own services are modules, and so is every application pool EAP
     * runs and every transfer server ETS runs - they are all processes the manager starts, watches
     * and restarts. Module::core separates euclid's own from the rest, and it decides what the
     * control calls will accept: **StartModule() and StopModule() are refused for anything that is
     * not core**, because an application's or a transfer server's desired state belongs to EAP and
     * ETS, and anything recorded here would be undone within seconds. EAP::Eap::StopApplication()
     * and ETS::Ets::StopServer() are the calls for those. RestartModule() works on all three, since
     * it changes no desired state for anything else to disagree with.
     *
     * @par Nothing here waits
     * Every call records what is wanted and answers; the manager acts on its next reconcile. So the
     * instance counts that come back are the pool as it was when the request was answered rather
     * than as it will be, and asking twice in quick succession is what Module::desiredMinInstances
     * and its siblings exist for.
     *
     * @par
     * Every action here is administrator-only, server-side, and it is the only module where that is
     * true of all of them - what it exposes is every module's live process pool and the raw database
     * collections behind them.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Emm final : public ModuleClient {
    public:

        /**
         * @brief Builds EMM's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Emm(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Emm(EAM::Session &&) = delete;

        // -- reading ----------------------------------------------------------------------------

        /**
         * @brief Every module the manager knows about, with its instances.
         *
         * @par
         * As persisted in the module repository, which is what the manager reconciles against - so
         * this is what euclid intends as well as what it is running.
         */
        [[nodiscard]]
        ModulesResult ListModules() const;

        // -- running ----------------------------------------------------------------------------

        /**
         * @brief Lets a stopped module run again.
         *
         * @param name the module, by the name it is written down under - lower case, as the keys of
         * euclid.modules are. A mixed-case spelling is tried as given first, so an installation that
         * really did name a module that way still reaches exactly the one it asked for.
         * @throws ServiceError 400 if the module is not core - see the class documentation, 404 if
         * there is no such module.
         */
        [[nodiscard]]
        ModuleStateResult StartModule(const std::string &name) const;

        /**
         * @brief Stops a module and keeps it stopped.
         *
         * @par
         * A desired state rather than a kill: the manager stops the instances on its next reconcile,
         * and will not start them again until StartModule() says so.
         *
         * @throws ServiceError 400 if the module is not core, 404 if there is no such module.
         */
        [[nodiscard]]
        ModuleStateResult StopModule(const std::string &name) const;

        /**
         * @brief Cycles a module's instances, one per reconcile tick.
         *
         * @par
         * One at a time rather than all at once, so a module with more than one instance keeps
         * serving throughout. Allowed for applications and transfer servers as well as core modules,
         * unlike stopping: it changes no desired state, so the same instances come straight back and
         * there is nothing for EAP's or ETS's own reconcile to disagree with.
         *
         * @throws ServiceError 400 if the module is stopped - nothing runs for it, so there is
         * nothing to restart, and honouring this would mean starting it and undoing what StopModule()
         * asked for. Also 404 if there is no such module.
         */
        [[nodiscard]]
        ModuleStateResult RestartModule(const std::string &name) const;

        // -- sizing -----------------------------------------------------------------------------

        /**
         * @brief Changes how many instances a module may run.
         *
         * @par
         * The range the manager works within, not a count; nothing is started or stopped by this
         * call. Read the limits back off InstanceLimits rather than recomputing them, because they
         * are what will stand once the reconcile lands.
         *
         * @param name    the module.
         * @param options the bounds; -1 leaves one as it stands, and at least one is required.
         * @throws EuclidError if neither bound is given. ServiceError 400 if a bound is out of range,
         * or if the pair would end up crossed; 404 if there is no such module.
         */
        [[nodiscard]]
        InstanceLimits SetInstances(const std::string &name, const InstanceLimitOptions &options) const;

        /**
         * @brief Changes how many worker threads a module's processes run with.
         *
         * @par
         * A thread count is fixed when a process starts, so this takes effect as the instances are
         * cycled rather than at once - ThreadCount::runningInstances is how many the manager still
         * has to get through.
         *
         * @param name    the module.
         * @param threads between 1 and MaxWorkerThreads.
         * @throws EuclidError if the count is outside that range - said here rather than paying a
         * round trip for the server's own refusal. ServiceError 404 if there is no such module.
         */
        [[nodiscard]]
        ThreadCount SetThreads(const std::string &name, long threads) const;

        // -- logging ----------------------------------------------------------------------------

        /**
         * @brief Sets the level a module logs at.
         *
         * @param name  the module.
         * @param level LogTrace and its siblings.
         * @throws ServiceError 400 if the level is not one euclid knows, 404 if there is no such
         * module.
         */
        [[nodiscard]]
        ModuleLogLevel SetLogLevel(const std::string &name, const std::string &level) const;

        /**
         * @brief Takes the level off, putting the module back under the installation's own
         * configuration.
         *
         * @par
         * An empty level is what does this rather than a call of its own - which is why passing one
         * to SetLogLevel() is not a mistake.
         */
        [[nodiscard]]
        ModuleLogLevel ResetLogLevel(const std::string &name) const;

        // -- archives ---------------------------------------------------------------------------

        /**
         * @brief Dumps modules' database collections, optionally sealed.
         *
         * @par
         * Answered unparsed, and deliberately: what comes back is raw collection contents whose
         * shape belongs to each module's own storage rather than to this SDK, and a reader here
         * would have to know all of them. Write it out as it came, and hand it back to Import().
         *
         * @par
         * A sealed archive is AES-GCM over the same object an unsealed one answers with, so opening
         * a frame yields something the plain import path already understands.
         *
         * @param options which modules, how much, and the passphrase.
         * @throws ServiceError 400 if the export covers EKM without a passphrase.
         */
        [[nodiscard]]
        boost::json::object Export(const ExportOptions &options = {}) const;

        /**
         * @brief Writes an export back.
         *
         * @par
         * Takes the object Export() answered with, as it was. A sealed archive additionally needs
         * the passphrase; the rest of the import cannot tell a sealed file from a plain one once it
         * is open, and neither can the caller.
         *
         * @par
         * An unrecognised collection name is skipped rather than written - an import file is
         * client-supplied input crossing a trust boundary, not something the server generated. What
         * was skipped and why is in the answer, under "skipped", beside what was written under
         * "imported": worth reading, because a file that imported nothing at all still answers 200.
         *
         * @param archive    the export, as Export() gave it.
         * @param passphrase the passphrase, for a sealed archive.
         * @param modules    only these modules out of the file; empty imports everything in it.
         * @throws ServiceError 400 for a sealed archive with no passphrase, and for a wrong
         * passphrase or a tampered file - which fail in the same way, because GCM authenticates as
         * it decrypts and telling the two apart is not something to offer.
         */
        [[nodiscard]]
        boost::json::object Import(const boost::json::object &archive, const std::string &passphrase = {},
                                   const std::vector<std::string> &modules = {}) const;

    private:

        /**
         * @brief The two actions that only differ in the state they ask for.
         */
        [[nodiscard]]
        ModuleStateResult SetStopped(const std::string &action, const std::string &name) const;
    };

}// namespace Euclid::CDK::EMM
