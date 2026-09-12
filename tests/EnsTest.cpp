// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief ENS, end to end against a fake euclid server.
 *
 * @par
 * The same treatment EQS gets, minus the lease: a topic hands each message to every subscriber and
 * keeps it as a record of having done so, so what is worth checking here is what is published, what
 * is held while a topic is stopped, and where a subscription sends it.
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
#include <euclid/cdk/ens/Ens.h>
#include <euclid/cdk/esm/Esm.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    const std::string kTopic = "ern:euclid:ens:eu-central-1:000000000000:topic/order-events";
    const std::string kQueue = "ern:euclid:eqs:eu-central-1:000000000000:queue/orders";

    /**
     * @brief A gateway, a logged-in session and an ENS client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EnsClient {

        explicit EnsClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), ens(session) {}

        FakeGateway gateway;
        EAM::Session session;
        ENS::Ens ens;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EnsTopicTest)

    BOOST_AUTO_TEST_CASE(CreatesAndListsTopics) {
        const auto topics = boost::json::serialize(boost::json::object{
                {"total", 2},
                {"topics", boost::json::array{
                                   boost::json::object{
                                           {"name", "order-events"},
                                           {"owner", "jens"},
                                           {"ern", kTopic},
                                           {"tags", boost::json::object{{"team", "finance"}}},
                                           {"size", 4096},
                                           {"messages", 12},
                                           {"maxMessageLength", 262144},
                                           {"status", "RUNNING"},
                                           {"retentionPeriod", 604800},
                                           {"created", "2026-01-01T00:00:00Z"}},
                                   boost::json::object{{"name", "audit"}, {"status", "STOPPED"}}}}});

        const EnsClient client(Test::AnsweringByAction({{"list-topics", topics},
                                                        {"create-topic", R"({"name": "order-events", "ern": ")" + kTopic + R"("})"}}));

        const auto created = client.ens.CreateTopic("order-events");
        BOOST_TEST(created.ern == kTopic);
        BOOST_TEST(lastBody(client.gateway).at("maxMessageLength").as_int64() == ENS::DefaultMaxMessageLength);

        const auto page = client.ens.ListTopics({.prefix = "order"});
        BOOST_TEST(page.total == 2L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[0].name == "order-events");
        BOOST_TEST(page.items[0].tags.at("team") == "finance");
        BOOST_TEST(page.items[0].messages == 12L);
        BOOST_TEST(page.items[0].retentionPeriod == 604800L);
        BOOST_TEST(page.items[0].status == std::string(ENS::TopicRunning));
        BOOST_TEST(page.items[1].status == std::string(ENS::TopicStopped));

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("prefix").as_string() == "order");
        BOOST_TEST(body.at("sortColumn").as_string() == "name");
    }

    BOOST_AUTO_TEST_CASE(AnswersWithTheErnAndTheMetadata) {
        const EnsClient client(Test::AnsweringByAction({
                {"get-topic-ern", R"({"ern": ")" + kTopic + R"("})"},
                {"get-topic-metadata", R"({"region": "eu-central-1", "accountId": "000000000000", "owner": "jens",
                                           "nameSpace": "reports", "name": "order-events", "ern": "ern:topic/order-events",
                                           "size": 4096, "messages": 12, "status": "STOPPED",
                                           "retentionPeriod": 0, "held": 5})"},
        }));

        BOOST_TEST(client.ens.GetTopicErn("order-events") == kTopic);

        const auto metadata = client.ens.GetTopicMetadata(kTopic);
        BOOST_TEST(metadata.nameSpace == "reports");
        BOOST_TEST(metadata.status == std::string(ENS::TopicStopped));
        // What starting the topic again would have to fan out.
        BOOST_TEST(metadata.held == 5L);
        BOOST_TEST(metadata.retentionPeriod == ENS::InstallationRetention);
    }

    BOOST_AUTO_TEST_CASE(PurgesAndDeletesATopicAndTagsIt) {
        const EnsClient client(Test::Answering());

        client.ens.AddTopicTag(kTopic, "team", "finance");
        BOOST_TEST(lastBody(client.gateway).at("value").as_string() == "finance");
        client.ens.SetTopicTag(kTopic, "team", "treasury");
        BOOST_TEST(lastBody(client.gateway).at("value").as_string() == "treasury");
        client.ens.DeleteTopicTag(kTopic, "team");
        BOOST_TEST(lastBody(client.gateway).at("key").as_string() == "team");

        client.ens.PurgeTopic(kTopic);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "purge-topic");

        client.ens.DeleteTopic(kTopic);
        BOOST_TEST(lastBody(client.gateway).at("ern").as_string() == kTopic);
    }

    // The opposite default to EQS: a session scoped to a namespace cannot empty another's topics.
    BOOST_AUTO_TEST_CASE(FollowsTheSessionsNamespaceOnABlanketPurge) {
        EnsClient client(Test::Answering());
        client.session.ChangeNamespace("reports");

        client.ens.PurgeAllTopics();
        auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("region").as_string() == "eu-central-1");
        BOOST_TEST(body.at("accountId").as_string() == "000000000000");
        BOOST_TEST(body.at("nameSpace").as_string() == "reports");

        // An empty namespace is a value here rather than "unspecified", so asking for every
        // namespace of the account has to be possible even from a scoped session.
        client.ens.PurgeAllTopics({.nameSpace = std::string(ENS::EveryNamespace), .nameSpaceSet = true});
        body = lastBody(client.gateway);
        BOOST_TEST(body.at("nameSpace").as_string() == "");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EnsTopicStateTest)

    // Stopping holds delivery without refusing publishers, which is what makes it safe.
    BOOST_AUTO_TEST_CASE(HoldsDeliveryWithoutRefusingPublishers) {
        const EnsClient client(Test::AnsweringByAction({
                {"stop-topic", R"({"ern": "ern:topic/order-events", "status": "STOPPED", "released": 0})"},
                {"publish-message", R"({"messageId": "m-1"})"},
        }));

        const auto stopped = client.ens.StopTopic(kTopic);
        BOOST_TEST(stopped.status == std::string(ENS::TopicStopped));
        BOOST_TEST(stopped.released == 0L);

        // Still accepted, and kept until the topic is started again.
        BOOST_TEST(client.ens.PublishMessage(kTopic, "{}") == "m-1");
    }

    BOOST_AUTO_TEST_CASE(HandsOverWhatItHeldWhenStartedAgain) {
        const EnsClient client(Test::Answering(R"({"ern": "ern:topic/order-events", "status": "RUNNING", "released": 5})"));

        const auto started = client.ens.StartTopic(kTopic);
        BOOST_TEST(started.status == std::string(ENS::TopicRunning));
        // Delivery rather than a promise of it: those five went out as part of this call.
        BOOST_TEST(started.released == 5L);
    }

    BOOST_AUTO_TEST_CASE(StartsATopicThatWasNeverStoppedWithoutComplaint) {
        const EnsClient client(Test::Answering(R"({"ern": "ern:topic/order-events", "status": "RUNNING", "released": 0})"));

        const auto started = client.ens.StartTopic(kTopic);
        BOOST_TEST(started.status == std::string(ENS::TopicRunning));
        BOOST_TEST(started.released == 0L);
    }

    BOOST_AUTO_TEST_CASE(CarriesTheServersReasonForATopicThatIsNotThere) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(404, R"({"error": "Topic not found, ern: ern:topic/nope"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const ENS::Ens ens(session);

        try {
            std::ignore = ens.StopTopic("ern:topic/nope");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "ens");
            BOOST_TEST(ex.Action() == "stop-topic");
            BOOST_TEST(ex.Status() == 404);
            BOOST_TEST(ex.Reason() == "Topic not found, ern: ern:topic/nope");
        }
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EnsRetentionTest)

    BOOST_AUTO_TEST_CASE(SetsHowLongAPublishedMessageIsKept) {
        const EnsClient client(Test::Answering(R"({"ern": "ern:topic/order-events", "retentionPeriod": 604800})"));

        const auto result = client.ens.SetTopicRetention(kTopic, 604800);
        BOOST_TEST(result.retentionPeriod == 604800L);
        BOOST_TEST(lastBody(client.gateway).at("retentionPeriod").as_int64() == 604800);
    }

    BOOST_AUTO_TEST_CASE(TakesZeroToMeanTheInstallationsOwnPeriod) {
        const EnsClient client(Test::Answering(R"({"ern": "ern:topic/order-events", "retentionPeriod": 0})"));

        const auto result = client.ens.SetTopicRetention(kTopic, ENS::InstallationRetention);
        BOOST_TEST(result.retentionPeriod == ENS::InstallationRetention);
        BOOST_TEST(lastBody(client.gateway).at("retentionPeriod").as_int64() == 0);
    }

    // Refused before the round trip, which the server would refuse anyway.
    BOOST_AUTO_TEST_CASE(RefusesANegativePeriodBeforeTheRoundTrip) {
        const EnsClient client(Test::Answering());

        BOOST_CHECK_THROW(std::ignore = client.ens.SetTopicRetention(kTopic, -1), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EnsMessageTest)

    BOOST_AUTO_TEST_CASE(AnswersWithTheIdTheServerGaveTheMessage) {
        const EnsClient client(Test::Answering(R"({"messageId": "22222222-2222-2222-2222-222222222222"})"));

        const auto messageId = client.ens.PublishMessage(kTopic, R"({"order": 17})",
                                                         {.attributes = {{"tenant", "acme"}},
                                                          .priority = std::string(COM::PriorityHigh)});
        BOOST_TEST(messageId == "22222222-2222-2222-2222-222222222222");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("ern").as_string() == kTopic);
        BOOST_TEST(body.at("body").as_string() == R"({"order": 17})");
        BOOST_TEST(body.at("attributes").at("tenant").at("value").as_string() == "acme");
        BOOST_TEST(body.at("priority").as_string() == "HIGH");
    }

    BOOST_AUTO_TEST_CASE(LeavesThePriorityOutWhenThereIsNone) {
        const EnsClient client(Test::Answering(R"({"messageId": "m-1"})"));

        std::ignore = client.ens.PublishMessage(kTopic, "{}");
        BOOST_TEST(!lastBody(client.gateway).as_object().contains("priority"));
    }

    BOOST_AUTO_TEST_CASE(ListsATopicsMessages) {
        const auto messages = boost::json::serialize(boost::json::object{
                {"total", 1},
                {"messages", boost::json::array{boost::json::object{
                                     {"ern", "ern:message/1"},
                                     {"topicErn", kTopic},
                                     {"messageId", "m-1"},
                                     {"status", "SEND"},
                                     {"body", R"({"order": 17})"},
                                     {"contentType", "application/json"},
                                     {"attributes", boost::json::object{{"tenant", boost::json::object{{"type", "string"}, {"value", "acme"}}}}},
                                     {"created", "2026-09-01T08:00:00Z"}}}}});

        const EnsClient client(Test::Answering(messages));

        const auto page = client.ens.ListMessages(kTopic, {.pageSize = 5});
        BOOST_REQUIRE(page.items.size() == 1U);
        BOOST_TEST(page.items[0].topicErn == kTopic);
        BOOST_TEST(page.items[0].attributes.at("tenant").Get<std::string>() == "acme");

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("topicErn").as_string() == kTopic);
        BOOST_TEST(body.at("sortColumn").as_string() == "created");
        BOOST_TEST(!body.contains("prefix"));
    }

    // A topic counts delivery rather than a backlog, which is why these are not a queue's counters.
    BOOST_AUTO_TEST_CASE(CountsDeliveryRatherThanABacklog) {
        const EnsClient client(Test::Answering(R"({"ern": "ern:topic/order-events", "available": 12, "send": 30, "resend": 2})"));

        const auto counts = client.ens.GetMessageCount(kTopic);
        BOOST_TEST(counts.available == 12L);
        BOOST_TEST(counts.send == 30L);
        BOOST_TEST(counts.resend == 2L);
    }

    // "key" both ways here, where EQS reads it back as "name".
    BOOST_AUTO_TEST_CASE(CarriesAttributesUnderTheKeyEnsUses) {
        const EnsClient client(Test::Answering(R"({"messageId": "m-1", "key": "tenant",
                                                   "value": {"type": "string", "value": "acme"}})"));

        const auto set = client.ens.SetMessageAttribute("m-1", "tenant", "acme");
        BOOST_TEST(set.key == "tenant");
        BOOST_TEST(set.value.Get<std::string>() == "acme");

        auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("key").as_string() == "tenant");
        BOOST_TEST(!body.contains("name"));

        std::ignore = client.ens.GetMessageAttribute("m-1", "tenant");
        body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("key").as_string() == "tenant");
        BOOST_TEST(!body.contains("name"));
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EnsSubscriptionTest)

    BOOST_AUTO_TEST_CASE(SubscribesAQueueToATopic) {
        const EnsClient client(Test::Answering(R"({"ern": "ern:subscription/1", "sourceErn": "ern:topic/order-events",
                                                   "type": "SQS", "targetErn": "ern:queue/orders"})"));

        const auto result = client.ens.Subscribe(kTopic, kQueue);
        BOOST_TEST(result.ern == "ern:subscription/1");
        BOOST_TEST(result.type == std::string(COM::Queue));

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("sourceErn").as_string() == kTopic);
        BOOST_TEST(body.at("targetErn").as_string() == kQueue);
        BOOST_TEST(body.at("type").as_string() == "SQS");
    }

    BOOST_AUTO_TEST_CASE(LetsTheDeliveryProtocolBeNamed) {
        const EnsClient client(Test::Answering(R"({"ern": "ern:subscription/2", "type": "SNS"})"));

        std::ignore = client.ens.Subscribe(kTopic, "ern:topic/downstream", std::string(COM::Topic));
        BOOST_TEST(lastBody(client.gateway).at("type").as_string() == "SNS");
    }

    BOOST_AUTO_TEST_CASE(ListsAndRemovesSubscriptions) {
        const EnsClient client(Test::Answering(R"({"total": 1, "subscriptions": [
                                                   {"ern": "ern:subscription/1", "sourceErn": "ern:topic/order-events",
                                                    "type": "SQS", "targetErn": "ern:queue/orders",
                                                    "created": "2026-09-01T08:00:00Z"}]})"));

        const auto subscriptions = client.ens.ListSubscriptions(kTopic);
        BOOST_REQUIRE(subscriptions.size() == 1U);
        BOOST_TEST(subscriptions[0].targetErn == "ern:queue/orders");
        BOOST_TEST(lastBody(client.gateway).at("topicErn").as_string() == kTopic);

        // By the subscription's own ERN - not the topic's, and not the queue's.
        client.ens.Unsubscribe("ern:subscription/1");
        BOOST_TEST(lastBody(client.gateway).at("ern").as_string() == "ern:subscription/1");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EnsTransportTest)

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EnsClient client(Test::Answering(R"({"ern": "ern:topic/order-events"})"));

        client.session.ChangeNamespace("reports");
        std::ignore = client.ens.GetTopicErn("order-events");

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "ens");
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "reports");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

    // Two modules of one session are two clients, each naming its own target.
    BOOST_AUTO_TEST_CASE(GivesEachModuleOfOneSessionItsOwnTarget) {
        const FakeGateway gateway(Test::Answering(R"({"ern": "ern:whatever"})"));
        const auto session = Test::Builder(gateway).Login();

        const ENS::Ens ens(session);
        const ESM::Esm esm(session);

        std::ignore = ens.GetTopicErn("order-events");
        BOOST_TEST(std::string(gateway.LastRequest()["x-euclid-target"]) == "ens");

        std::ignore = esm.GetBucketErn("reports");
        BOOST_TEST(std::string(gateway.LastRequest()["x-euclid-target"]) == "esm");
    }

    BOOST_AUTO_TEST_CASE(ReachesAnActionThisSdkDoesNotWrapAndItsMetrics) {
        const EnsClient client(Test::Answering(R"({"ens_topic_count": 3, "whatever": 42})"));

        BOOST_TEST(client.ens.Metrics().at("ens_topic_count").as_int64() == 3);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");

        BOOST_TEST(client.ens.Call("some-future-action").at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

BOOST_AUTO_TEST_SUITE_END()
