// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EES, end to end against a fake euclid server.
 *
 * @par
 * The subscriber name is the identity on every call here, and the claim-then-acknowledge cycle is
 * what a subscriber gets wrong: an event is invisible rather than gone until it is acknowledged, so
 * what these check is that the wire carries the name, the ids and the timings that cycle depends on -
 * and that a long poll asked for longer than the server's cap is asked for again rather than
 * answered short.
 */

// C++ includes
#include <chrono>
#include <string>
#include <tuple>

// Boost includes
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/ees/Ees.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    /**
     * @brief A gateway, a logged-in session and an EES client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EesClient {

        explicit EesClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), ees(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EES::Ees ees;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief One subscription, as the server describes it.
     */
    boost::json::object subscription(const std::string &eventType = "esm.object.created") {
        return {
                {"subscriber", "indexer"},
                {"eventType", eventType},
                {"filter", boost::json::object{{"bucket", "reports"}}},
                {"accountId", "000000000000"},
                {"mode", "durable"},
                {"created", "2026-10-01T08:00:00Z"},
                {"lastSeen", "2026-10-15T08:00:00Z"},
        };
    }

    /**
     * @brief One event, as the server delivers it.
     */
    boost::json::object event(const std::string &eventId = "ev-1", const long attempts = 1) {
        return {
                {"eventId", eventId},
                {"eventType", "esm.object.created"},
                {"sourceModule", "esm"},
                {"payload", boost::json::object{{"key", "2026/q3.pdf"}, {"size", 2048}}},
                {"attempts", attempts},
                {"created", "2026-10-15T08:00:00Z"},
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EesSubscriptionTest)

    BOOST_AUTO_TEST_CASE(SubscribesAndReadsTheSubscriptionsBack) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"subscriptions", boost::json::array{subscription(), subscription("esm.object.deleted")}}});
        const EesClient client(Test::Answering(answer));

        const auto subscribed = client.ees.Subscribe("indexer",
                                                     {std::string(EES::ObjectCreated), std::string(EES::ObjectDeleted)},
                                                     {.filter = {{"bucket", "reports"}}});

        BOOST_REQUIRE(subscribed.subscriptions.size() == 2U);
        BOOST_TEST(subscribed.subscriptions[0].subscriber == "indexer");
        BOOST_TEST(subscribed.subscriptions[0].eventType == "esm.object.created");
        BOOST_TEST(subscribed.subscriptions[0].mode == std::string(EES::ModeDurable));
        BOOST_TEST(subscribed.subscriptions[0].filter.at("bucket").as_string() == "reports");

        const auto body = lastBody(client.gateway);
        // "name" on the wire, and the subscriber everywhere else in this module.
        BOOST_TEST(body.at("name").as_string() == "indexer");
        BOOST_TEST(body.at("eventTypes").as_array().size() == 2U);
        BOOST_TEST(body.at("filter").at("bucket").as_string() == "reports");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "subscribe-events");
    }

    // The server reads an absent mode as durable and refuses one it cannot parse, so an empty
    // string sent along would be a 400 rather than the default a caller meant to accept.
    BOOST_AUTO_TEST_CASE(LeavesTheModeOutRatherThanSendingItEmpty) {
        const EesClient client(Test::Answering(R"({"subscriptions": []})"));

        std::ignore = client.ees.Subscribe("indexer", {std::string(EES::ObjectCreated)});
        BOOST_TEST(!lastBody(client.gateway).as_object().contains("mode"));

        std::ignore = client.ees.Subscribe("watcher", {std::string(EES::ObjectCreated)},
                                           {.mode = std::string(EES::ModeLive)});
        BOOST_TEST(lastBody(client.gateway).at("mode").as_string() == "live");
    }

    // A subscription to nothing is a subscriber that waits forever for events it never asked for.
    BOOST_AUTO_TEST_CASE(RefusesASubscriptionThatNamesNoEventType) {
        const EesClient client(Test::Answering(R"({"subscriptions": []})"));

        BOOST_CHECK_THROW(std::ignore = client.ees.Subscribe("indexer", {}), EuclidError);
        // Nothing went out: the login, and nothing else.
        BOOST_TEST(client.gateway.Received().size() == 1U);
    }

    BOOST_AUTO_TEST_CASE(ListsWhatASubscriberWantsAndWhatIsWaiting) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"subscriptions", boost::json::array{subscription()}}, {"waiting", 17}});
        const EesClient client(Test::Answering(answer));

        const auto listed = client.ees.ListSubscriptions("indexer");

        BOOST_REQUIRE(listed.subscriptions.size() == 1U);
        // The backlog, which is the figure worth watching - one that only grows is a subscriber
        // not keeping up with what it asked for.
        BOOST_TEST(listed.waiting == 17L);
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "indexer");
    }

    // Naming no event type removes the subscriber entirely, which is what something being
    // decommissioned wants - so the field is sent empty rather than left out.
    BOOST_AUTO_TEST_CASE(UnsubscribingEverythingSendsAnEmptyEventType) {
        const EesClient client(Test::Answering(R"({"subscriber": "indexer", "removed": 3})"));

        const auto removed = client.ees.Unsubscribe("indexer");

        BOOST_TEST(removed.removed == 3L);
        BOOST_REQUIRE(lastBody(client.gateway).as_object().contains("eventType"));
        BOOST_TEST(lastBody(client.gateway).at("eventType").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(UnsubscribingOneTypeNamesIt) {
        const EesClient client(Test::Answering(R"({"subscriber": "indexer", "removed": 1})"));

        std::ignore = client.ees.Unsubscribe("indexer", std::string(EES::ObjectDeleted));
        BOOST_TEST(lastBody(client.gateway).at("eventType").as_string() == "esm.object.deleted");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EesEventTest)

    BOOST_AUTO_TEST_CASE(ReceivesEventsAndLeavesThePayloadAlone) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"events", boost::json::array{event(), event("ev-2")}}, {"total", 2}});
        const EesClient client(Test::Answering(answer));

        const auto received = client.ees.ReceiveEvents("indexer", {.maxEvents = 25});

        BOOST_TEST(received.total == 2L);
        BOOST_REQUIRE(received.events.size() == 2U);
        BOOST_TEST(received.events[0].eventId == "ev-1");
        BOOST_TEST(received.events[0].sourceModule == "esm");
        // The payload's shape belongs to whichever module published it, so it is handed over as it
        // came rather than parsed into anything.
        BOOST_TEST(received.events[0].payload.at("key").as_string() == "2026/q3.pdf");
        BOOST_TEST(received.events[0].attempts == 1L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "indexer");
        BOOST_TEST(body.at("maxEvents").as_int64() == 25);
        BOOST_TEST(body.at("waitTime").as_int64() == 0);
        BOOST_TEST(body.at("visibilityTimeout").as_int64() == EES::DefaultVisibilityTimeout.count());
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "receive-events");
    }

    // Above one means a previous delivery was claimed and never acknowledged, so the visibility
    // timeout brought it back - which is what anything whose handling is not idempotent reads.
    BOOST_AUTO_TEST_CASE(ReportsHowManyTimesDeliveryHasBeenAttempted) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"events", boost::json::array{event("ev-1", 3)}}, {"total", 1}});
        const EesClient client(Test::Answering(answer));

        BOOST_TEST(client.ees.ReceiveEvents("indexer").events[0].attempts == 3L);
    }

    // A receive that came back empty before the caller's deadline was answered early - the server
    // had no long-poll slot free - and the wait the caller asked for is theirs, so it is asked for
    // again rather than given up on. One second of deadline rather than a realistic one: the point
    // is that it loops, and a test that proved it with half a minute of sleeping would cost the
    // whole suite more than it is worth.
    BOOST_AUTO_TEST_CASE(AReceiveAnsweredEarlyIsAskedAgain) {
        const EesClient client(Test::Answering(R"({"events": [], "total": 0})"));

        const auto received = client.ees.ReceiveEvents("indexer", {.waitTime = std::chrono::seconds(1)});

        BOOST_TEST(received.events.empty());
        BOOST_TEST(client.gateway.Received().size() >= 3U);// the login, and two receives at least
    }

    // One request cannot wait longer than the server's cap however long it asks for, so the figure
    // on the wire is the cap rather than what the caller asked for - and the loop above is what
    // makes up the difference. Checked on a receive that finds something, so proving the clamp
    // costs one round trip instead of the caller's whole deadline.
    BOOST_AUTO_TEST_CASE(NeverAsksOneRequestToWaitLongerThanTheServerWill) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"events", boost::json::array{event()}}, {"total", 1}});
        const EesClient client(Test::Answering(answer));

        std::ignore = client.ees.ReceiveEvents("indexer", {.waitTime = std::chrono::seconds(35)});

        BOOST_TEST(lastBody(client.gateway).at("waitTime").as_int64() == EES::MaxWaitTime.count());
        BOOST_TEST(client.gateway.Received().size() == 2U);// the login, and one receive
    }

    BOOST_AUTO_TEST_CASE(AcknowledgesEventsAndSaysWhatIsLeft) {
        const EesClient client(Test::Answering(R"({"subscriber": "indexer", "acknowledged": 2, "waiting": 5})"));

        const auto acknowledged = client.ees.AckEvents("indexer", {"ev-1", "ev-2"});

        BOOST_TEST(acknowledged.acknowledged == 2L);
        BOOST_TEST(acknowledged.waiting == 5L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "indexer");
        BOOST_REQUIRE(body.at("eventIds").as_array().size() == 2U);
        BOOST_TEST(body.at("eventIds").as_array()[0].as_string() == "ev-1");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "ack-events");
    }

    // Fewer acknowledged than were sent is not an error: one already acknowledged, or one whose
    // retention ran out, is not there to acknowledge again and means the same thing to a caller.
    BOOST_AUTO_TEST_CASE(FewerAcknowledgedThanAskedForIsNotAFailure) {
        const EesClient client(Test::Answering(R"({"subscriber": "indexer", "acknowledged": 1, "waiting": 0})"));

        BOOST_TEST(client.ees.AckEvents("indexer", {"ev-1", "ev-gone"}).acknowledged == 1L);
    }

    BOOST_AUTO_TEST_CASE(RefusesAnAcknowledgementOfNothing) {
        const EesClient client(Test::Answering("{}"));

        BOOST_CHECK_THROW(std::ignore = client.ees.AckEvents("indexer", {}), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EesTransportTest)

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EesClient client(Test::Answering(R"({"subscriptions": []})"));

        client.session.ChangeNamespace("integration");
        std::ignore = client.ees.ListSubscriptions("indexer");

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "ees");
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "integration");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

    BOOST_AUTO_TEST_CASE(CarriesTheServersReasonWhenASubscriptionIsRefused) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(400, R"({"error": "mode must be \"durable\" or \"live\""})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EES::Ees ees(session);

        try {
            std::ignore = ees.Subscribe("indexer", {std::string(EES::ObjectCreated)}, {.mode = "sometimes"});
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "ees");
            BOOST_TEST(ex.Action() == "subscribe-events");
            BOOST_TEST(ex.Status() == 400);
        }
    }

    BOOST_AUTO_TEST_CASE(ReachesItsMetricsAndAnActionThisSdkDoesNotWrap) {
        const EesClient client(Test::Answering(R"({"ees_subscription_count": 4, "whatever": 42})"));

        BOOST_TEST(client.ees.Metrics().at("ees_subscription_count").as_int64() == 4);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");

        BOOST_TEST(client.ees.Call("some-future-action").at("whatever").as_int64() == 42);
    }

BOOST_AUTO_TEST_SUITE_END()
