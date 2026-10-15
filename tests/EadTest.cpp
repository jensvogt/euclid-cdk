// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EAD, end to end against a fake euclid server.
 *
 * @par
 * A small module, and the thing worth testing in it is the capped total: a page says "at least this
 * many" rather than a count, and a reader that took it for exact would draw a pager that asks for a
 * page which is not there. The other is the purge window, which must not be possible to leave out.
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
#include <euclid/cdk/ead/Ead.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    struct EadClient {

        explicit EadClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), ead(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EAD::Ead ead;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief One entry of the trail, as the server records it.
     */
    boost::json::object auditEvent(const std::string &command = "delete-object") {
        return {
                {"accountId", "000000000000"},
                // "namespace" here, as in EAP, ETS and EAG.
                {"namespace", "integration"},
                {"userId", "jens"},
                {"module", "esm"},
                {"command", command},
                {"parameters", boost::json::object{{"bucket", "ern:bucket/reports"}, {"key", "q3.pdf"}}},
                {"status", "200"},
                {"created", "2026-10-15T08:00:00Z"},
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EadTest)

    BOOST_AUTO_TEST_CASE(ListsTheTrailAndFiltersIt) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"events", boost::json::array{auditEvent(), auditEvent("delete-bucket")}},
                {"total", 2},
                {"totalExact", true}});
        const EadClient client(Test::Answering(answer));

        const auto page = client.ead.ListEvents({.userId = "jens", .module = "esm",
                                                 .command = "delete-object", .pageSize = 50});

        BOOST_TEST(page.total == 2L);
        BOOST_TEST(page.totalExact);
        BOOST_REQUIRE(page.events.size() == 2U);
        BOOST_TEST(page.events[0].userId == "jens");
        BOOST_TEST(page.events[0].module == "esm");
        BOOST_TEST(page.events[0].nameSpace == "integration");
        BOOST_TEST(page.events[0].status == "200");
        // What a command was called with is that command's own shape, so it is left as it came.
        BOOST_TEST(page.events[0].parameters.at("key").as_string() == "q3.pdf");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("userId").as_string() == "jens");
        // "module" is the module that answered the command, not the x-euclid-target of this request.
        BOOST_TEST(body.at("module").as_string() == "esm");
        BOOST_TEST(body.at("command").as_string() == "delete-object");
        BOOST_TEST(body.at("pageSize").as_int64() == 50);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-target"]) == "ead");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "list-events");
    }

    // false means "at least this many", which is all a pager needs to know there is another page.
    // An exact count on a trail of tens of millions is seconds of database time beside a page of
    // fifty, paid again on every page somebody turns.
    BOOST_AUTO_TEST_CASE(SaysWhenTheTotalIsAFloorRatherThanACount) {
        const auto answer = boost::json::serialize(boost::json::object{
                {"events", boost::json::array{auditEvent()}},
                {"total", EAD::ListTotalLimit},
                {"totalExact", false}});
        const EadClient client(Test::Answering(answer));

        const auto page = client.ead.ListEvents();
        BOOST_TEST(page.total == EAD::ListTotalLimit);
        BOOST_TEST(!page.totalExact);
    }

    // A server that does not send the field is one that counted exactly; reading its total as
    // approximate would have a pager ask for a page that is not there.
    BOOST_AUTO_TEST_CASE(ATotalWithNothingSaidAboutItIsExact) {
        const EadClient client(Test::Answering(R"({"events": [], "total": 0})"));

        BOOST_TEST(client.ead.ListEvents().totalExact);
    }

    BOOST_AUTO_TEST_CASE(CountsExactlyWhenTheNumberIsTheAnswer) {
        const EadClient client(Test::Answering(R"({"count": 4815162342})"));

        BOOST_TEST(client.ead.CountEvents("jens", "esm") == 4815162342L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("userId").as_string() == "jens");
        BOOST_TEST(body.at("command").as_string() == "");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "count-events");
    }

    BOOST_AUTO_TEST_CASE(PurgesOlderThanAWindow) {
        const EadClient client(Test::Answering(R"({"removed": 918})"));

        BOOST_TEST(client.ead.PurgeEvents(90) == 918L);
        BOOST_TEST(lastBody(client.gateway).at("olderThanDays").as_int64() == 90);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "purge-events");
    }

    // Zero is what a caller who left the window out sends, and a server reading that as
    // "everything" would take the whole trail. Refused before it travels.
    BOOST_AUTO_TEST_CASE(RefusesAPurgeWithNoWindow) {
        const EadClient client(Test::Answering(R"({"removed": 0})"));

        BOOST_CHECK_THROW(std::ignore = client.ead.PurgeEvents(0), EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.ead.PurgeEvents(-30), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndReachesItsMetrics) {
        EadClient client(Test::Answering(R"({"ead_event_count": 11})"));

        client.session.ChangeNamespace("integration");
        BOOST_TEST(client.ead.Metrics().at("ead_event_count").as_int64() == 11);

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "integration");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

BOOST_AUTO_TEST_SUITE_END()
