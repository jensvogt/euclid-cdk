// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EKV, end to end against a fake euclid server.
 *
 * @par
 * Two things here are not request shapes. An item's timestamps arrive as two ordinary attributes and
 * have to come back out, or an item read and written back grows them permanently; and a read that
 * misses is a 404, which GetItem() passes on and FindItem() turns into nothing at all.
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
#include <euclid/cdk/ekv/Ekv.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    /**
     * @brief A gateway, a logged-in session and an EKV client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EkvClient {

        explicit EkvClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), ekv(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EKV::Ekv ekv;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief One item as the server sends it: the attributes, plus the two timestamps it keeps among
     * them.
     */
    boost::json::object item(const std::string &host = "laptop") {
        return {
                {"userId", "jens"},
                {"startedAt", 1757462400},
                {"host", host},
                {"tags", boost::json::array{"work", "vpn"}},
                {"client", boost::json::object{{"os", "linux"}, {"version", 3}}},
                {"_created", "2026-09-10T00:00:00Z"},
                {"_modified", "2026-09-10T01:00:00Z"},
        };
    }

    boost::json::object table() {
        return {
                {"name", "sessions"},
                {"ern", "ern:euclid:ekv:eu-central-1:000000000000:table/sessions"},
                {"partitionKey", "userId"},
                {"partitionKeyType", "string"},
                {"sortKey", "startedAt"},
                {"sortKeyType", "number"},
                {"itemCount", 12},
                {"created", "2026-01-01T00:00:00Z"},
                {"modified", "2026-09-10T01:00:00Z"},
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EkvTableTest)

    BOOST_AUTO_TEST_CASE(CreatesATableOnAPartitionKeyAndASortKey) {
        const EkvClient client(Test::Answering(boost::json::serialize(table())));

        const auto created = client.ekv.CreateTable("sessions", "userId",
                                                    {.sortKey = "startedAt", .sortKeyType = std::string(EKV::KeyNumber)});
        BOOST_TEST(created.name == "sessions");
        BOOST_TEST(created.partitionKey == "userId");
        BOOST_TEST(created.sortKey == "startedAt");
        BOOST_TEST(created.sortKeyType == std::string(EKV::KeyNumber));
        BOOST_TEST(created.itemCount == 12L);

        // A string partition key unless the caller says otherwise, and the sort key sent either way.
        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("partitionKey").as_string() == "userId");
        BOOST_TEST(body.at("partitionKeyType").as_string() == std::string(EKV::KeyString));
        BOOST_TEST(body.at("sortKey").as_string() == "startedAt");
        BOOST_TEST(body.at("sortKeyType").as_string() == "number");
    }

    // A table with no sort key is a table Query() can only take whole partitions of.
    BOOST_AUTO_TEST_CASE(CreatesATableOnAPartitionKeyAlone) {
        auto keyed = table();
        keyed["sortKey"] = "";
        keyed["sortKeyType"] = "";
        const EkvClient client(Test::Answering(boost::json::serialize(keyed)));

        const auto created = client.ekv.CreateTable("settings", "userId");
        BOOST_TEST(created.sortKey.empty());
        BOOST_TEST(lastBody(client.gateway).at("sortKey").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(DescribesAndListsTables) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"total", 2},
                {"tables", boost::json::array{table(), boost::json::object{{"name", "settings"}, {"partitionKey", "userId"}}}}});

        const EkvClient client(Test::AnsweringByAction({{"describe-table", boost::json::serialize(table())},
                                                        {"list-tables", listed}}));

        BOOST_TEST(client.ekv.DescribeTable("sessions").itemCount == 12L);
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "sessions");

        const auto page = client.ekv.ListTables({.prefix = "se"});
        BOOST_TEST(page.total == 2L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[0].name == "sessions");
        BOOST_TEST(page.items[1].sortKey.empty());
        BOOST_TEST(lastBody(client.gateway).at("sortColumn").as_string() == "name");
    }

    BOOST_AUTO_TEST_CASE(DeletingATableSaysHowManyItemsWentWithIt) {
        const EkvClient client(Test::Answering(R"({"deletedItems": 12})"));

        BOOST_TEST(client.ekv.DeleteTable("sessions") == 12L);
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "sessions");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EkvItemTest)

    // The timestamps come out of the attributes, or an item read and written back grows two.
    BOOST_AUTO_TEST_CASE(LiftsTheTimestampsOutOfAnItemsAttributes) {
        const EkvClient client(Test::Answering(boost::json::serialize(item())));

        const auto stored = client.ekv.GetItem("sessions", {{"userId", "jens"}, {"startedAt", 1757462400}});

        BOOST_TEST(stored.created == "2026-09-10T00:00:00Z");
        BOOST_TEST(stored.modified == "2026-09-10T01:00:00Z");
        BOOST_TEST(!stored.attributes.contains(EKV::CreatedAttribute));
        BOOST_TEST(!stored.attributes.contains(EKV::ModifiedAttribute));
        BOOST_TEST(stored.attributes.size() == 5U);

        // What was stored is what comes back, nesting and all - EKV keeps documents, not typed maps.
        BOOST_TEST(stored.attributes.at("host").as_string() == "laptop");
        BOOST_TEST(stored.attributes.at("startedAt").as_int64() == 1757462400);
        BOOST_TEST(stored.attributes.at("tags").as_array().size() == 2U);
        BOOST_TEST(stored.attributes.at("client").at("os").as_string() == "linux");
    }

    // The attributes an item comes back with are what can be written straight back.
    BOOST_AUTO_TEST_CASE(WritesAnItemBackWithoutTheTimestampsItCameWith) {
        const EkvClient client(Test::Answering(boost::json::serialize(item("desktop"))));

        auto stored = client.ekv.GetItem("sessions", {{"userId", "jens"}});
        stored.attributes["host"] = "desktop";

        const auto written = client.ekv.PutItem("sessions", stored.attributes);
        BOOST_TEST(written.attributes.at("host").as_string() == "desktop");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("table").as_string() == "sessions");
        const auto &sent = body.at("item").as_object();
        BOOST_TEST(sent.at("host").as_string() == "desktop");
        BOOST_TEST(!sent.contains("_created"));
        BOOST_TEST(!sent.contains("_modified"));
    }

    // "There is no such item" and "here is an item with nothing in it" are different answers.
    BOOST_AUTO_TEST_CASE(AMissingItemIsAnErrorFromGetAndNothingFromFind) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(404, R"({"error": "No such item"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EKV::Ekv ekv(session);

        try {
            std::ignore = ekv.GetItem("sessions", {{"userId", "nobody"}});
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "ekv");
            BOOST_TEST(ex.Action() == "get-item");
            BOOST_TEST(ex.Status() == 404);
        }

        BOOST_TEST(!ekv.FindItem("sessions", {{"userId", "nobody"}}).has_value());
    }

    // Only a 404 is "not there" - a refusal is still a refusal.
    BOOST_AUTO_TEST_CASE(FindItemPassesOnEverythingThatIsNotAMiss) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(400, R"({"error": "key does not match the table's key attributes"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EKV::Ekv ekv(session);

        BOOST_CHECK_THROW(std::ignore = ekv.FindItem("sessions", {{"wrong", "key"}}), ServiceError);
    }

    // Deleting what is not there has already achieved what the caller asked for.
    BOOST_AUTO_TEST_CASE(DeletingSaysWhetherThereWasAnythingToDelete) {
        const EkvClient client(Test::Answering(R"({"deleted": true})"));
        BOOST_TEST(client.ekv.DeleteItem("sessions", {{"userId", "jens"}}));

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("table").as_string() == "sessions");
        BOOST_TEST(body.at("key").at("userId").as_string() == "jens");

        const EkvClient missing(Test::Answering(R"({"deleted": false})"));
        BOOST_TEST(!missing.ekv.DeleteItem("sessions", {{"userId", "nobody"}}));
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EkvReadTest)

    BOOST_AUTO_TEST_CASE(TakesAWholePartitionByDefault) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"items", boost::json::array{item(), item("phone")}}, {"count", 2}});

        const EkvClient client(Test::Answering(answer));

        const auto result = client.ekv.Query("sessions", "jens");
        BOOST_TEST(result.count == 2L);
        BOOST_REQUIRE(result.items.size() == 2U);
        BOOST_TEST(result.items[1].attributes.at("host").as_string() == "phone");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("partitionKey").as_string() == "jens");
        BOOST_TEST(body.at("sortOperator").as_string() == std::string(EKV::WholePartition));
        BOOST_TEST(body.at("sortValue").is_null());
        BOOST_TEST(body.at("sortUpper").is_null());
        // Always sent: the server reads an absent flag as descending.
        BOOST_TEST(body.at("forward").as_bool() == true);
        BOOST_TEST(body.at("pageSize").as_int64() == 0);
    }

    BOOST_AUTO_TEST_CASE(NarrowsAPartitionByItsSortKey) {
        const EkvClient client(Test::Answering(R"({"items": [], "count": 0})"));

        std::ignore = client.ekv.Query("sessions", "jens",
                                       {.sortOperator = std::string(EKV::SortGe),
                                        .sortValue = 1757462400,
                                        .forward = false,
                                        .pageSize = 10,
                                        .pageIndex = 2});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("sortOperator").as_string() == "ge");
        BOOST_TEST(body.at("sortValue").as_int64() == 1757462400);
        BOOST_TEST(body.at("forward").as_bool() == false);
        BOOST_TEST(body.at("pageSize").as_int64() == 10);
        BOOST_TEST(body.at("pageIndex").as_int64() == 2);
    }

    BOOST_AUTO_TEST_CASE(SendsBothBoundsOfABetween) {
        const EkvClient client(Test::Answering(R"({"items": [], "count": 0})"));

        std::ignore = client.ekv.Query("sessions", "jens", {.sortOperator = std::string(EKV::SortBetween),
                                                            .sortValue = 1757462400,
                                                            .sortUpper = 1757466000});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("sortValue").as_int64() == 1757462400);
        BOOST_TEST(body.at("sortUpper").as_int64() == 1757466000);
    }

    // Refused before the round trip, which the server would refuse anyway.
    BOOST_AUTO_TEST_CASE(RefusesABetweenMissingABound) {
        const EkvClient client(Test::Answering(R"({"items": [], "count": 0})"));

        BOOST_CHECK_THROW(std::ignore = client.ekv.Query("sessions", "jens",
                                                         {.sortOperator = std::string(EKV::SortBetween), .sortValue = 1}),
                          EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.ekv.Query("sessions", "jens",
                                                         {.sortOperator = std::string(EKV::SortBetween), .sortUpper = 2}),
                          EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

    // A query says what it returned; a scan also says what there is to page through.
    BOOST_AUTO_TEST_CASE(ScanReadsATableAndSaysHowMuchOfItThereIs) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"items", boost::json::array{item()}}, {"count", 1}, {"total", 12}});

        const EkvClient client(Test::Answering(answer));

        const auto result = client.ekv.Scan("sessions", {.pageSize = 1, .pageIndex = 3});
        BOOST_TEST(result.count == 1L);
        BOOST_TEST(result.total == 12L);
        BOOST_REQUIRE(result.items.size() == 1U);
        BOOST_TEST(result.items[0].created == "2026-09-10T00:00:00Z");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("table").as_string() == "sessions");
        BOOST_TEST(body.at("pageSize").as_int64() == 1);
        BOOST_TEST(body.at("pageIndex").as_int64() == 3);
    }

    // A sparse answer stays self-consistent: what came back is the count.
    BOOST_AUTO_TEST_CASE(CountsWhatArrivedWhenTheServerDidNot) {
        const EkvClient client(Test::Answering(R"({"items": [{"userId": "jens"}, {"userId": "jill"}]})"));

        BOOST_TEST(client.ekv.Query("sessions", "jens").count == 2L);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EkvTransportTest)

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EkvClient client(Test::Answering(R"({"total": 0, "tables": []})"));

        client.session.ChangeNamespace("reports");
        std::ignore = client.ekv.ListTables();

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "ekv");
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "reports");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

    BOOST_AUTO_TEST_CASE(ReachesItsMetricsAndAnActionThisSdkDoesNotWrap) {
        const EkvClient client(Test::Answering(R"({"ekv_table_count": 4, "whatever": 42})"));

        BOOST_TEST(client.ekv.Metrics().at("ekv_table_count").as_int64() == 4);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");

        BOOST_TEST(client.ekv.Call("some-future-action").at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

BOOST_AUTO_TEST_SUITE_END()
