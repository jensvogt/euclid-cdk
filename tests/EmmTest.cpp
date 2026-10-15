// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EMM, end to end against a fake euclid server.
 *
 * @par
 * Two things here are easy to get wrong and expensive to get wrong. One is the -1 sentinel: an
 * instance that has never reported its load reads -1, and a reader defaulting the field to zero
 * would report it as idle - which is what the autoscaler acts on. The other is the "absent means
 * leave it alone" rule on the limits, where sending a bound nobody asked to change would overwrite
 * it.
 */

// C++ includes
#include <string>
#include <tuple>

// Boost includes
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/emm/Emm.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    struct EmmClient {

        explicit EmmClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), emm(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EMM::Emm emm;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief One module with one instance, as the manager records it.
     */
    boost::json::object module(const std::string &name = "esm", const bool core = true) {
        return {
                {"name", name},
                {"executable", "/usr/local/euclid/bin/euclid-" + name},
                {"socketPath", "/var/run/euclid/" + name + ".sock"},
                {"active", true},
                {"core", core},
                {"desiredStopped", false},
                {"autoRestart", true},
                {"maxRestarts", 5},
                {"minInstances", 1},
                {"maxInstances", 4},
                {"desiredMinInstances", -1},
                {"desiredMaxInstances", -1},
                {"desiredThreads", -1},
                {"logLevel", "info"},
                {"created", "2026-09-01T08:00:00Z"},
                {"modified", "2026-10-15T08:00:00Z"},
                {"lastStartTime", "2026-10-15T07:00:00Z"},
                {"instances", boost::json::array{boost::json::object{
                                      {"instanceId", "i-1"},
                                      {"pid", 4711},
                                      {"host", ""},
                                      {"state", "RUNNING"},
                                      {"socketPath", "/var/run/euclid/" + name + "-1.sock"},
                                      {"httpPort", 34001},
                                      {"restartCount", 0},
                                      {"utilisation", 42},
                                      {"backlog", 7},
                                      {"loadReportedAt", "2026-10-15T08:00:00Z"},
                                      {"backgroundTasks", 0},
                                      {"started", "2026-10-15T07:00:00Z"},
                                      {"created", "2026-09-01T08:00:00Z"},
                                      {"modified", "2026-10-15T08:00:00Z"}}}},
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EmmModuleTest)

    BOOST_AUTO_TEST_CASE(ListsModulesAndTheirInstances) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"modules", boost::json::array{module(), module("app-orders-a1b2", false)}}, {"total", 2}});
        const EmmClient client(Test::Answering(answer));

        const auto listed = client.emm.ListModules();

        BOOST_TEST(listed.total == 2L);
        BOOST_REQUIRE(listed.modules.size() == 2U);
        BOOST_TEST(listed.modules[0].name == "esm");
        BOOST_TEST(listed.modules[0].core);
        BOOST_TEST(listed.modules[0].minInstances == 1L);
        BOOST_TEST(listed.modules[0].logLevel == "info");
        // Worth reading before asking: only a core module can be started and stopped here.
        BOOST_TEST(!listed.modules[1].core);

        BOOST_REQUIRE(listed.modules[0].instances.size() == 1U);
        const auto &instance = listed.modules[0].instances[0];
        BOOST_TEST(instance.instanceId == "i-1");
        BOOST_TEST(instance.pid == 4711L);
        BOOST_TEST(instance.httpPort == 34001L);
        BOOST_TEST(instance.utilisation == 42L);
        BOOST_TEST(instance.backlog == 7L);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-target"]) == "emm");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "list-modules");
    }

    // -1 is "has never reported", which is a different thing from reporting no load: a module that
    // does not report, an SDK too old to know how, or a call being refused. Reading a missing field
    // as zero would present it to the autoscaler as a healthy idle instance.
    BOOST_AUTO_TEST_CASE(AnInstanceThatNeverReportedReadsMinusOneRatherThanZero) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"modules", boost::json::array{boost::json::object{
                                    {"name", "esm"},
                                    {"instances", boost::json::array{boost::json::object{{"instanceId", "i-1"}}}}}}},
                {"total", 1}});
        const EmmClient client(Test::Answering(answer));

        const auto &instance = client.emm.ListModules().modules[0].instances[0];
        BOOST_TEST(instance.utilisation == -1L);
        BOOST_TEST(instance.backlog == -1L);
        BOOST_TEST(instance.loadReportedAt.empty());
    }

    // Likewise for the pending limits: -1 is "nothing pending", and zero is a floor somebody set.
    BOOST_AUTO_TEST_CASE(NothingPendingReadsMinusOne) {
        const EmmClient client(Test::Answering(R"({"modules": [{"name": "esm"}], "total": 1})"));

        const auto &only = client.emm.ListModules().modules[0];
        BOOST_TEST(only.desiredMinInstances == -1L);
        BOOST_TEST(only.desiredMaxInstances == -1L);
        BOOST_TEST(only.desiredThreads == -1L);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EmmControlTest)

    BOOST_AUTO_TEST_CASE(StopsAndStartsAModule) {
        const EmmClient client(Test::Answering(R"({"name": "esm", "stopped": true, "runningInstances": 2})"));

        const auto stopped = client.emm.StopModule("esm");
        BOOST_TEST(stopped.stopped);
        // The pool as it was when the request was answered: the manager acts on its next reconcile.
        BOOST_TEST(stopped.runningInstances == 2L);
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "esm");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "stop-module");

        std::ignore = client.emm.StartModule("esm");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "start-module");
    }

    BOOST_AUTO_TEST_CASE(RestartsAModule) {
        const EmmClient client(Test::Answering(R"({"name": "esm", "runningInstances": 3})"));

        const auto restarted = client.emm.RestartModule("esm");
        BOOST_TEST(restarted.runningInstances == 3L);
        // Changes no desired state, which is why it is allowed for applications too.
        BOOST_TEST(!restarted.stopped);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "restart-module");
    }

    // An application's desired state belongs to EAP, so stopping one here would be undone within
    // seconds - the server refuses it and points at the call that works.
    BOOST_AUTO_TEST_CASE(CarriesTheRefusalForSomethingThatIsNotACoreModule) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(400, R"({"error": "app-orders is not a euclid module - use \"eap stop-application\""})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EMM::Emm emm(session);

        try {
            std::ignore = emm.StopModule("app-orders");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "emm");
            BOOST_TEST(ex.Action() == "stop-module");
            BOOST_TEST(ex.Status() == 400);
        }
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EmmSizingTest)

    // Absent is what means "leave that one as it is", which is what lets a ceiling be raised
    // without restating the floor - so only what was asked for goes on the wire.
    BOOST_AUTO_TEST_CASE(SendsOnlyTheBoundItWasGiven) {
        const EmmClient client(Test::Answering(R"({"name": "esm", "minInstances": 1, "maxInstances": 8})"));

        const auto limits = client.emm.SetInstances("esm", {.maxInstances = 8});

        BOOST_TEST(limits.maxInstances == 8L);
        const auto body = lastBody(client.gateway);
        BOOST_TEST(!body.as_object().contains("minInstances"));
        BOOST_TEST(body.at("maxInstances").as_int64() == 8);
    }

    // Zero is a meaningful floor - it lets a module scale away entirely when idle - so it has to
    // travel rather than be read as "not given".
    BOOST_AUTO_TEST_CASE(AFloorOfZeroIsSent) {
        const EmmClient client(Test::Answering(R"({"name": "esm", "minInstances": 0, "maxInstances": 4})"));

        std::ignore = client.emm.SetInstances("esm", {.minInstances = 0});
        BOOST_TEST(lastBody(client.gateway).at("minInstances").as_int64() == 0);
    }

    BOOST_AUTO_TEST_CASE(RefusesAChangeThatNamesNeitherBound) {
        const EmmClient client(Test::Answering("{}"));

        BOOST_CHECK_THROW(std::ignore = client.emm.SetInstances("esm", {}), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

    BOOST_AUTO_TEST_CASE(SetsWorkerThreads) {
        const EmmClient client(Test::Answering(R"({"name": "esm", "threads": 16, "runningInstances": 2})"));

        const auto threads = client.emm.SetThreads("esm", 16);

        BOOST_TEST(threads.threads == 16L);
        // A thread count is fixed when a process starts, so this is how many the manager still has
        // to cycle through.
        BOOST_TEST(threads.runningInstances == 2L);
        BOOST_TEST(lastBody(client.gateway).at("threads").as_int64() == 16);
    }

    // A stored count the module will not honour is worse than an error, because nothing afterwards
    // would ever say so.
    BOOST_AUTO_TEST_CASE(RefusesAThreadCountOutsideTheRangeAModuleHonours) {
        const EmmClient client(Test::Answering("{}"));

        BOOST_CHECK_THROW(std::ignore = client.emm.SetThreads("esm", 0), EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.emm.SetThreads("esm", EMM::MaxWorkerThreads + 1), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);
    }

    BOOST_AUTO_TEST_CASE(SetsAndTakesBackALogLevel) {
        const EmmClient client(Test::Answering(R"({"name": "esm", "logLevel": "debug", "channel": "module.esm"})"));

        const auto level = client.emm.SetLogLevel("esm", std::string(EMM::LogDebug));
        BOOST_TEST(level.logLevel == "debug");
        BOOST_TEST(level.channel == "module.esm");
        BOOST_TEST(lastBody(client.gateway).at("logLevel").as_string() == "debug");

        // An empty level is what puts the module back under the installation's own configuration.
        std::ignore = client.emm.ResetLogLevel("esm");
        BOOST_TEST(lastBody(client.gateway).at("logLevel").as_string() == "");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EmmArchiveTest)

    // Exactly one of "all" and a module list, so naming no modules sends "all" rather than an empty
    // array - which is neither of the two and is refused.
    BOOST_AUTO_TEST_CASE(ExportingEverythingAsksForAllRatherThanAnEmptyList) {
        const EmmClient client(Test::Answering(R"({"modules": ["esm"], "collections": {}})"));

        std::ignore = client.emm.Export();

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("all").as_bool() == true);
        BOOST_TEST(!body.as_object().contains("modules"));
        BOOST_TEST(body.at("full").as_bool() == false);
    }

    BOOST_AUTO_TEST_CASE(ExportingSomeModulesNamesThemInstead) {
        const EmmClient client(Test::Answering(R"({"modules": ["esm"], "collections": {}})"));

        std::ignore = client.emm.Export({.modules = {"esm", "eqs"}, .full = true, .passphrase = "hunter2"});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(!body.as_object().contains("all"));
        BOOST_REQUIRE(body.at("modules").as_array().size() == 2U);
        BOOST_TEST(body.at("full").as_bool() == true);
        BOOST_TEST(body.at("passphrase").as_string() == "hunter2");
    }

    // An export answers with exactly what an import reads, so the archive travels as it came rather
    // than being picked apart - which is what keeps a file this SDK never has to understand
    // importable.
    BOOST_AUTO_TEST_CASE(ImportsTheArchiveAsItCameWithTheExtrasWrittenOver) {
        const EmmClient client(Test::Answering(R"({"imported": {"esm_bucket": {"imported": 3}}, "skipped": []}
                                               )"));

        const boost::json::object archive{
                {"encrypted", true},
                {"salt", "c2FsdA=="},
                {"data", "c2VhbGVk"},
                {"exportedAt", "2026-10-15T08:00:00Z"}};

        const auto result = client.emm.Import(archive, "hunter2", {"esm"});

        BOOST_TEST(result.at("imported").at("esm_bucket").at("imported").as_int64() == 3);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("data").as_string() == "c2VhbGVk");
        BOOST_TEST(body.at("salt").as_string() == "c2FsdA==");
        BOOST_TEST(body.at("passphrase").as_string() == "hunter2");
        BOOST_REQUIRE(body.at("modules").as_array().size() == 1U);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "import");
    }

    BOOST_AUTO_TEST_CASE(ImportsAPlainArchiveWithoutInventingAPassphrase) {
        const EmmClient client(Test::Answering(R"({"imported": {}, "skipped": []})"));

        std::ignore = client.emm.Import({{"collections", boost::json::object{}}});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(!body.as_object().contains("passphrase"));
        BOOST_TEST(!body.as_object().contains("modules"));
    }

BOOST_AUTO_TEST_SUITE_END()
