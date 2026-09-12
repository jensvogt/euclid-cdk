// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EQS, end to end against a fake euclid server.
 *
 * @par
 * What went on the wire and what came back off it, as in EamTest - plus the two things about a queue
 * client that are not a request shape: the lease a receive hands out, and the long poll, whose whole
 * point is how few requests it costs.
 */

// C++ includes
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <tuple>

// Boost includes
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/eqs/Eqs.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    const std::string kQueue = "ern:euclid:eqs:eu-central-1:000000000000:queue/orders";

    /**
     * @brief A gateway, a logged-in session and an EQS client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EqsClient {

        explicit EqsClient(FakeGateway::Handler handler, const std::chrono::milliseconds timeout = std::chrono::seconds(30))
            : gateway(std::move(handler)), session(Test::Builder(gateway).Timeout(timeout).Login()), eqs(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EQS::Eqs eqs;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief How many of the requests served were for this action.
     */
    long served(const FakeGateway &gateway, const std::string &action) {
        long count = 0;
        for (const auto &request: gateway.Received()) {
            if (std::string(request["x-euclid-action"]) == action) ++count;
        }
        return count;
    }

    std::string messagesResponse(const std::string &body = "{\"order\": 17}") {
        return boost::json::serialize(boost::json::object{
                {"total", 1},
                {"messages", boost::json::array{boost::json::object{
                                     {"ern", "ern:message/1"},
                                     {"queueErn", kQueue},
                                     {"messageId", "11111111-1111-1111-1111-111111111111"},
                                     {"status", "INITIAL"},
                                     {"priority", "HIGH"},
                                     {"body", body},
                                     {"receiptHandle", "receipt-1"},
                                     {"size", 14},
                                     {"receivedCount", 1},
                                     {"contentType", "application/json"},
                                     {"attributes", boost::json::object{{"tenant", boost::json::object{{"type", "string"}, {"value", "acme"}}}}},
                                     {"systemAttributes", boost::json::object{{"priority", boost::json::object{{"type", "string"}, {"value", "HIGH"}}}}},
                                     {"created", "2026-09-01T08:00:00Z"}}}}});
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EqsQueueTest)

    BOOST_AUTO_TEST_CASE(CreatesAndListsQueues) {
        const auto queues = boost::json::serialize(boost::json::object{
                {"total", 2},
                {"queues", boost::json::array{
                                   boost::json::object{
                                           {"name", "orders"},
                                           {"owner", "jens"},
                                           {"ern", kQueue},
                                           {"tags", boost::json::object{{"team", "finance"}}},
                                           {"size", 2048},
                                           {"available", 3},
                                           {"delayed", 1},
                                           {"invisible", 2},
                                           {"visibility", 30},
                                           {"maxMessageLength", 262144},
                                           {"maxReceiveCount", 3},
                                           {"deadLetterQueueArn", "ern:queue/orders-dlq"},
                                           {"priority", "MIDDLE"},
                                           {"status", "AVAILABLE"},
                                           {"created", "2026-01-01T00:00:00Z"}},
                                   boost::json::object{{"name", "euclid-internal"}, {"internal", true}}}}});

        const EqsClient client(Test::AnsweringByAction({{"list-queues", queues},
                                                        {"create-queue", R"({"name": "orders", "ern": ")" + kQueue + R"("})"}}));

        const auto created = client.eqs.CreateQueue("orders");
        BOOST_TEST(created.ern == kQueue);

        // The defaults travel rather than being left to the server to guess at.
        auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("visibility").as_int64() == EQS::DefaultVisibility);
        BOOST_TEST(body.at("maxRetries").as_int64() == EQS::DefaultMaxRetries);
        BOOST_TEST(body.at("maxMessageLength").as_int64() == EQS::DefaultMaxMessageLength);
        BOOST_TEST(body.at("dlqName").as_string() == "");
        BOOST_TEST(body.at("delay").as_int64() == 0);
        BOOST_TEST(body.at("internal").as_bool() == false);

        const auto page = client.eqs.ListQueues({.prefix = "ord", .pageSize = 25});
        BOOST_TEST(page.total == 2L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[0].name == "orders");
        BOOST_TEST(page.items[0].tags.at("team") == "finance");
        BOOST_TEST(page.items[0].available == 3L);
        BOOST_TEST(page.items[0].invisible == 2L);
        // An "arn" the server has never renamed, read into the name this SDK uses.
        BOOST_TEST(page.items[0].deadLetterQueueErn == "ern:queue/orders-dlq");
        BOOST_TEST(page.items[0].status == std::string(EQS::QueueAvailable));
        BOOST_TEST(!page.items[0].internal);
        BOOST_TEST(page.items[1].internal);

        body = lastBody(client.gateway);
        BOOST_TEST(body.at("prefix").as_string() == "ord");
        BOOST_TEST(body.at("sortColumn").as_string() == "name");
        BOOST_TEST(body.at("includeInternal").as_bool() == false);
    }

    BOOST_AUTO_TEST_CASE(AnswersWithTheErnTheMetadataAndTheTags) {
        const EqsClient client(Test::AnsweringByAction({
                {"get-queue-ern", R"({"ern": ")" + kQueue + R"("})"},
                {"get-queue-metadata", R"({"region": "eu-central-1", "accountId": "000000000000", "owner": "jens",
                                           "nameSpace": "reports", "name": "orders", "ern": "ern:queue/orders",
                                           "size": 2048, "messages": 6})"},
        }));

        BOOST_TEST(client.eqs.GetQueueErn("orders") == kQueue);

        const auto metadata = client.eqs.GetQueueMetadata(kQueue);
        BOOST_TEST(metadata.nameSpace == "reports");
        BOOST_TEST(metadata.messages == 6L);
        BOOST_TEST(metadata.size == 2048L);

        client.eqs.AddQueueTag(kQueue, "team", "finance");
        BOOST_TEST(lastBody(client.gateway).at("value").as_string() == "finance");
        client.eqs.SetQueueTag(kQueue, "team", "treasury");
        BOOST_TEST(lastBody(client.gateway).at("value").as_string() == "treasury");
        client.eqs.DeleteQueueTag(kQueue, "team");
        BOOST_TEST(lastBody(client.gateway).at("key").as_string() == "team");
    }

    BOOST_AUTO_TEST_CASE(StopsAndStartsAQueue) {
        const EqsClient client(Test::AnsweringByAction({
                {"stop-queue", R"({"ern": "ern:queue/orders", "status": "STOPPED", "available": 4})"},
                {"start-queue", R"({"ern": "ern:queue/orders", "status": "AVAILABLE", "available": 4})"},
        }));

        BOOST_TEST(client.eqs.StopQueue(kQueue).status == std::string(EQS::QueueStopped));
        const auto started = client.eqs.StartQueue(kQueue);
        BOOST_TEST(started.status == std::string(EQS::QueueAvailable));
        BOOST_TEST(started.available == 4L);
    }

    // Only the default changes; a message already in flight keeps the window it was given.
    BOOST_AUTO_TEST_CASE(AnswersAChangedVisibilityWithTheValueItNowHas) {
        const EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders", "visibility": 60})"));

        BOOST_TEST(client.eqs.SetQueueVisibility(kQueue, 60) == 60L);
        BOOST_TEST(lastBody(client.gateway).at("visibility").as_int64() == 60);
    }

    // Every namespace of the session's account, which is what this call has always done.
    BOOST_AUTO_TEST_CASE(PurgesEveryNamespaceOfTheAccountUnlessToldOtherwise) {
        const EqsClient client(Test::Answering());

        client.eqs.PurgeAllQueues();

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("region").as_string() == "eu-central-1");
        BOOST_TEST(body.at("accountId").as_string() == "000000000000");
        BOOST_TEST(body.at("nameSpace").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(NarrowsABlanketPurgeToOneNamespaceWhenAsked) {
        const EqsClient client(Test::Answering());

        client.eqs.PurgeAllQueues({.accountId = "999999999999", .nameSpace = "reports"});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("accountId").as_string() == "999999999999");
        BOOST_TEST(body.at("nameSpace").as_string() == "reports");
    }

    // Only what is sent from here on: a message already waiting keeps the timestamp it was given.
    BOOST_AUTO_TEST_CASE(ChangesHowLongASentMessageWaits) {
        const EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders", "delay": 30})"));

        BOOST_TEST(client.eqs.SetQueueDelay(kQueue, 30) == 30L);
        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("ern").as_string() == kQueue);
        BOOST_TEST(body.at("delay").as_int64() == 30);
    }

    // A delay is for smoothing a burst, not for scheduling, and the server holds it to SQS's bound.
    BOOST_AUTO_TEST_CASE(RefusesADelayOutsideTheBoundBeforeTheRoundTrip) {
        const EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders", "delay": 0})"));

        BOOST_CHECK_THROW(std::ignore = client.eqs.SetQueueDelay(kQueue, -1), EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.eqs.SetQueueDelay(kQueue, EQS::MaxDelay + 1), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else

        // The bound itself is allowed.
        BOOST_TEST(client.eqs.SetQueueDelay(kQueue, EQS::MaxDelay) == 0L);
    }

    BOOST_AUTO_TEST_CASE(ChangesTheLargestMessageAQueueAccepts) {
        const EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders", "maxMessageLength": 262144,
                                                   "effectiveMaxMessageLength": 262144})"));

        const auto result = client.eqs.SetQueueMaxMessageLength(kQueue, 262144);
        BOOST_TEST(result.maxMessageLength == 262144L);
        BOOST_TEST(result.effectiveMaxMessageLength == 262144L);
        BOOST_TEST(lastBody(client.gateway).at("maxMessageLength").as_int64() == 262144);
    }

    // Zero is the queue having no limit of its own, and the answer says what a send is then measured
    // against - which is not the figure that was stored.
    BOOST_AUTO_TEST_CASE(TakesZeroToMeanNoLimitOfTheQueuesOwn) {
        const EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders", "maxMessageLength": 0,
                                                   "effectiveMaxMessageLength": 1048576})"));

        const auto result = client.eqs.SetQueueMaxMessageLength(kQueue, EQS::InstallationMaxMessageLength);
        BOOST_TEST(result.maxMessageLength == 0L);
        BOOST_TEST(result.effectiveMaxMessageLength == EQS::DefaultMaxMessageLength);

        // Negative is a typo rather than a value, here as on the server.
        BOOST_CHECK_THROW(std::ignore = client.eqs.SetQueueMaxMessageLength(kQueue, -1), EuclidError);
    }

    // What a redrive could not place is reported rather than guessed at.
    BOOST_AUTO_TEST_CASE(RedrivesADeadLetterQueue) {
        const EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders-dlq", "messages": 7, "remaining": 2,
                                                   "targets": [{"queueErn": "ern:queue/orders", "messages": 5},
                                                               {"queueErn": "ern:queue/refunds", "messages": 2}],
                                                   "note": "2 messages have no recorded origin"})"));

        const auto result = client.eqs.RedriveDlq("ern:queue/orders-dlq");
        BOOST_TEST(result.messages == 7L);
        BOOST_TEST(result.remaining == 2L);
        BOOST_REQUIRE(result.targets.size() == 2U);
        BOOST_TEST(result.targets[0].queueErn == "ern:queue/orders");
        BOOST_TEST(result.targets[0].messages == 5L);
        BOOST_TEST(!result.note.empty());
        BOOST_TEST(lastBody(client.gateway).at("targetErn").as_string() == "");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EqsMessageTest)

    BOOST_AUTO_TEST_CASE(SendsReceivesAndDeletesOne) {
        const EqsClient client(Test::AnsweringByAction({
                {"send-message", R"({"messageId": "11111111-1111-1111-1111-111111111111"})"},
                {"get-message-count", R"({"ern": "ern:queue/orders", "available": 1, "delayed": 0, "invisible": 0, "total": 1})"},
                {"receive-messages", messagesResponse()},
        }));

        const auto messageId = client.eqs.SendMessage(kQueue, R"({"order": 17})", {.attributes = {{"tenant", "acme"}}});
        BOOST_TEST(messageId == "11111111-1111-1111-1111-111111111111");
        BOOST_TEST(lastBody(client.gateway).at("attributes").at("tenant").at("type").as_string() == "string");

        const auto page = client.eqs.ReceiveMessages(kQueue);
        BOOST_REQUIRE(page.items.size() == 1U);
        BOOST_TEST(page.items[0].body == R"({"order": 17})");
        BOOST_TEST(page.items[0].receiptHandle == "receipt-1");
        BOOST_TEST(page.items[0].attributes.at("tenant").Get<std::string>() == "acme");
        BOOST_TEST(page.items[0].systemAttributes.at("priority").Get<std::string>() == std::string(COM::PriorityHigh));

        // The receipt handle is the lease, and deleting by it is what says the work was done.
        client.eqs.DeleteMessage(page.items[0].receiptHandle);
        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("receiptHandle").as_string() == "receipt-1");
        BOOST_TEST(!body.as_object().contains("messageId"));
    }

    BOOST_AUTO_TEST_CASE(SendsTheEnvelopeSeparatelyFromTheSendersAttributes) {
        const EqsClient client(Test::Answering(R"({"messageId": "m-1"})"));

        std::ignore = client.eqs.SendMessage(kQueue, "body",
                                             {.attributes = {{"tenant", "acme"}},
                                              .systemAttributes = {{"priority", std::string(COM::PriorityLow)}},
                                              .priority = std::string(COM::PriorityHigh)});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("attributes").at("tenant").at("value").as_string() == "acme");
        BOOST_TEST(body.at("systemAttributes").at("priority").at("value").as_string() == "LOW");
        BOOST_TEST(body.at("priority").as_string() == "HIGH");
    }

    // Nothing to say about the envelope or the priority means the queue's own defaults apply.
    BOOST_AUTO_TEST_CASE(SaysNothingAboutWhatItHasNothingToSayAbout) {
        const EqsClient client(Test::Answering(R"({"messageId": "m-1"})"));

        std::ignore = client.eqs.SendMessage(kQueue, "body");

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(!body.contains("systemAttributes"));
        BOOST_TEST(!body.contains("priority"));
        BOOST_TEST(body.at("attributes").as_object().empty());
    }

    // By ID rather than by receipt handle, which is how a message nobody has received goes.
    BOOST_AUTO_TEST_CASE(DeletesAMessageNobodyReceived) {
        const EqsClient client(Test::Answering());

        client.eqs.DeleteMessageById("m-1");

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("messageId").as_string() == "m-1");
        BOOST_TEST(!body.contains("receiptHandle"));
    }

    // A read rather than a lease: nothing becomes invisible, and nothing counts as a delivery.
    BOOST_AUTO_TEST_CASE(ListsMessagesWithoutLeasingThem) {
        const EqsClient client(Test::Answering(messagesResponse()));

        const auto page = client.eqs.ListMessages(kQueue, {.pageSize = 5});
        BOOST_TEST(page.total == 1L);
        BOOST_REQUIRE(page.items.size() == 1U);
        BOOST_TEST(page.items[0].messageId == "11111111-1111-1111-1111-111111111111");

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("queueErn").as_string() == kQueue);
        BOOST_TEST(body.at("pageSize").as_int64() == 5);
        BOOST_TEST(body.at("sortColumn").as_string() == "created");
        // The server has nothing to match a message against, so no prefix travels.
        BOOST_TEST(!body.contains("prefix"));
    }

    BOOST_AUTO_TEST_CASE(DrainsAQueueInBatches) {
        std::atomic<int> receives{0};
        const EqsClient client(Test::Authenticated([&receives](const Request &request) {
            const auto action = std::string(request["x-euclid-action"]);
            if (action == "get-message-count") {
                return FakeGateway::Json(200, R"({"available": 1, "total": 1})");
            }
            if (action == "receive-messages") {
                // Two batches, then nothing - which is what stops the drain.
                return FakeGateway::Json(200, receives.fetch_add(1) < 2 ? messagesResponse() : R"({"total": 0, "messages": []})");
            }
            return FakeGateway::Json(200, "{}");
        }));

        const auto messages = client.eqs.ReceiveAllMessages(kQueue);
        BOOST_TEST(messages.size() == 2U);
        BOOST_TEST(receives.load() == 3);
    }

    BOOST_AUTO_TEST_CASE(ReadsAMessagesMetadataAndExtendsItsLease) {
        const EqsClient client(Test::Answering(R"({"messageId": "m-1", "queueErn": "ern:queue/orders",
                                                   "receiptHandle": "receipt-1", "status": "INVISIBLE",
                                                   "priority": "MIDDLE", "size": 14, "receivedCount": 2,
                                                   "visibilityTimeout": 30, "contentType": "application/json"})"));

        const auto metadata = client.eqs.GetMessageMetadata("m-1");
        BOOST_TEST(metadata.receivedCount == 2L);
        BOOST_TEST(metadata.visibilityTimeout == 30L);

        client.eqs.SetMessageVisibility("m-1", 120);
        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("visibility").as_int64() == 120);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "set-message-visibility");
    }

    // "key" going out and "name" coming back is the server's own asymmetry, not a slip.
    BOOST_AUTO_TEST_CASE(TypesAttributesAndKeepsTheServersOwnFieldNames) {
        const EqsClient client(Test::Answering(R"({"messageId": "m-1", "name": "retries",
                                                   "value": {"type": "int", "value": 2}})"));

        const auto attribute = client.eqs.SetMessageAttribute("m-1", "retries", 2);
        BOOST_TEST(attribute.name == "retries");
        BOOST_TEST(attribute.value.Get<int>() == 2);

        auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("key").as_string() == "retries");
        BOOST_TEST(!body.contains("name"));
        BOOST_TEST(body.at("value").at("type").as_string() == "int");

        std::ignore = client.eqs.GetMessageAttribute("m-1", "retries");
        body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("name").as_string() == "retries");
        BOOST_TEST(!body.contains("key"));
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EqsLongPollTest)

    // A receive is a write, and one that takes nothing is work the server did for nothing.
    BOOST_AUTO_TEST_CASE(CostsNoReceiveAtAllOnAnEmptyQueue) {
        const EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders", "available": 0, "total": 0})"));

        const auto page = client.eqs.ReceiveMessages(kQueue);
        BOOST_TEST(page.items.empty());
        BOOST_TEST(served(client.gateway, "get-message-count") == 1L);
        BOOST_TEST(served(client.gateway, "receive-messages") == 0L);
    }

    // The waiting is the server's: one request covers the whole window.
    BOOST_AUTO_TEST_CASE(IsOneRequestWhenTheServerHonoursTheWait) {
        const EqsClient client(Test::Authenticated([](const Request &) {
            std::this_thread::sleep_for(std::chrono::milliseconds(900));
            return FakeGateway::Json(200, R"({"total": 0, "messages": []})");
        }));

        const auto page = client.eqs.ReceiveMessages(kQueue, {.waitTime = std::chrono::seconds(1)});
        BOOST_TEST(page.items.empty());
        BOOST_TEST(served(client.gateway, "receive-messages") == 1L);
        BOOST_TEST(lastBody(client.gateway).at("waitTime").as_int64() == 1);
    }

    // Answered early with an empty queue means the server had no slot to wait in, and the answer is
    // to wait a moment rather than to ask again at once.
    BOOST_AUTO_TEST_CASE(AsksAgainWhenTheServerDeclinesToWait) {
        EqsClient client(Test::Answering(R"({"total": 0, "messages": []})"));
        client.eqs.SetSlotsBusyBackoff(std::chrono::milliseconds(200));

        const auto started = std::chrono::steady_clock::now();
        const auto page = client.eqs.ReceiveMessages(kQueue, {.waitTime = std::chrono::seconds(1)});
        const auto elapsed = std::chrono::steady_clock::now() - started;

        BOOST_TEST(page.items.empty());
        // It waited the window out rather than hammering the server or giving up at once.
        BOOST_TEST(elapsed >= std::chrono::milliseconds(700));
        BOOST_TEST(served(client.gateway, "receive-messages") > 1L);
        BOOST_TEST(served(client.gateway, "receive-messages") < 10L);
    }

    // The session's timeout is sized for an answer that comes straight back; a long poll is not one.
    BOOST_AUTO_TEST_CASE(OutlivesTheSessionsOrdinaryTimeout) {
        const EqsClient client(Test::Authenticated([](const Request &) {
                                   std::this_thread::sleep_for(std::chrono::milliseconds(600));
                                   return FakeGateway::Json(200, R"({"total": 0, "messages": []})");
                               }),
                               std::chrono::milliseconds(300));

        // An ordinary call gives up at the session's deadline...
        BOOST_CHECK_THROW(std::ignore = client.eqs.GetQueueErn("orders"), EuclidError);

        // ...while a long poll is allowed the window it asked the server to hold.
        BOOST_TEST(client.eqs.ReceiveMessages(kQueue, {.waitTime = std::chrono::seconds(1)}).items.empty());
        BOOST_TEST(served(client.gateway, "receive-messages") >= 1L);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EqsTransportTest)

    // Instrumentation says it is instrumentation, so the server can log and scale accordingly.
    BOOST_AUTO_TEST_CASE(MarksInternalTrafficAsSuch) {
        const EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders", "available": 2, "total": 2})"));

        std::ignore = client.eqs.GetMessageCount(kQueue);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-internal"]).empty());

        const auto internal = client.eqs.AsInternal();
        std::ignore = internal.GetMessageCount(kQueue);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-internal"]) == "true");

        // The header is this view's, not the session's: the ordinary client still says nothing.
        std::ignore = client.eqs.GetMessageCount(kQueue);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-internal"]).empty());
    }

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EqsClient client(Test::Answering(R"({"ern": "ern:queue/orders"})"));

        client.session.ChangeNamespace("reports");
        std::ignore = client.eqs.GetQueueErn("orders");

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "eqs");
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "reports");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
        BOOST_TEST(SigningScheme::Of(request)->Name() == "rfc9421");
    }

    BOOST_AUTO_TEST_CASE(CarriesTheServersReasonWhenACallIsRefused) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(409, R"({"error": "queue is stopped"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EQS::Eqs eqs(session);

        try {
            std::ignore = eqs.ReceiveMessages(kQueue, {.waitTime = std::chrono::seconds(1)});
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "eqs");
            BOOST_TEST(ex.Action() == "receive-messages");
            BOOST_TEST(ex.Status() == 409);
            BOOST_TEST(ex.Reason() == "queue is stopped");
        }
    }

    BOOST_AUTO_TEST_CASE(ReachesAnActionThisSdkDoesNotWrapAndItsMetrics) {
        const EqsClient client(Test::Answering(R"({"eqs_message_count": 12, "whatever": 42})"));

        BOOST_TEST(client.eqs.Metrics().at("eqs_message_count").as_int64() == 12);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");

        // "get-metadata" is one of the two actions this SDK does not name, as in euclid-ndk.
        BOOST_TEST(client.eqs.Call("get-metadata", {{"ern", kQueue}}).at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metadata");
    }

BOOST_AUTO_TEST_SUITE_END()
