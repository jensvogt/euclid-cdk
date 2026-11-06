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

    // ── infrastructure ──────────────────────────────────────────────────────

    BOOST_AUTO_TEST_CASE(ApplyingADeclarationNamesWhatItCreated) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "declared": true,
                "created": ["ern:eqs:eu-central-1:000000000000:development:queue:orders"],
                "deleted": [], "granted": ["access-queue-consume"], "revoked": []})"));

        const auto applied = client.eap.ApplyInfrastructure("order-service");
        BOOST_TEST(applied.declared);
        BOOST_REQUIRE(applied.created.size() == 1U);
        BOOST_TEST(applied.created[0] == "ern:eqs:eu-central-1:000000000000:development:queue:orders");
        BOOST_TEST(applied.granted.size() == 1U);
        BOOST_TEST(applied.deleted.empty());
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "apply-infrastructure");
    }

    // The half worth reading before trusting a declaration: a reconcile is full, so a resource this
    // application created and the file no longer names goes, and takes its messages with it. Named
    // here rather than counted, so a removal nobody intended is visible in the answer.
    BOOST_AUTO_TEST_CASE(ApplyingADeclarationNamesWhatItRemoved) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "declared": true,
                "created": [], "deleted": ["ern:eqs:eu-central-1:000000000000:development:queue:retired"],
                "granted": [], "revoked": ["access-queue-produce"]})"));

        const auto applied = client.eap.ApplyInfrastructure("order-service");
        BOOST_REQUIRE(applied.deleted.size() == 1U);
        BOOST_TEST(applied.deleted[0] == "ern:eqs:eu-central-1:000000000000:development:queue:retired");
        BOOST_TEST(applied.revoked.size() == 1U);
    }

    // An application that provisions its resources by hand has no declaration, and that is an answer
    // rather than a failure: nothing was created, and `declared` says why.
    BOOST_AUTO_TEST_CASE(AnApplicationWithNoDeclarationIsAnsweredNotRefused) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "declared": false})"));

        const auto applied = client.eap.ApplyInfrastructure("order-service");
        BOOST_TEST(!applied.declared);
        BOOST_TEST(applied.created.empty());
        BOOST_TEST(applied.deleted.empty());
        BOOST_TEST(applied.granted.empty());
        BOOST_TEST(applied.revoked.empty());
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

