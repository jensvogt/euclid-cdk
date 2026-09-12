// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief ETS, end to end against a fake euclid server.
 *
 * @par
 * A transfer server is a definition rather than a socket, so what these check is what a definition
 * is written as: which fields an update sends and which it leaves alone, and that asking for a
 * server to run is a desired state rather than a running process.
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
#include <euclid/cdk/ets/Ets.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    /**
     * @brief A gateway, a logged-in session and an ETS client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EtsClient {

        explicit EtsClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), ets(session) {}

        FakeGateway gateway;
        EAM::Session session;
        ETS::Ets ets;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief One transfer server, as the server describes it.
     */
    boost::json::object server(const std::string &desiredState = "STOPPED", const std::string &state = "STOPPED") {
        return {
                {"serverId", "partner-drop"},
                {"runtimeName", "ets-000000000000-partner-drop"},
                {"ern", "ern:euclid:ets:eu-central-1:000000000000:server/partner-drop"},
                {"accountId", "000000000000"},
                // The one module whose wire spells this "namespace".
                {"namespace", "reports"},
                {"region", "eu-central-1"},
                {"protocol", "SFTP"},
                {"address", "0.0.0.0"},
                {"port", 2222},
                {"bucketName", "invoices"},
                {"bucketErn", "ern:euclid:esm:eu-central-1:000000000000:bucket/invoices"},
                {"homeDirectory", "incoming/"},
                {"userIds", boost::json::array{"jens"}},
                {"userGroups", boost::json::array{"partners"}},
                {"directories", boost::json::array{"incoming/", "archive/"}},
                {"desiredState", desiredState},
                {"state", state},
                {"hostKey", ""},
                {"pasvMin", 6000},
                {"pasvMax", 6100},
                {"created", "2026-09-01T08:00:00Z"},
                {"modified", "2026-09-01T08:00:00Z"},
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EtsDefinitionTest)

    BOOST_AUTO_TEST_CASE(DefinesAServerStoppedAndReadsItBack) {
        const EtsClient client(Test::Answering(boost::json::serialize(server())));

        const auto defined = client.ets.CreateServer("partner-drop", "invoices", 2222,
                                                     {.homeDirectory = "incoming/",
                                                      .userGroups = {"partners"},
                                                      .directories = {"incoming/", "archive/"}});

        BOOST_TEST(defined.serverId == "partner-drop");
        BOOST_TEST(defined.nameSpace == "reports");
        BOOST_TEST(defined.runtimeName == "ets-000000000000-partner-drop");
        BOOST_TEST(defined.bucketName == "invoices");
        BOOST_TEST(defined.bucketErn == "ern:euclid:esm:eu-central-1:000000000000:bucket/invoices");
        BOOST_REQUIRE(defined.directories.size() == 2U);
        BOOST_TEST(defined.directories[1] == "archive/");
        // Nothing listens yet: a new server's desired state is stopped.
        BOOST_TEST(defined.desiredState == std::string(ETS::StateStopped));
        BOOST_TEST(!defined.IsRunning());

        // The defaults travel rather than being left to the server to guess at.
        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("protocol").as_string() == std::string(ETS::Sftp));
        BOOST_TEST(body.at("address").as_string() == std::string(ETS::EveryInterface));
        BOOST_TEST(body.at("port").as_int64() == 2222);
        BOOST_TEST(body.at("bucket").as_string() == "invoices");
        BOOST_TEST(body.at("pasvMin").as_int64() == ETS::DefaultPasvMin);
        BOOST_TEST(body.at("pasvMax").as_int64() == ETS::DefaultPasvMax);
        BOOST_TEST(body.at("userGroups").as_array().size() == 1U);
        BOOST_TEST(body.at("userIds").as_array().empty());
    }

    BOOST_AUTO_TEST_CASE(CreatesAnFtpServerWhenAskedTo) {
        const EtsClient client(Test::Answering(boost::json::serialize(server())));

        std::ignore = client.ets.CreateServer("partner-drop", "invoices", 2121,
                                              {.protocol = std::string(ETS::Ftp), .pasvMin = 7000, .pasvMax = 7100});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("protocol").as_string() == "FTP");
        BOOST_TEST(body.at("pasvMin").as_int64() == 7000);
        BOOST_TEST(body.at("pasvMax").as_int64() == 7100);
    }

    // The server distinguishes a field being sent from one that is not, rather than one value from
    // another - so an update says only what it means to change.
    BOOST_AUTO_TEST_CASE(SendsOnlyWhatAnUpdateNames) {
        const EtsClient client(Test::Answering(boost::json::serialize(server())));

        std::ignore = client.ets.UpdateServer("partner-drop", {.port = 2223, .userGroups = {{"partners", "auditors"}}});

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("serverId").as_string() == "partner-drop");
        BOOST_TEST(body.at("port").as_int64() == 2223);
        BOOST_TEST(body.at("userGroups").as_array().size() == 2U);
        // Everything nobody named stays as it was stored.
        BOOST_TEST(!body.contains("address"));
        BOOST_TEST(!body.contains("bucket"));
        BOOST_TEST(!body.contains("homeDirectory"));
        BOOST_TEST(!body.contains("userIds"));
        BOOST_TEST(!body.contains("directories"));
        BOOST_TEST(!body.contains("hostKey"));
        BOOST_TEST(!body.contains("pasvMin"));
        BOOST_TEST(!body.contains("protocol"));
    }

    // An empty string is a value: it puts logins back at the root of the bucket.
    BOOST_AUTO_TEST_CASE(SendsAnEmptyValueThatWasAskedFor) {
        const EtsClient client(Test::Answering(boost::json::serialize(server())));

        std::ignore = client.ets.UpdateServer("partner-drop", {.homeDirectory = "", .directories = std::vector<std::string>{}});

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.contains("homeDirectory"));
        BOOST_TEST(body.at("homeDirectory").as_string() == "");
        BOOST_TEST(body.at("directories").as_array().empty());
    }

    // Every match and no total: a deployment has as many transfer servers as it has ports to spare.
    BOOST_AUTO_TEST_CASE(ListsServersByPrefix) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"servers", boost::json::array{server(), server("RUNNING", "RUNNING")}}});

        const EtsClient client(Test::Answering(listed));

        const auto servers = client.ets.ListServers("partner");
        BOOST_REQUIRE(servers.size() == 2U);
        BOOST_TEST(servers[0].serverId == "partner-drop");
        BOOST_TEST(!servers[0].IsRunning());
        BOOST_TEST(servers[1].IsRunning());
        BOOST_TEST(lastBody(client.gateway).at("prefix").as_string() == "partner");
    }

    BOOST_AUTO_TEST_CASE(DeletesTheDefinitionAndNotTheBucket) {
        const EtsClient client(Test::Answering(R"({"serverId": "partner-drop", "deleted": true})"));

        client.ets.DeleteServer("partner-drop");
        BOOST_TEST(lastBody(client.gateway).at("serverId").as_string() == "partner-drop");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "delete-server");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EtsRunningTest)

    // Asking is all a start does: the manager is what turns a desired state into a process.
    BOOST_AUTO_TEST_CASE(StartingAServerAsksForItRatherThanWaitingForIt) {
        const EtsClient client(Test::Answering(boost::json::serialize(server("RUNNING", "STOPPED"))));

        const auto started = client.ets.StartServer("partner-drop");
        BOOST_TEST(started.desiredState == std::string(ETS::StateRunning));
        // What is observed has not caught up yet, and that is not an error.
        BOOST_TEST(started.state == std::string(ETS::StateStopped));
        BOOST_TEST(!started.IsRunning());
        BOOST_TEST(lastBody(client.gateway).at("serverId").as_string() == "partner-drop");
    }

    BOOST_AUTO_TEST_CASE(ReadsTheStateAServerIsObservedIn) {
        const EtsClient client(Test::Answering(boost::json::serialize(server("RUNNING", "RUNNING"))));

        const auto observed = client.ets.GetServer("partner-drop");
        BOOST_TEST(observed.IsRunning());
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-server");
    }

    BOOST_AUTO_TEST_CASE(StopsAServer) {
        const EtsClient client(Test::Answering(boost::json::serialize(server("STOPPED", "RUNNING"))));

        const auto stopped = client.ets.StopServer("partner-drop");
        BOOST_TEST(stopped.desiredState == std::string(ETS::StateStopped));
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "stop-server");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EtsTransportTest)

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EtsClient client(Test::Answering(boost::json::serialize(server())));

        client.session.ChangeNamespace("reports");
        std::ignore = client.ets.GetServer("partner-drop");

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "ets");
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "reports");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

    // Every action here is administrator-only, and the refusal says so.
    BOOST_AUTO_TEST_CASE(CarriesTheServersReasonWhenTheCallerIsNotAnAdministrator) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(403, R"({"error": "Administrator rights required"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const ETS::Ets ets(session);

        try {
            std::ignore = ets.CreateServer("partner-drop", "invoices", 2222);
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "ets");
            BOOST_TEST(ex.Action() == "create-server");
            BOOST_TEST(ex.Status() == 403);
            BOOST_TEST(ex.Reason() == "Administrator rights required");
        }
    }

    BOOST_AUTO_TEST_CASE(ReachesItsMetricsAndAnActionThisSdkDoesNotWrap) {
        const EtsClient client(Test::Answering(R"({"ets_server_count": 2, "whatever": 42})"));

        BOOST_TEST(client.ets.Metrics().at("ets_server_count").as_int64() == 2);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");

        BOOST_TEST(client.ets.Call("some-future-action").at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

BOOST_AUTO_TEST_SUITE_END()
