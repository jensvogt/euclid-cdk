// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief ESS, end to end against a fake euclid server.
 *
 * @par
 * The thing worth checking in a secret store is which calls carry a value: only get-secret answers
 * with one, and only create and update send one. Everything else here is metadata, and these tests
 * say so field by field.
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
#include <euclid/cdk/ess/Ess.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    const std::string kSecretErn = "ern:euclid:ess:eu-central-1:000000000000:secret/db-password";
    const std::string kKeyErn = "ern:euclid:ekm:eu-central-1:000000000000:key/11111111";

    /**
     * @brief A gateway, a logged-in session and an ESS client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EssClient {

        explicit EssClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), ess(session) {}

        FakeGateway gateway;
        EAM::Session session;
        ESS::Ess ess;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief One secret's metadata, as the server describes it.
     */
    boost::json::object secret(const long version = 1) {
        return {
                {"name", "db-password"},
                {"ern", kSecretErn},
                {"description", "the reporting database"},
                {"encryptionKeyErn", kKeyErn},
                {"version", version},
                {"rotated", "2026-09-01T08:00:00Z"},
                {"tags", boost::json::object{{"team", "finance"}}},
                {"created", "2026-01-01T00:00:00Z"},
                {"modified", "2026-09-01T08:00:00Z"},
        };
    }

    /**
     * @brief What the actions that answer with metadata alone answer with.
     */
    std::string secretResponse(const long version = 1) {
        return boost::json::serialize(boost::json::object{{"secret", secret(version)}});
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EssSecretTest)

    // The value goes out and the metadata comes back: nothing here echoes what was just stored.
    BOOST_AUTO_TEST_CASE(StoresASecretAndAnswersWithItsMetadata) {
        const EssClient client(Test::Answering(secretResponse()));

        const auto stored = client.ess.CreateSecret("db-password", "hunter2",
                                                    {.description = "the reporting database", .keyErn = kKeyErn});
        BOOST_TEST(stored.name == "db-password");
        BOOST_TEST(stored.ern == kSecretErn);
        BOOST_TEST(stored.encryptionKeyErn == kKeyErn);
        BOOST_TEST(stored.version == 1L);
        BOOST_TEST(stored.tags.at("team") == "finance");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "db-password");
        BOOST_TEST(body.at("value").as_string() == "hunter2");
        BOOST_TEST(body.at("description").as_string() == "the reporting database");
        BOOST_TEST(body.at("keyErn").as_string() == kKeyErn);
    }

    // A secret nobody can read is not a secret, and the server says so too.
    BOOST_AUTO_TEST_CASE(RefusesAnEmptyValueBeforeTheRoundTrip) {
        const EssClient client(Test::Answering(secretResponse()));

        BOOST_CHECK_THROW(std::ignore = client.ess.CreateSecret("db-password", ""), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

    // The one call that answers with a value, and so the point at which it enters the process.
    BOOST_AUTO_TEST_CASE(ReadsASecretsValueAndTheMetadataAroundIt) {
        const auto answer = boost::json::serialize(boost::json::object{{"value", "hunter2"}, {"secret", secret(3)}});
        const EssClient client(Test::Answering(answer));

        const auto read = client.ess.GetSecret("db-password");
        BOOST_TEST(read.value == "hunter2");
        BOOST_TEST(read.secret.name == "db-password");
        BOOST_TEST(read.secret.version == 3L);
        BOOST_TEST(read.secret.rotated == "2026-09-01T08:00:00Z");
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "db-password");
    }

    BOOST_AUTO_TEST_CASE(ListsSecretsWithoutTheirValues) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"total", 2},
                {"secrets", boost::json::array{secret(), boost::json::object{{"name", "api-token"}, {"version", 7}}}}});

        const EssClient client(Test::Answering(listed));

        const auto page = client.ess.ListSecrets({.prefix = "db", .pageSize = 25});
        BOOST_TEST(page.total == 2L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[0].name == "db-password");
        BOOST_TEST(page.items[0].encryptionKeyErn == kKeyErn);
        BOOST_TEST(page.items[1].version == 7L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("prefix").as_string() == "db");
        BOOST_TEST(body.at("sortColumn").as_string() == "name");
    }

    BOOST_AUTO_TEST_CASE(DeletesASecret) {
        const EssClient client(Test::Answering(R"({"name": "db-password", "ern": "ern:secret/db-password"})"));

        const auto deleted = client.ess.DeleteSecret("db-password");
        BOOST_TEST(deleted.name == "db-password");
        BOOST_TEST(deleted.ern == "ern:secret/db-password");
    }

    // Tagging answers with the secret as it now reads, metadata only.
    BOOST_AUTO_TEST_CASE(TagsASecretAndAnswersWithItAsItNowReads) {
        const EssClient client(Test::Answering(secretResponse()));

        const auto tagged = client.ess.AddSecretTag("db-password", "team", "finance");
        BOOST_TEST(tagged.tags.at("team") == "finance");
        auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "db-password");
        BOOST_TEST(body.at("value").as_string() == "finance");

        std::ignore = client.ess.DeleteSecretTag("db-password", "team");
        body = lastBody(client.gateway);
        BOOST_TEST(body.at("key").as_string() == "team");
        BOOST_TEST(!body.as_object().contains("value"));
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EssUpdateTest)

    // A new value is what a rotation is, and what bumps the version.
    BOOST_AUTO_TEST_CASE(RotatingSendsNothingButTheNewValue) {
        const EssClient client(Test::Answering(secretResponse(2)));

        const auto rotated = client.ess.RotateSecret("db-password", "hunter3");
        BOOST_TEST(rotated.version == 2L);

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("name").as_string() == "db-password");
        BOOST_TEST(body.at("value").as_string() == "hunter3");
        BOOST_TEST(!body.contains("description"));
        BOOST_TEST(!body.contains("keyErn"));
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "update-secret");
    }

    // Sent when it was named, empty included: an empty description clears the stored one.
    BOOST_AUTO_TEST_CASE(SendsAnEmptyDescriptionThatWasAskedFor) {
        const EssClient client(Test::Answering(secretResponse()));

        std::ignore = client.ess.UpdateSecret("db-password", {.description = ""});

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.contains("description"));
        BOOST_TEST(body.at("description").as_string() == "");
        BOOST_TEST(!body.contains("value"));
    }

    // Moving a secret to another key changes how it is protected, not what it is.
    BOOST_AUTO_TEST_CASE(MovesASecretOntoAnotherKey) {
        const EssClient client(Test::Answering(secretResponse()));

        std::ignore = client.ess.UpdateSecret("db-password", {.keyErn = kKeyErn});

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("keyErn").as_string() == kKeyErn);
        BOOST_TEST(!body.contains("value"));
        BOOST_TEST(!body.contains("description"));
    }

    // An empty key is not a key to move onto, so it means "leave it where it is" - which leaves an
    // update with nothing to do at all.
    BOOST_AUTO_TEST_CASE(RefusesAnUpdateThatChangesNothing) {
        const EssClient client(Test::Answering(secretResponse()));

        BOOST_CHECK_THROW(std::ignore = client.ess.UpdateSecret("db-password", {}), EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.ess.UpdateSecret("db-password", {.keyErn = ""}), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

    // The server refuses a value it was given but that is empty; so does this, one round trip earlier.
    BOOST_AUTO_TEST_CASE(RefusesAnEmptyValueOnAnUpdate) {
        const EssClient client(Test::Answering(secretResponse()));

        BOOST_CHECK_THROW(std::ignore = client.ess.UpdateSecret("db-password", {.value = ""}), EuclidError);
        BOOST_TEST(client.gateway.Received().size() == 1U);
    }

    BOOST_AUTO_TEST_CASE(ChangesEverythingAtOnceWhenAskedTo) {
        const EssClient client(Test::Answering(secretResponse(4)));

        const auto updated = client.ess.UpdateSecret("db-password", {.value = "hunter4",
                                                                     .description = "the reporting replica",
                                                                     .keyErn = kKeyErn});
        BOOST_TEST(updated.version == 4L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("value").as_string() == "hunter4");
        BOOST_TEST(body.at("description").as_string() == "the reporting replica");
        BOOST_TEST(body.at("keyErn").as_string() == kKeyErn);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EssTransportTest)

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EssClient client(Test::Answering(R"({"total": 0, "secrets": []})"));

        client.session.ChangeNamespace("reports");
        std::ignore = client.ess.ListSecrets();

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "ess");
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "reports");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

    // A secret under a key that has gone is the failure this module has that others do not.
    BOOST_AUTO_TEST_CASE(CarriesTheServersReasonWhenAValueCannotBeDecrypted) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(500, R"({"error": "The stored value could not be decrypted with the key it names"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const ESS::Ess ess(session);

        try {
            std::ignore = ess.GetSecret("db-password");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "ess");
            BOOST_TEST(ex.Action() == "get-secret");
            BOOST_TEST(ex.Status() == 500);
            BOOST_TEST(ex.Reason() == "The stored value could not be decrypted with the key it names");
        }
    }

    BOOST_AUTO_TEST_CASE(ReachesItsMetricsAndAnActionThisSdkDoesNotWrap) {
        const EssClient client(Test::Answering(R"({"ess_secret_count": 5, "whatever": 42})"));

        BOOST_TEST(client.ess.Metrics().at("ess_secret_count").as_int64() == 5);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");

        BOOST_TEST(client.ess.Call("some-future-action").at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

BOOST_AUTO_TEST_SUITE_END()