BOOST_AUTO_TEST_SUITE(EapNodeTest)

    // A node is not configured here: a worker announces itself and renews on a lease, so what
    // comes back is a snapshot of what each last said about itself.
    BOOST_AUTO_TEST_CASE(ListsTheNodesWithoutAskingWhatTheyRun) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"nodes", boost::json::array{
                                  boost::json::object{{"name", "worker-1"}, {"address", "10.0.0.11"},
                                                      {"principal", "node-worker-1"},
                                                      {"labels", boost::json::object{{"zone", "a"}}},
                                                      {"cpuCount", 8}, {"loadAverage", 1.75},
                                                      {"version", "1.4.0"}, {"os", "linux"}, {"arch", "x86_64"},
                                                      {"drained", false}, {"live", true},
                                                      {"lastSeen", "2026-10-15T08:00:00Z"}},
                                  boost::json::object{{"name", "worker-2"}, {"drained", true}, {"live", true}}}},
                {"total", 2}});
        const EapClient client(Test::Answering(listed));

        const auto nodes = client.eap.ListNodes();

        BOOST_REQUIRE(nodes.size() == 2U);
        BOOST_TEST(nodes[0].name == "worker-1");
        BOOST_TEST(nodes[0].cpuCount == 8L);
        BOOST_TEST(nodes[0].labels.at("zone") == "a");
        BOOST_TEST(nodes[0].live);
        BOOST_TEST(!nodes[0].drained);
        // A listing does not ask what a node is running - that is the one thing not on its record.
        BOOST_TEST(nodes[0].applications.empty());
        // Draining and being live are independent: a drained node keeps what it runs and keeps
        // renewing, and is only refused new placements.
        BOOST_TEST(nodes[1].drained);
        BOOST_TEST(nodes[1].live);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "list-nodes");
    }

    // A load average of 1.75 is a real reading; reading it as a long would report it as one, and
    // a reading of 0.5 as nothing at all.
    BOOST_AUTO_TEST_CASE(ReadsTheLoadAverageAsAReal) {
        const EapClient client(Test::Answering(R"({"nodes": [{"name": "worker-1", "loadAverage": 0.5}], "total": 1})"));

        BOOST_TEST(client.eap.ListNodes()[0].loadAverage == 0.5, boost::test_tools::tolerance(0.0001));
    }

    BOOST_AUTO_TEST_CASE(DescribesOneNodeWithWhatItIsHoldingSlotsFor) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"name", "worker-1"},
                {"live", true},
                {"applications", boost::json::array{boost::json::object{
                                         {"applicationId", "order-service"},
                                         {"runtimeName", "order-service-a1b2"},
                                         {"namespace", "production"},
                                         {"runtime", "BINARY"},
                                         {"instances", 4},
                                         {"running", 0}}}}});
        const EapClient client(Test::Answering(answer));

        const auto node = client.eap.GetNode("worker-1");

        BOOST_REQUIRE(node.applications.size() == 1U);
        BOOST_TEST(node.applications[0].applicationId == "order-service");
        BOOST_TEST(node.applications[0].nameSpace == "production");
        // Slots held and slots serving, both - a node holding four and running none is the state
        // worth seeing, and one count cannot say it.
        BOOST_TEST(node.applications[0].instances == 4L);
        BOOST_TEST(node.applications[0].running == 0L);
        BOOST_TEST(lastBody(client.gateway).at("node").as_string() == "worker-1");
    }

    BOOST_AUTO_TEST_CASE(DeletesANodesRegistration) {
        const EapClient client(Test::Answering(R"({"node": "worker-1", "deleted": true})"));

        client.eap.DeleteNode("worker-1");

        BOOST_TEST(lastBody(client.gateway).at("node").as_string() == "worker-1");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "delete-node");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EapLimitsTest)

    // Absent means "leave that ceiling alone" and zero removes it, so only what was asked for
    // goes on the wire - and zero has to travel.
    BOOST_AUTO_TEST_CASE(SendsOnlyTheCeilingItWasGiven) {
        const EapClient client(Test::Answering(boost::json::serialize(application())));

        std::ignore = client.eap.SetApplicationLimits("order-service", {.memoryLimitMb = 512});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("memoryLimitMb").as_int64() == 512);
        BOOST_TEST(!body.as_object().contains("cpuLimit"));
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "set-application-limits");

        std::ignore = client.eap.SetApplicationLimits("order-service", {.cpuLimit = 0.0});
        BOOST_TEST(lastBody(client.gateway).at("cpuLimit").as_double() == 0.0, boost::test_tools::tolerance(0.001));
    }

    BOOST_AUTO_TEST_CASE(RefusesAChangeThatNamesNeitherCeiling) {
        const EapClient client(Test::Answering("{}"));

        BOOST_CHECK_THROW(std::ignore = client.eap.SetApplicationLimits("order-service", {}), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

    // Nothing has moved when this is answered: where each instance ends up is the manager's to
    // decide over its next passes.
    BOOST_AUTO_TEST_CASE(RebalancingAsksRatherThanMoves) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "rebalancing": true, "instances": 4})"));

        const auto result = client.eap.RebalanceApplication("order-service");
        BOOST_TEST(result.rebalancing);
        BOOST_TEST(result.instances == 4L);
    }

    // A job's instance is a run of it, and moving one would start it again.
    BOOST_AUTO_TEST_CASE(CarriesTheRefusalForAJob) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(400, R"({"error": "it is a JOB, and moving a run would start it again"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EAP::Eap eap(session);

        try {
            std::ignore = eap.RebalanceApplication("nightly-report");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Status() == 400);
            BOOST_TEST(ex.Action() == "rebalance-application");
        }
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EapInfrastructureTest)

    // The same engine and the same answer as an apply, with dryRun true and nothing changed - and
    // the lists include what a full reconcile would delete, which is the point of running it.
    BOOST_AUTO_TEST_CASE(PlanningSaysWhatWouldHappenWithoutDoingIt) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "declared": true,
                                                   "dryRun": true,
                                                   "created": ["ern:queue/new"],
                                                   "adopted": ["ern:bucket/existing"],
                                                   "updated": [{"ern": "ern:queue/orders", "changes": ["retention"]}],
                                                   "deleted": ["ern:queue/gone"],
                                                   "kept": [{"ern": "ern:queue/theirs", "reason": "owned by another application"}],
                                                   "granted": [], "revoked": []})"));

        const auto plan = client.eap.PlanInfrastructure("order-service");

        BOOST_TEST(plan.dryRun);
        BOOST_REQUIRE(plan.deleted.size() == 1U);
        // Adoption is not creation, and separating them is what makes an apply over existing
        // resources readable.
        BOOST_REQUIRE(plan.adopted.size() == 1U);
        BOOST_REQUIRE(plan.updated.size() == 1U);
        BOOST_TEST(plan.updated[0].changes[0] == "retention");
        BOOST_REQUIRE(plan.kept.size() == 1U);
        BOOST_TEST(plan.kept[0].reason == "owned by another application");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "plan-infrastructure");
    }

    BOOST_AUTO_TEST_CASE(AppliesAPlatformDeclaration) {
        const EapClient client(Test::Answering(R"({"platform": "shared", "declared": true, "dryRun": false,
                                                   "created": ["ern:topic/events"]})"));

        const boost::json::object declaration{{"topics", boost::json::array{"events"}}};
        const auto applied = client.eap.ApplyPlatform("shared", declaration, true);

        BOOST_TEST(applied.platform == "shared");
        BOOST_TEST(applied.applicationId.empty());
        BOOST_TEST(!applied.dryRun);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "shared");
        BOOST_TEST(body.at("prune").as_bool() == true);
        BOOST_REQUIRE(body.as_object().contains("declaration"));
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "apply-platform");
    }

    BOOST_AUTO_TEST_CASE(ReadsAPlatformRecord) {
        const EapClient client(Test::Answering(R"({"name": "platform", "namespace": "production",
                                                   "owns": ["ern:topic/events", "ern:bucket/shared"],
                                                   "declaration": {"topics": ["events"]},
                                                   "modified": "2026-11-06T08:00:00Z"})"));

        const auto platform = client.eap.GetPlatform();

        BOOST_TEST(platform.name == "platform");
        BOOST_REQUIRE(platform.owns.size() == 2U);
        BOOST_TEST(platform.declaration.is_object());
        // The default when none was named at apply time.
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == std::string(EAP::DefaultPlatform));
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EapEgressTest)

    // Several instances of one application share one budget: a limit kept in each process would be
    // multiplied by the pool size, which is how a fleet gets itself blocked.
    BOOST_AUTO_TEST_CASE(AcquiresAPermitAndCarriesTheLease) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "site": "downloads.example.com",
                                                   "granted": true, "permitId": "p-1",
                                                   "expiresAt": "2026-11-06T08:15:00Z",
                                                   "limits": {"requestsPerSecond": 5.0, "burst": 2.0, "concurrent": 4}})"));

        const auto permit = client.eap.AcquireEgressPermit("downloads.example.com");

        BOOST_TEST(permit.granted);
        BOOST_TEST(permit.permitId == "p-1");
        BOOST_TEST(permit.limits.requestsPerSecond == 5.0, boost::test_tools::tolerance(0.001));
        BOOST_TEST(permit.limits.concurrent == 4L);
        BOOST_TEST(lastBody(client.gateway).at("leaseSeconds").as_int64() == EAP::DefaultEgressLease.count());
    }

    // A refusal is an ordinary answer rather than an error: being rate limited is not a fault, so
    // "granted" is the field to branch on and nothing throws.
    BOOST_AUTO_TEST_CASE(ARefusalIsAnAnswerRatherThanAnError) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "site": "downloads.example.com",
                                                   "granted": false, "reason": "Concurrency", "retryAfterMs": 100,
                                                   "limits": {"requestsPerSecond": 5.0}})"));

        const auto permit = client.eap.AcquireEgressPermit("downloads.example.com");

        BOOST_TEST(!permit.granted);
        BOOST_TEST(permit.reason == "Concurrency");
        BOOST_TEST(permit.retryAfterMs == 100L);
        BOOST_TEST(permit.permitId.empty());
    }

    BOOST_AUTO_TEST_CASE(ReleasesAPermitAndReportsAThrottle) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "site": "downloads.example.com",
                                                   "released": true})"));

        BOOST_TEST(client.eap.ReleaseEgressPermit("downloads.example.com", "p-1").released);
        BOOST_TEST(lastBody(client.gateway).at("permitId").as_string() == "p-1");

        const EapClient reporter(Test::Answering(R"({"site": "downloads.example.com",
                                                     "blockedUntil": "2026-11-06T08:05:00Z"})"));
        // Telling euclid turns one instance's refusal into the whole pool backing off.
        BOOST_TEST(reporter.eap.ReportEgressThrottled("downloads.example.com", 120).blockedUntil
                   == "2026-11-06T08:05:00Z");
        BOOST_TEST(lastBody(reporter.gateway).at("retryAfterSeconds").as_int64() == 120);
    }

    // An absent applicationId is what asks about the calling application itself, which is what an
    // application setting its own limits means.
    BOOST_AUTO_TEST_CASE(LeavesTheApplicationOutWhenItIsTheCallersOwn) {
        const EapClient client(Test::Answering(R"({"applicationId": "order-service", "site": "*",
                                                   "limits": {"requestsPerSecond": 10.0, "burst": 1.0, "concurrent": 2},
                                                   "limitsSet": true})"));

        const auto site = client.eap.SetEgressLimit("*", {.requestsPerSecond = 10.0, .burst = 1.0, .concurrent = 2});

        BOOST_TEST(site.limitsSet);
        BOOST_TEST(!lastBody(client.gateway).as_object().contains("applicationId"));

        std::ignore = client.eap.SetEgressLimit("*", {.requestsPerSecond = 10.0}, "other-service");
        BOOST_TEST(lastBody(client.gateway).at("applicationId").as_string() == "other-service");
    }

    // A site showing the default has no limit of its own, which is what limitsSet is for - and
    // deleting the limit of such a site answers 404.
    BOOST_AUTO_TEST_CASE(SaysWhetherASitesLimitsAreItsOwn) {
        const EapClient client(Test::Answering(R"({"defaultLimits": {"requestsPerSecond": 1.0},
                                                   "sites": [{"site": "a.example.com", "limitsSet": false,
                                                              "limits": {"requestsPerSecond": 1.0}},
                                                             {"site": "b.example.com", "limitsSet": true,
                                                              "limits": {"requestsPerSecond": 20.0}}]})"));

        const auto sites = client.eap.ListEgressSites();

        BOOST_TEST(sites.defaultLimits.requestsPerSecond == 1.0, boost::test_tools::tolerance(0.001));
        BOOST_REQUIRE(sites.sites.size() == 2U);
        BOOST_TEST(!sites.sites[0].limitsSet);
        BOOST_TEST(sites.sites[1].limitsSet);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EapNodeLeaseTest)

    BOOST_AUTO_TEST_CASE(RegistersANodeAndTakesALease) {
        const EapClient client(Test::Answering(R"({"node": {"name": "worker-1", "principal": "node-worker-1",
                                                            "live": true}, "leaseSeconds": 60})"));

        const auto registered = client.eap.RegisterNode("worker-1", {.address = "10.0.0.11",
                                                                     .version = "1.4.0",
                                                                     .os = "linux",
                                                                     .arch = "x86_64",
                                                                     .labels = {{"zone", "a"}}});

        BOOST_TEST(registered.node.name == "worker-1");
        BOOST_TEST(registered.leaseSeconds == 60L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("node").as_string() == "worker-1");
        BOOST_TEST(body.at("arch").as_string() == "x86_64");
        BOOST_TEST(body.at("labels").at("zone").as_string() == "a");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "register-node");
    }

    // The heartbeat carries the machine's readings and comes back with the whole set of instances
    // this node should run - not what changed, so a worker reconciles against it.
    BOOST_AUTO_TEST_CASE(RenewingReturnsTheWholeAssignedSet) {
        const EapClient client(Test::Answering(R"({"node": "worker-1", "leaseSeconds": 60,
                                                   "leaseExpiresAt": "2026-11-06T08:01:00Z",
                                                   "instances": [{"instanceId": "i-1", "applicationId": "order-service",
                                                                  "runtimeName": "order-service-a1b2",
                                                                  "revision": "2026-11-05T08:00:00Z",
                                                                  "memoryLimitMb": 512, "cpuLimit": 1.5,
                                                                  "nameSpace": "production",
                                                                  "environment": {"EUCLID_LOG_LEVEL": "info"}}]})"));

        const auto renewal = client.eap.RenewNode("worker-1", {.loadAverage = 1.5, .memoryTotalMb = 16384,
                                                               .memoryUsedMb = 8192});

        BOOST_REQUIRE(renewal.instances.size() == 1U);
        BOOST_TEST(renewal.instances[0].instanceId == "i-1");
        // What says an instance has to be replaced rather than left alone.
        BOOST_TEST(renewal.instances[0].revision == "2026-11-05T08:00:00Z");
        BOOST_TEST(renewal.instances[0].cpuLimit == 1.5, boost::test_tools::tolerance(0.001));
        BOOST_TEST(renewal.instances[0].nameSpace == "production");
        BOOST_TEST(renewal.instances[0].environment.at("EUCLID_LOG_LEVEL") == "info");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("loadAverage").as_double() == 1.5, boost::test_tools::tolerance(0.001));
        BOOST_TEST(body.at("memoryTotalMb").as_int64() == 16384);
    }

    // A worker that measured nothing sends nothing: the server reads an absent figure as a worker
    // older than the field, which is what zero would mean anyway.
    BOOST_AUTO_TEST_CASE(AHeartbeatWithNoReadingsSendsNoMemory) {
        const EapClient client(Test::Answering(R"({"node": "worker-1", "leaseSeconds": 60, "instances": []})"));

        std::ignore = client.eap.RenewNode("worker-1");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(!body.as_object().contains("memoryTotalMb"));
        BOOST_TEST(!body.as_object().contains("memoryUsedMb"));
        BOOST_TEST(body.at("loadAverage").as_double() == 0.0, boost::test_tools::tolerance(0.001));
    }

    // Absent means drain, because that is what the action is called - so putting a node back has
    // to say so, and the field travels either way.
    BOOST_AUTO_TEST_CASE(DrainingAndUndrainingBothSayWhichTheyMean) {
        const EapClient client(Test::Answering(R"({"node": "worker-1", "drained": true})"));

        BOOST_TEST(client.eap.DrainNode("worker-1").drained);
        BOOST_TEST(lastBody(client.gateway).at("drained").as_bool() == true);

        std::ignore = client.eap.DrainNode("worker-1", false);
        BOOST_TEST(lastBody(client.gateway).at("drained").as_bool() == false);
    }

    // Empty is what takes a slot back to the manager's own host - a legitimate thing to ask for,
    // and not the same as not saying - so the node travels even when empty.
    BOOST_AUTO_TEST_CASE(AssigningASlotSendsTheNodeEvenWhenEmpty) {
        const EapClient client(Test::Answering(R"({"runtimeName": "order-service-a1b2", "instanceId": "i-1",
                                                   "node": "", "leaseExpiresAt": "2026-11-06T08:15:00Z"})"));

        const auto assigned = client.eap.AssignInstance("order-service-a1b2", "i-1");

        BOOST_TEST(assigned.instanceId == "i-1");
        BOOST_TEST(assigned.node.empty());
        BOOST_REQUIRE(lastBody(client.gateway).as_object().contains("node"));
        BOOST_TEST(lastBody(client.gateway).at("node").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(ReportsAnInstanceAndLeavesTheStateToTheServerWhenUnset) {
        const EapClient client(Test::Answering("{}"));

        client.eap.ReportNodeInstance("order-service-a1b2", "i-1", {.httpPort = 34001, .pid = 4711});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("httpPort").as_int64() == 34001);
        // An absent state reads as RUNNING; an empty string would parse as no state at all.
        BOOST_TEST(!body.as_object().contains("state"));

        client.eap.ReportNodeInstance("order-service-a1b2", "i-1", {.state = "STOPPED"});
        BOOST_TEST(lastBody(client.gateway).at("state").as_string() == "STOPPED");
    }

    // A credentials bundle whose shape belongs to whatever consumes it, so it is answered unparsed.
    BOOST_AUTO_TEST_CASE(IssuesInstanceCredentialsUnparsed) {
        const EapClient client(Test::Answering(R"({"accessKeyId": "AKIA", "secretAccessKey": "s3cret",
                                                   "token": "jwt"})"));

        const auto credentials = client.eap.IssueInstanceCredentials("order-service-a1b2", "i-1");
        BOOST_TEST(credentials.at("accessKeyId").as_string() == "AKIA");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "issue-instance-credentials");
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
