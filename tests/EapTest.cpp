// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EAP, end to end against a fake euclid server.
 *
 * @par
 * A deployment is a definition rather than a process, so what these check is what a definition is
 * written as: which fields an update sends and which it leaves alone, that starting one asks rather
 * than waits, and that a load report always says which application it belongs to - the field whose
 * absence once sent nine hundred reports to a pool that did not exist.
 */

// C++ includes
#include <cstdlib>
#include <string>
#include <tuple>

// Boost includes
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/eap/Eap.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    /**
     * @brief A gateway, a logged-in session and an EAP client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EapClient {

        explicit EapClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), eap(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EAP::Eap eap;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief One application, as the server describes it.
     */
    boost::json::object application(const std::string &desiredState = "STOPPED", const std::string &state = "STOPPED") {
        return {
                {"applicationId", "order-service"},
                {"runtimeName", "order-service-a1b2"},
                {"ern", "ern:euclid:eap:eu-central-1:000000000000:application/order-service"},
                {"accountId", "000000000000"},
                // "namespace" here and in ETS; everywhere else it is "nameSpace".
                {"namespace", "reports"},
                {"region", "eu-central-1"},
                {"runtime", "BINARY"},
                {"bucketErn", "ern:euclid:esm:eu-central-1:000000000000:bucket/artifacts"},
                {"artifactKey", "order-service-1.4.0"},
                {"version", "1.4.0"},
                {"md5Sum", "d41d8cd98f00b204e9800998ecf8427e"},
                {"command", ""},
                {"arguments", boost::json::array{"--port", "8080"}},
                {"environment", boost::json::object{{"EUCLID_LOG_LEVEL", "info"}}},
                {"resources", boost::json::array{"ern:queue/orders", "ern:bucket/invoices"}},
                {"userId", "app-order-service-a1b2"},
                {"logLevel", ""},
                {"minInstances", 1},
                {"maxInstances", 4},
                {"readyTimeoutMs", 30000},
                {"desiredState", desiredState},
                {"state", state},
                {"instances", state == "RUNNING" ? 2 : 0},
                {"endpoints", state == "RUNNING"
                                      ? boost::json::array{boost::json::object{{"instanceId", "i-1"}, {"pid", 4711}, {"httpPort", 34001}},
                                                           boost::json::object{{"instanceId", "i-2"}, {"pid", 4712}, {"httpPort", 34002}}}
                                      : boost::json::array{}},
                {"created", "2026-09-01T08:00:00Z"},
                {"modified", "2026-09-19T08:00:00Z"},
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EapDeploymentTest)

    BOOST_AUTO_TEST_CASE(DeploysAnApplicationStoppedAndReadsItBack) {
        const EapClient client(Test::Answering(boost::json::serialize(application())));

        const auto deployed = client.eap.CreateApplication("order-service", std::string(EAP::RuntimeBinary),
                                                           "artifacts", "order-service-1.4.0",
                                                           {.arguments = {"--port", "8080"},
                                                            .environment = {{"EUCLID_LOG_LEVEL", "info"}},
                                                            .buckets = {"invoices"},
                                                            .queues = {"orders"},
                                                            .maxInstances = 4});

        BOOST_TEST(deployed.applicationId == "order-service");
        BOOST_TEST(deployed.runtimeName == "order-service-a1b2");
        BOOST_TEST(deployed.nameSpace == "reports");
        // Deployed by name, described by ERN - the server resolves the one into the other.
        BOOST_TEST(deployed.bucketErn == "ern:euclid:esm:eu-central-1:000000000000:bucket/artifacts");
        BOOST_TEST(deployed.artifactKey == "order-service-1.4.0");
        BOOST_REQUIRE(deployed.resources.size() == 2U);
        BOOST_TEST(deployed.arguments[1] == "8080");
        BOOST_TEST(deployed.environment.at("EUCLID_LOG_LEVEL") == "info");
        // The identity it runs as, which euclid made for it rather than a person's.
        BOOST_TEST(deployed.userId == "app-order-service-a1b2");
        // Nothing runs yet.
        BOOST_TEST(deployed.desiredState == std::string(EAP::StateStopped));
        BOOST_TEST(!deployed.IsRunning());

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("runtime").as_string() == std::string(EAP::RuntimeBinary));
        BOOST_TEST(body.at("bucket").as_string() == "artifacts");
        BOOST_TEST(body.at("artifact").as_string() == "order-service-1.4.0");
        BOOST_TEST(body.at("queues").as_array().size() == 1U);
        BOOST_TEST(body.at("buckets").as_array().size() == 1U);
        // The defaults travel rather than being left to the server to guess at.
        BOOST_TEST(body.at("minInstances").as_int64() == EAP::DefaultMinInstances);
        BOOST_TEST(body.at("maxInstances").as_int64() == 4);
        BOOST_TEST(body.at("readyTimeoutMs").as_int64() == EAP::DefaultReadyTimeoutMs);
        // No user named: euclid makes a technical principal, which is the better answer.
        BOOST_TEST(body.at("user").as_string() == "");
    }

    // The server distinguishes a field being sent from one that is not, rather than one value from
    // A copy names where it is going, and says nothing about the name unless it is being changed:
    // the server reads an absent targetApplicationId as "the original's name", so sending an empty
    // one would be asking for an application with no name at all.
    BOOST_AUTO_TEST_CASE(ACopyNamesTheTargetNamespace) {
        const EapClient client(Test::Answering(boost::json::serialize(application())));

        std::ignore = client.eap.CopyApplication("order-service", "production");

        auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("applicationId").as_string() == "order-service");
        BOOST_TEST(body.at("targetNamespace").as_string() == "production");
        BOOST_TEST(!body.contains("targetApplicationId"));

        std::ignore = client.eap.CopyApplication("order-service", "development", "order-service-next");

        body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("targetNamespace").as_string() == "development");
        BOOST_TEST(body.at("targetApplicationId").as_string() == "order-service-next");
    }

    // A bound left at -1 is not sent at all: the server reads an absent one as "leave it as it
    // stands", which is what lets a ceiling be raised without disturbing the floor under it.
    BOOST_AUTO_TEST_CASE(ScalingSendsOnlyTheBoundItWasGiven) {
        const EapClient client(Test::Answering(boost::json::serialize(application())));

        std::ignore = client.eap.ScaleApplication("order-service", {.maxInstances = 16});

        auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("applicationId").as_string() == "order-service");
        BOOST_TEST(body.at("maxInstances").as_int64() == 16);
        BOOST_TEST(!body.contains("minInstances"));

        std::ignore = client.eap.ScaleApplication("order-service", {.minInstances = 4});

        body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("minInstances").as_int64() == 4);
        BOOST_TEST(!body.contains("maxInstances"));

        // Both together pins the pool, which is a normal thing to ask for.
        std::ignore = client.eap.ScaleApplication("order-service", {.minInstances = 2, .maxInstances = 2});

        body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("minInstances").as_int64() == 2);
        BOOST_TEST(body.at("maxInstances").as_int64() == 2);
    }

    // another - so an update says only what it means to change.
    BOOST_AUTO_TEST_CASE(SendsOnlyWhatAnUpdateNames) {
        const EapClient client(Test::Answering(boost::json::serialize(application())));

        std::ignore = client.eap.UpdateApplication("order-service", {.maxInstances = 8});

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("applicationId").as_string() == "order-service");
        BOOST_TEST(body.at("maxInstances").as_int64() == 8);
        BOOST_TEST(!body.contains("runtime"));
        BOOST_TEST(!body.contains("artifact"));
        BOOST_TEST(!body.contains("buckets"));
        BOOST_TEST(!body.contains("queues"));
        BOOST_TEST(!body.contains("namespace"));
    }

    // An empty command is a value: it hands the artifact back to the runtime's own interpreter.
    BOOST_AUTO_TEST_CASE(SendsAnEmptyValueThatWasAskedFor) {
        const EapClient client(Test::Answering(boost::json::serialize(application())));

        std::ignore = client.eap.UpdateApplication("order-service", {.command = "", .nameSpace = ""});

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.contains("command"));
        BOOST_TEST(body.at("command").as_string() == "");
        // An empty namespace moves the application back to the account root, so it has to travel.
        BOOST_TEST(body.contains("namespace"));
        BOOST_TEST(body.at("namespace").as_string() == "");
    }

    // Both or neither: they are re-resolved together, so naming one revokes what the other granted.
    BOOST_AUTO_TEST_CASE(GrantsAreNamedTogether) {
        const EapClient client(Test::Answering(boost::json::serialize(application())));

        std::ignore = client.eap.UpdateApplication("order-service", {.buckets = std::vector<std::string>{"invoices"},
                                                                     .queues = std::vector<std::string>{}});

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("buckets").as_array().size() == 1U);
        BOOST_TEST(body.contains("queues"));
        BOOST_TEST(body.at("queues").as_array().empty());
    }

    // Absent asks for the artifact already deployed, which is what a rebuild under the same key
    // wants - and the server refuses a redeploy that would change neither version nor checksum.
    BOOST_AUTO_TEST_CASE(RedeployDefaultsToTheArtifactAlreadyDeployed) {
        const EapClient client(Test::Answering(boost::json::serialize(application())));

        std::ignore = client.eap.RedeployApplication("order-service");
        auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("applicationId").as_string() == "order-service");
        BOOST_TEST(!body.contains("artifact"));
        BOOST_TEST(!body.contains("version"));

        std::ignore = client.eap.RedeployApplication("order-service", "order-service-1.5.0", "1.5.0");
        body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("artifact").as_string() == "order-service-1.5.0");
        BOOST_TEST(body.at("version").as_string() == "1.5.0");
    }

    BOOST_AUTO_TEST_CASE(DeletesAnApplication) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "deleted": true})"));

        client.eap.DeleteApplication("order-service");
        BOOST_TEST(lastBody(client.gateway).at("applicationId").as_string() == "order-service");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "delete-application");
    }

    BOOST_AUTO_TEST_CASE(TheRuntimeConstantsAreTheStringsEapAccepts) {

        // Spelled by hand rather than derived, because the server matches them exactly and refuses
        // anything else with a 400. A constant that drifted to "JAVA-21" would still compile and
        // read perfectly at the call site, and fail only against a running installation.
        BOOST_TEST(EAP::RuntimeJava == "JAVA");
        BOOST_TEST(EAP::RuntimeJava21 == "JAVA21");
        BOOST_TEST(EAP::RuntimeJava25 == "JAVA25");

        // Three distinct runtimes, not one with aliases: a jar built for 25 does not start on 21,
        // so asking for one and getting the other is the failure these exist to prevent.
        BOOST_TEST(EAP::RuntimeJava != EAP::RuntimeJava21);
        BOOST_TEST(EAP::RuntimeJava21 != EAP::RuntimeJava25);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EapRunningTest)

    // Asking is all a start does: the manager is what turns a desired state into processes.
    BOOST_AUTO_TEST_CASE(StartingAsksForItRatherThanWaitingForIt) {
        const EapClient client(Test::Answering(boost::json::serialize(application("RUNNING", "STOPPED"))));

        const auto started = client.eap.StartApplication("order-service");
        BOOST_TEST(started.desiredState == std::string(EAP::StateRunning));
        // What is observed has not caught up yet, and that is not an error.
        BOOST_TEST(started.state == std::string(EAP::StateStopped));
        BOOST_TEST(!started.IsRunning());
    }

    BOOST_AUTO_TEST_CASE(ReadsTheInstancesThatAreAnswering) {
        const EapClient client(Test::Answering(boost::json::serialize(application("RUNNING", "RUNNING"))));

        const auto running = client.eap.GetApplication("order-service");
        BOOST_TEST(running.IsRunning());
        BOOST_TEST(running.instances == 2L);
        BOOST_REQUIRE(running.endpoints.size() == 2U);
        BOOST_TEST(running.endpoints[0].instanceId == "i-1");
        BOOST_TEST(running.endpoints[0].pid == 4711L);
        BOOST_TEST(running.endpoints[1].httpPort == 34002L);
    }

    BOOST_AUTO_TEST_CASE(StopsAnApplication) {
        const EapClient client(Test::Answering(boost::json::serialize(application("STOPPED", "RUNNING"))));

        const auto stopped = client.eap.StopApplication("order-service");
        BOOST_TEST(stopped.desiredState == std::string(EAP::StateStopped));
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "stop-application");
    }

    // A restart records a request; the pool it names is the one about to be cycled.
    BOOST_AUTO_TEST_CASE(RestartingSaysWhatIsAboutToBeCycled) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "restarting": true, "instances": 2})"));

        const auto restart = client.eap.RestartApplication("order-service");
        BOOST_TEST(restart.restarting);
        BOOST_TEST(restart.instances == 2L);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "restart-application");
    }

    // Every match at once: an installation has tens of applications rather than thousands.
    BOOST_AUTO_TEST_CASE(ListsApplicationsByPrefix) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"applications", boost::json::array{application(), application("RUNNING", "RUNNING")}}});

        const EapClient client(Test::Answering(listed));

        const auto applications = client.eap.ListApplications("order");
        BOOST_REQUIRE(applications.size() == 2U);
        BOOST_TEST(!applications[0].IsRunning());
        BOOST_TEST(applications[1].IsRunning());
        BOOST_TEST(lastBody(client.gateway).at("prefix").as_string() == "order");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EapLoggingTest)

    BOOST_AUTO_TEST_CASE(SetsWhatOneApplicationLogsAt) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "logLevel": "debug",
                                                   "channel": "application.order-service-a1b2"})"));

        const auto result = client.eap.SetLogLevel("order-service", std::string(EAP::LogDebug));
        BOOST_TEST(result.logLevel == std::string(EAP::LogDebug));
        BOOST_TEST(result.channel == "application.order-service-a1b2");
        BOOST_TEST(lastBody(client.gateway).at("level").as_string() == "debug");
    }

    // Taking the override off, which is not the same as setting the level it happens to have now:
    // the application follows the configuration as that changes from here on.
    BOOST_AUTO_TEST_CASE(ResettingSendsAnEmptyLevel) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "logLevel": "",
                                                   "channel": "application.order-service-a1b2"})"));

        const auto result = client.eap.ResetLogLevel("order-service");
        BOOST_TEST(result.logLevel.empty());
        BOOST_TEST(lastBody(client.gateway).at("level").as_string() == "");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "set-log-level");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EapLoadReportTest)

    // The applicationId always travels. The server can guess it from a caller named "app-<x>", and
    // that guess is wrong whenever the id and the identity differ - silently, with a 200.
    BOOST_AUTO_TEST_CASE(AReportAlwaysSaysWhichApplicationItIsFor) {
        const EapClient client(Test::Answering(R"({"instanceId": "i-1", "utilisation": 42.5, "backlog": 7, "active": 3})"));

        const auto report = client.eap.ReportLoad("order-service", {.utilisation = 42.5,
                                                                    .backlog = 7,
                                                                    .active = 3,
                                                                    .instanceId = "i-1"});
        BOOST_TEST(report.instanceId == "i-1");
        // A measurement rather than a count: 42.5 is not 42.
        BOOST_TEST(report.utilisation == 42.5);
        BOOST_TEST(report.backlog == 7L);
        BOOST_TEST(report.active == 3L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("applicationId").as_string() == "order-service");
        BOOST_TEST(body.at("instanceId").as_string() == "i-1");
        BOOST_TEST(body.at("utilisation").as_double() == 42.5);
        BOOST_TEST(body.at("backlog").as_int64() == 7);
        BOOST_TEST(body.at("active").as_int64() == 3);
    }

    // Which slot is reporting comes from the environment the manager set, so an application does not
    // have to carry it around.
    BOOST_AUTO_TEST_CASE(TheInstanceComesFromTheEnvironmentWhenItIsNotNamed) {
#if defined(_WIN32)
        _putenv_s("EUCLID_INSTANCE_ID", "i-from-env");
#else
        setenv("EUCLID_INSTANCE_ID", "i-from-env", 1);
#endif
        BOOST_TEST(EAP::Eap::InstanceId() == "i-from-env");

        const EapClient client(Test::Answering(R"({"instanceId": "i-from-env", "utilisation": 10})"));
        std::ignore = client.eap.ReportLoad("order-service", {.utilisation = 10});
        BOOST_TEST(lastBody(client.gateway).at("instanceId").as_string() == "i-from-env");

#if defined(_WIN32)
        _putenv_s("EUCLID_INSTANCE_ID", "");
#else
        unsetenv("EUCLID_INSTANCE_ID");
#endif
    }

    // A report that cannot say which slot it came from cannot be attributed to one, and attributing
    // it to the wrong one would be worse than dropping it.
    BOOST_AUTO_TEST_CASE(RefusesAReportWithNoInstanceBeforeTheRoundTrip) {
#if defined(_WIN32)
        _putenv_s("EUCLID_INSTANCE_ID", "");
#else
        unsetenv("EUCLID_INSTANCE_ID");
#endif
        const EapClient client(Test::Answering(R"({"instanceId": ""})"));

        BOOST_CHECK_THROW(std::ignore = client.eap.ReportLoad("order-service", {.utilisation = 10}), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EapTransportTest)

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EapClient client(Test::Answering(boost::json::serialize(application())));

        client.session.ChangeNamespace("reports");
        std::ignore = client.eap.GetApplication("order-service");

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "eap");
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "reports");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

    // Every action but the load report is administrator-only, and the refusal says so.
    BOOST_AUTO_TEST_CASE(CarriesTheServersReasonWhenTheCallerIsNotAnAdministrator) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(403, R"({"error": "Administrator privileges required"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EAP::Eap eap(session);

        try {
            std::ignore = eap.CreateApplication("order-service", std::string(EAP::RuntimeJava), "artifacts", "app.jar");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "eap");
            BOOST_TEST(ex.Action() == "create-application");
            BOOST_TEST(ex.Status() == 403);
            BOOST_TEST(ex.Reason() == "Administrator privileges required");
        }
    }

    BOOST_AUTO_TEST_CASE(ReachesItsMetricsAndAnActionThisSdkDoesNotWrap) {
        const EapClient client(Test::Answering(R"({"eap_application_count": 3, "whatever": 42})"));

        BOOST_TEST(client.eap.Metrics().at("eap_application_count").as_int64() == 3);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");

        BOOST_TEST(client.eap.Call("some-future-action").at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

BOOST_AUTO_TEST_SUITE_END()
