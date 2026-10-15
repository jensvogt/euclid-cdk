// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/emm/Emm.h>

namespace Euclid::CDK::EMM {

    namespace {

        /**
         * @brief A JSON array of strings, for the payload fields that take a list.
         */
        boost::json::array stringsOf(const std::vector<std::string> &values) {
            boost::json::array array;
            for (const auto &value: values) array.emplace_back(value);
            return array;
        }

    }// namespace

    Emm::Emm(const EAM::Session &session) : ModuleClient(session, std::string(EMM::Target)) {}

    // -- reading ------------------------------------------------------------------------------------

    ModulesResult Emm::ListModules() const {
        return ToModulesResult(Call("list-modules"));
    }

    // -- running ------------------------------------------------------------------------------------

    ModuleStateResult Emm::StartModule(const std::string &name) const {
        return SetStopped("start-module", name);
    }

    ModuleStateResult Emm::StopModule(const std::string &name) const {
        return SetStopped("stop-module", name);
    }

    ModuleStateResult Emm::RestartModule(const std::string &name) const {
        return ToModuleStateResult(Call("restart-module", {{"name", name}}));
    }

    // -- sizing -------------------------------------------------------------------------------------

    InstanceLimits Emm::SetInstances(const std::string &name, const InstanceLimitOptions &options) const {

        // Refused here rather than on arrival: a call naming neither bound asks for no change at
        // all, which the server answers 400 for, and the argument says so without a round trip.
        if (options.minInstances < 0 && options.maxInstances < 0) {
            throw EuclidError("set at least one of minInstances or maxInstances - -1 leaves a bound as it stands, "
                              "and leaving both asks for nothing");
        }

        boost::json::object payload{{"name", name}};
        // Sent only when asked for, because absent is what means "leave that one as it is" - which
        // is what lets a ceiling be raised without restating the floor. Zero is a real floor and
        // travels, so the test is against zero rather than for truthiness.
        if (options.minInstances >= 0) payload["minInstances"] = options.minInstances;
        if (options.maxInstances >= 0) payload["maxInstances"] = options.maxInstances;

        return ToInstanceLimits(Call("set-instances", payload));
    }

    ThreadCount Emm::SetThreads(const std::string &name, const long threads) const {

        // The range the modules themselves clamp to. Refused rather than sent and corrected, for
        // the server's own reason: a stored count the module will not honour is worse than an
        // error, because nothing afterwards would ever say so.
        if (threads < 1 || threads > MaxWorkerThreads) {
            throw EuclidError("threads has to be between 1 and " + std::to_string(MaxWorkerThreads));
        }

        return ToThreadCount(Call("set-threads", {{"name", name}, {"threads", threads}}));
    }

    // -- logging ------------------------------------------------------------------------------------

    ModuleLogLevel Emm::SetLogLevel(const std::string &name, const std::string &level) const {
        return ToModuleLogLevel(Call("set-log-level", {{"name", name}, {"logLevel", level}}));
    }

    ModuleLogLevel Emm::ResetLogLevel(const std::string &name) const {
        // An empty level is what takes the override off rather than setting one.
        return SetLogLevel(name, "");
    }

    // -- archives -----------------------------------------------------------------------------------

    boost::json::object Emm::Export(const ExportOptions &options) const {

        boost::json::object payload{{"full", options.full}};

        // Exactly one of "all" and a module list, so naming no modules has to send "all" rather
        // than an empty array - an empty list is neither of the two and is refused.
        if (options.modules.empty()) {
            payload["all"] = true;
        } else {
            payload["modules"] = stringsOf(options.modules);
        }
        if (!options.passphrase.empty()) payload["passphrase"] = options.passphrase;

        return Call("export", payload);
    }

    boost::json::object Emm::Import(const boost::json::object &archive, const std::string &passphrase,
                                    const std::vector<std::string> &modules) const {

        // The archive is the body, with the two extras written over it: an export answers with
        // exactly what an import reads, so copying it rather than picking fields out of it is what
        // keeps a file this SDK never has to understand importable.
        auto payload = archive;
        if (!passphrase.empty()) payload["passphrase"] = passphrase;
        if (!modules.empty()) payload["modules"] = stringsOf(modules);

        return Call("import", payload);
    }

    ModuleStateResult Emm::SetStopped(const std::string &action, const std::string &name) const {
        return ToModuleStateResult(Call(action, {{"name", name}}));
    }

}// namespace Euclid::CDK::EMM
