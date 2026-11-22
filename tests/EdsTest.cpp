// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EDS, end to end against a fake euclid server.
 *
 * @par
 * Two things here are easy to get wrong and expensive to get wrong. The first is that a record is a
 * *set*: PutRecord() sends every value at a name, and a client that sent one value at a time would
 * silently discard the others on each call. The second is that an answer's records carry "value"
 * where a stored set carries "values" - the same record, spelled differently depending on whether it
 * is being stored or served, and a reader that mixed them up would read every answer as empty.
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
#include <euclid/cdk/eds/Eds.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    struct EdsClient {

        explicit EdsClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), eds(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EDS::Eds eds;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief A zone, as describeZone() writes one.
     */
    boost::json::object zone(const std::string &name = "example.com", const long records = 4) {
        return {
                {"name", name},
                {"ern", "ern:eds:eu-central-1:000000000000:zone/" + name},
                // "namespace" here, as in EAP, ETS, EAG and EAD.
                {"namespace", "integration"},
                {"ttl", 300},
                {"description", "the example zone"},
                {"recordCount", records},
                {"serial", 1763800000},
                {"created", "2026-11-01T08:00:00Z"},
                {"modified", "2026-11-22T09:30:00Z"},
        };
    }

    /**
     * @brief A stored record set, as describeRecord() writes one - every value under "values".
     */
    boost::json::object record(const std::string &name = "www.example.com",
                               const std::string &type = "A",
                               boost::json::array values = {"10.0.0.1", "10.0.0.2"}) {
        return {
                {"zone", "example.com"},
                {"name", name},
                {"type", type},
                {"ttl", 60},
                {"values", std::move(values)},
                {"created", "2026-11-01T08:05:00Z"},
                {"modified", "2026-11-22T09:30:00Z"},
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EdsTest)

    BOOST_AUTO_TEST_CASE(CreatesAZone) {
        const EdsClient client(Test::Answering(boost::json::serialize(zone())));

        const auto created = client.eds.CreateZone("example.com", 300, "the example zone");

        BOOST_TEST(created.name == "example.com");
        BOOST_TEST(created.nameSpace == "integration");
        BOOST_TEST(created.ttl == 300L);
        BOOST_TEST(created.description == "the example zone");
        BOOST_TEST(created.recordCount == 4L);
        BOOST_TEST(created.serial == 1763800000L);
        BOOST_TEST(created.modified == "2026-11-22T09:30:00Z");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "example.com");
        BOOST_TEST(body.at("ttl").as_int64() == 300);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-target"]) == "eds");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "create-zone");
    }

    // Zero travels rather than being replaced here, because what it means belongs to the server:
    // the zone takes DefaultTtl, and a client filling that in would be a second place to change
    // when the server's default moves.
    BOOST_AUTO_TEST_CASE(AZoneWithNoTtlAsksTheServerForItsDefault) {
        const EdsClient client(Test::Answering(boost::json::serialize(zone())));

        std::ignore = client.eds.CreateZone("example.com");

        BOOST_TEST(lastBody(client.gateway).at("ttl").as_int64() == 0);
        BOOST_TEST(lastBody(client.gateway).at("description").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(ReadsAndListsZones) {
        const EdsClient client(Test::AnsweringByAction({
                {"get-zone", boost::json::serialize(zone())},
                {"list-zones", boost::json::serialize(boost::json::object{
                                       {"zones", boost::json::array{zone(), zone("example.org", 1)}},
                                       {"total", 7}})},
        }));

        BOOST_TEST(client.eds.GetZone("example.com").ern.starts_with("ern:eds:"));

        const auto page = client.eds.ListZones({.prefix = "example", .pageSize = 25});
        // The total counts what the account holds; the page holds what this caller may read of it,
        // so the two differ whenever a grant covers only some of the zones.
        BOOST_TEST(page.total == 7L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[1].name == "example.org");
        BOOST_TEST(page.items[1].recordCount == 1L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("prefix").as_string() == "example");
        BOOST_TEST(body.at("pageSize").as_int64() == 25);
        BOOST_TEST(body.at("pageIndex").as_int64() == 0);
    }

    BOOST_AUTO_TEST_CASE(DeletingAZoneSaysHowMuchWentWithIt) {
        const EdsClient client(Test::Answering(R"({"deletedRecords": 12})"));

        BOOST_TEST(client.eds.DeleteZone("example.com") == 12L);
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "example.com");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "delete-zone");
    }

    // The whole set in one call. A client that sent a value at a time would replace the set on each
    // one and leave only the last value standing.
    BOOST_AUTO_TEST_CASE(PutsAWholeRecordSet) {
        const EdsClient client(Test::Answering(boost::json::serialize(record())));

        const auto stored = client.eds.PutRecord("example.com", "www", EDS::TypeA, 60,
                                                 {"10.0.0.1", "10.0.0.2"});

        BOOST_TEST(stored.zone == "example.com");
        BOOST_TEST(stored.name == "www.example.com");
        BOOST_TEST(stored.type == "A");
        BOOST_TEST(stored.ttl == 60L);
        BOOST_REQUIRE(stored.values.size() == 2U);
        BOOST_TEST(stored.values[1] == "10.0.0.2");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("zone").as_string() == "example.com");
        // Sent relative; the server qualifies it into the zone, which is why the answer reads
        // www.example.com where the request read www.
        BOOST_TEST(body.at("name").as_string() == "www");
        BOOST_TEST(body.at("type").as_string() == "A");
        BOOST_REQUIRE(body.at("values").as_array().size() == 2U);
        BOOST_TEST(body.at("values").as_array()[0].as_string() == "10.0.0.1");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "put-record");
    }

    // An empty set is not a record, and a server reading it as "remove the set" would turn a
    // mistake into a deletion. Refused before it travels, naming the call that does mean it.
    BOOST_AUTO_TEST_CASE(RefusesARecordSetWithNoValues) {
        const EdsClient client(Test::Answering(boost::json::serialize(record())));

        BOOST_CHECK_THROW(std::ignore = client.eds.PutRecord("example.com", "www", EDS::TypeA, 60, {}),
                          EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

    BOOST_AUTO_TEST_CASE(ListsAndDeletesRecords) {
        const EdsClient client(Test::AnsweringByAction({
                {"list-records", boost::json::serialize(boost::json::object{
                                         {"records", boost::json::array{
                                                             record(),
                                                             record("example.com", "MX", {"10 mail.example.com."})}},
                                         {"total", 2}})},
                {"delete-record", R"({"deleted": true})"},
        }));

        const auto page = client.eds.ListRecords("example.com", {.type = std::string(EDS::TypeA), .pageSize = 50});
        BOOST_TEST(page.total == 2L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[1].type == "MX");
        // Presentation form, the way a zone file writes it - priority and host, not two fields.
        BOOST_TEST(page.items[1].values[0] == "10 mail.example.com.");

        auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("zone").as_string() == "example.com");
        BOOST_TEST(body.at("type").as_string() == "A");
        BOOST_TEST(body.at("name").as_string() == "");

        client.eds.DeleteRecord("example.com", "www", EDS::TypeA);
        body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "www");
        BOOST_TEST(body.at("type").as_string() == "A");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "delete-record");
    }

    // "value" singular in an answer, "values" plural in a stored set. Reading the answer with the
    // stored reader would leave every record blank, which is why this is tested rather than assumed.
    BOOST_AUTO_TEST_CASE(ReadsAnAnswerAsTheNameServerWouldGiveIt) {
        const auto body = boost::json::serialize(boost::json::object{
                {"name", "www.example.com"},
                {"type", "A"},
                {"authoritative", true},
                {"zone", "example.com"},
                {"rcode", "NOERROR"},
                {"answers", boost::json::array{
                                    boost::json::object{{"name", "www.example.com"}, {"type", "A"}, {"ttl", 60}, {"value", "10.0.0.1"}},
                                    boost::json::object{{"name", "www.example.com"}, {"type", "A"}, {"ttl", 60}, {"value", "10.0.0.2"}}}},
                {"authority", boost::json::array{
                                      boost::json::object{{"name", "example.com"}, {"type", "NS"}, {"ttl", 300}, {"value", "ns1.example.com."}}}},
        });
        const EdsClient client(Test::Answering(body));

        const auto answer = client.eds.GetAnswer("www.example.com", EDS::TypeA);

        BOOST_TEST(answer.authoritative);
        BOOST_TEST(answer.zone == "example.com");
        BOOST_TEST(answer.rcode == EDS::RcodeNoError);
        BOOST_REQUIRE(answer.answers.size() == 2U);
        BOOST_TEST(answer.answers[0].value == "10.0.0.1");
        BOOST_TEST(answer.answers[0].ttl == 60L);
        BOOST_REQUIRE(answer.authority.size() == 1U);
        BOOST_TEST(answer.authority[0].type == "NS");

        BOOST_TEST(lastBody(client.gateway).at("type").as_string() == "A");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-answer");
    }

    // A name no zone of ours covers: refused rather than denied, and the response carries neither a
    // zone nor an authority section. Both have to read as empty rather than throw.
    BOOST_AUTO_TEST_CASE(ANameNoZoneCoversComesBackRefusedAndEmpty) {
        const EdsClient client(Test::Answering(
                R"({"name": "www.nowhere.test", "type": "A", "authoritative": false, "rcode": "REFUSED", "answers": []})"));

        const auto answer = client.eds.GetAnswer("www.nowhere.test");

        BOOST_TEST(!answer.authoritative);
        BOOST_TEST(answer.rcode == EDS::RcodeRefused);
        BOOST_TEST(answer.zone == "");
        BOOST_TEST(answer.answers.empty());
        BOOST_TEST(answer.authority.empty());
        // Defaulted to A, the way DNS itself defaults it.
        BOOST_TEST(lastBody(client.gateway).at("type").as_string() == "A");
    }

    // NXDOMAIN is a zone of ours saying the name does not exist, which is a stronger statement than
    // REFUSED - and the SOA proving it arrives in the authority section, not the answer.
    BOOST_AUTO_TEST_CASE(ANameThatDoesNotExistInOurZoneIsNxDomain) {
        const EdsClient client(Test::Answering(boost::json::serialize(boost::json::object{
                {"name", "nothing.example.com"},
                {"type", "A"},
                {"authoritative", true},
                {"zone", "example.com"},
                {"rcode", "NXDOMAIN"},
                {"answers", boost::json::array{}},
                {"authority", boost::json::array{boost::json::object{
                                      {"name", "example.com"}, {"type", "SOA"}, {"ttl", 300}, {"value", "ns1.example.com. hostmaster.example.com. 1763800000 7200 900 1209600 300"}}}},
        })));

        const auto answer = client.eds.GetAnswer("nothing.example.com", EDS::TypeA);

        BOOST_TEST(answer.authoritative);
        BOOST_TEST(answer.rcode == EDS::RcodeNxDomain);
        BOOST_TEST(answer.answers.empty());
        BOOST_REQUIRE(answer.authority.size() == 1U);
        BOOST_TEST(answer.authority[0].type == "SOA");
    }

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndReachesItsMetrics) {
        EdsClient client(Test::Answering(R"({"eds_zone_count": 3})"));

        client.session.ChangeNamespace("integration");
        BOOST_TEST(client.eds.Metrics().at("eds_zone_count").as_int64() == 3);

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "integration");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

BOOST_AUTO_TEST_SUITE_END()
