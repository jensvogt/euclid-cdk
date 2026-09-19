// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EKM, end to end against a fake euclid server.
 *
 * @par
 * Two things are worth more attention here than the request shapes: that a key's ID and its ERN go
 * to the actions that take each - they are not interchangeable, and the server would not tell a
 * caller which one it wanted - and that encrypt and decrypt carry raw bytes rather than JSON.
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
#include <euclid/cdk/ekm/Ekm.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    const std::string kKeyId = "11111111-1111-1111-1111-111111111111";
    const std::string kKeyErn = "ern:euclid:ekm:eu-central-1:000000000000:key/" + kKeyId;

    /**
     * @brief A gateway, a logged-in session and an EKM client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EkmClient {

        explicit EkmClient(FakeGateway::Handler handler, const EAM::AuthMode auth = EAM::AuthMode::Auto)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Auth(auth).Login()), ekm(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EKM::Ekm ekm;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    Request requestFor(const FakeGateway &gateway, const std::string &action) {
        for (const auto &request: gateway.Received()) {
            if (std::string(request["x-euclid-action"]) == action) return request;
        }
        return {};
    }

    std::string certificateResponse() {
        return boost::json::serialize(boost::json::object{
                {"certificate", boost::json::object{
                                        {"name", "listener"},
                                        {"ern", "ern:euclid:ekm:eu-central-1:000000000000:certificate/listener"},
                                        {"description", "the HTTPS listener"},
                                        {"certificate", "-----BEGIN CERTIFICATE-----\nMIIB...\n-----END CERTIFICATE-----\n"},
                                        {"subject", "CN=euclid.example.com"},
                                        {"issuer", "CN=euclid.example.com"},
                                        {"serialNumber", "0a1b2c"},
                                        {"fingerprint", "AA:BB:CC"},
                                        {"subjectAltNames", boost::json::array{"euclid.example.com", "localhost"}},
                                        {"generated", true},
                                        {"notBefore", "2026-01-01T00:00:00Z"},
                                        {"notAfter", "2028-04-05T00:00:00Z"},
                                        {"tags", boost::json::object{{"purpose", "listener"}}},
                                        {"created", "2026-01-01T00:00:00Z"}}}});
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EkmKeyTest)

    BOOST_AUTO_TEST_CASE(CreatesAKeyWithTheDefaultsItDocuments) {
        const EkmClient client(Test::Answering(R"({"name": "11111111-1111-1111-1111-111111111111",
                                                   "ern": "ern:key/1", "description": "customer exports",
                                                   "algorithm": "AES", "length": 256, "status": "AVAILABLE"})"));

        const auto created = client.ekm.CreateKey({.description = "customer exports"});
        BOOST_TEST(created.name == kKeyId);
        BOOST_TEST(created.length == 256L);
        BOOST_TEST(created.status == std::string(EKM::KeyAvailable));

        // 256 rather than 128: what euclid itself creates when a bucket asks to be encrypted.
        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("algorithm").as_string() == std::string(EKM::Aes));
        BOOST_TEST(body.at("length").as_int64() == EKM::DefaultKeyLength);
        BOOST_TEST(body.at("description").as_string() == "customer exports");
    }

    BOOST_AUTO_TEST_CASE(ListsKeysWithoutTheirMaterial) {
        const auto keys = boost::json::serialize(boost::json::object{
                {"total", 2},
                {"keys", boost::json::array{
                                 boost::json::object{
                                         {"name", kKeyId},
                                         {"ern", kKeyErn},
                                         {"description", "customer exports"},
                                         {"algorithm", "AES"},
                                         {"length", 256},
                                         {"status", "AVAILABLE"},
                                         {"tags", boost::json::object{{"team", "finance"}}},
                                         {"created", "2026-01-01T00:00:00Z"}},
                                 boost::json::object{
                                         {"name", "22222222-2222-2222-2222-222222222222"},
                                         {"status", "PENDING_DELETION"},
                                         {"deletionDate", "2026-09-19T10:00:00Z"}}}}});

        const EkmClient client(Test::Answering(keys));

        const auto page = client.ekm.ListKeys({.prefix = "1111"});
        BOOST_TEST(page.total == 2L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[0].name == kKeyId);
        BOOST_TEST(page.items[0].ern == kKeyErn);
        BOOST_TEST(page.items[0].tags.at("team") == "finance");
        BOOST_TEST(page.items[0].deletionDate.empty());
        BOOST_TEST(page.items[1].status == std::string(EKM::KeyPendingDeletion));
        BOOST_TEST(page.items[1].deletionDate == "2026-09-19T10:00:00Z");

        BOOST_TEST(lastBody(client.gateway).at("sortColumn").as_string() == "name");
    }

    BOOST_AUTO_TEST_CASE(GetsOneKeyByNameWithoutItsMaterial) {
        const auto key = boost::json::serialize(boost::json::object{
                {"key", boost::json::object{
                                {"name", kKeyId},
                                {"ern", kKeyErn},
                                {"description", "customer exports"},
                                {"algorithm", "AES"},
                                {"length", 256},
                                {"status", "AVAILABLE"},
                                {"tags", boost::json::object{{"team", "finance"}}},
                                {"created", "2026-01-01T00:00:00Z"}}}});

        const EkmClient client(Test::Answering(key));

        const auto fetched = client.ekm.GetKey(kKeyId);
        BOOST_TEST(fetched.name == kKeyId);
        BOOST_TEST(fetched.ern == kKeyErn);
        BOOST_TEST(fetched.length == 256L);
        BOOST_TEST(fetched.tags.at("team") == "finance");
        // What this returns is the key's description; the material never leaves the module, so
        // there is no field here that could carry it.
        BOOST_TEST(fetched.deletionDate.empty());

        // kKeyId is a name rather than an ERN, so that is the field it goes in - and the other one
        // is left out rather than sent empty.
        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("name").as_string() == kKeyId);
        BOOST_TEST(!body.contains("ern"));
    }

    // A name is resolved in the session's own namespace and an ERN is not, so which of the two was
    // given has to reach the server as the field it is - the caller should not have to say.
    BOOST_AUTO_TEST_CASE(GetsAKeyByErnWhenGivenOne) {
        const EkmClient client(Test::Answering(R"({"key": {"name": "key-1", "ern": "ern:key/1"}})"));

        std::ignore = client.ekm.GetKey(kKeyErn);

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("ern").as_string() == kKeyErn);
        BOOST_TEST(!body.contains("name"));
    }

    // Scheduled rather than immediate: the window is the only chance anybody gets to notice.
    BOOST_AUTO_TEST_CASE(SchedulesAKeyForDeletionRatherThanDeletingIt) {
        const EkmClient client(Test::Answering(R"({"name": "11111111-1111-1111-1111-111111111111", "ern": "ern:key/1",
                                                   "deletionDate": "2026-09-19T10:00:00Z", "status": "PENDING_DELETION"})"));

        const auto deleted = client.ekm.DeleteKey(kKeyId);
        BOOST_TEST(deleted.deletionDate == "2026-09-19T10:00:00Z");
        BOOST_TEST(deleted.status == std::string(EKM::KeyPendingDeletion));

        // By ID, and with the grace period spelled out rather than left to the server.
        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("keyId").as_string() == kKeyId);
        BOOST_TEST(body.at("pendingWindowInDays").as_int64() == EKM::DefaultPendingWindowDays);
        BOOST_TEST(!body.contains("ern"));
    }

    // Revoking takes the ERN where deleting takes the ID - the one thing about EKM worth remembering.
    BOOST_AUTO_TEST_CASE(RevokesAndDescribesAKeyByItsErn) {
        const EkmClient client(Test::AnsweringByAction({
                {"revoke-key", R"({"name": "11111111-1111-1111-1111-111111111111", "ern": "ern:key/1", "status": "REVOKED"})"},
                {"set-key-description", R"({"name": "11111111-1111-1111-1111-111111111111", "ern": "ern:key/1", "description": ""})"},
        }));

        const auto revoked = client.ekm.RevokeKey(kKeyErn);
        BOOST_TEST(revoked.status == std::string(EKM::KeyRevoked));
        auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("ern").as_string() == kKeyErn);
        BOOST_TEST(!body.contains("keyId"));

        // An empty description clears it rather than leaving it alone.
        const auto described = client.ekm.SetKeyDescription(kKeyErn, "");
        BOOST_TEST(described.description.empty());
        body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("ern").as_string() == kKeyErn);
        BOOST_TEST(body.at("description").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(TagsAKey) {
        const EkmClient client(Test::Answering());

        client.ekm.AddKeyTag(kKeyErn, "team", "finance");
        auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("ern").as_string() == kKeyErn);
        BOOST_TEST(body.at("value").as_string() == "finance");

        client.ekm.DeleteKeyTag(kKeyErn, "team");
        body = lastBody(client.gateway);
        BOOST_TEST(body.at("key").as_string() == "team");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EkmCryptoTest)

    // The bytes go to the key, rather than the key coming to the bytes.
    BOOST_AUTO_TEST_CASE(EncryptsAndDecryptsThroughTheServer) {
        const std::string sealed = "\x01\x02\x03 IV||ciphertext||tag";
        const EkmClient client(Test::Authenticated([&sealed](const Request &request) {
            const auto action = std::string(request["x-euclid-action"]);
            if (action == "encrypt") return FakeGateway::Bytes(200, sealed);
            if (action == "decrypt") return FakeGateway::Bytes(200, "account 4711");
            return FakeGateway::Json(200, "{}");
        }));

        BOOST_TEST(client.ekm.Encrypt(kKeyId, "account 4711") == sealed);

        const auto request = requestFor(client.gateway, "encrypt");
        BOOST_TEST(std::string(request["x-euclid-key-id"]) == kKeyId);
        BOOST_TEST(std::string(request[boost::beast::http::field::content_type]) == "application/octet-stream");
        BOOST_TEST(request.body() == "account 4711");

        // Exactly what encrypt answered with goes back, unpacked by nobody.
        BOOST_TEST(client.ekm.Decrypt(kKeyId, sealed) == "account 4711");
        BOOST_TEST(client.gateway.LastRequest().body() == sealed);
    }

    // The byte-carrying actions present the token, as in every euclid client.
    BOOST_AUTO_TEST_CASE(PresentsTheTokenForTheByteActionsAndSignsTheRest) {
        const EkmClient client(Test::Authenticated([](const Request &request) {
            if (std::string(request["x-euclid-action"]) == "encrypt") return FakeGateway::Bytes(200, "sealed");
            return FakeGateway::Json(200, R"({"total": 0, "keys": []})");
        }));

        std::ignore = client.ekm.Encrypt(kKeyId, "plain");
        const auto bytes = requestFor(client.gateway, "encrypt");
        BOOST_TEST(SigningScheme::Of(bytes) == nullptr);
        BOOST_TEST(std::string(bytes[boost::beast::http::field::authorization]) == "Bearer " + client.session.Token());

        std::ignore = client.ekm.ListKeys();
        BOOST_REQUIRE(SigningScheme::Of(client.gateway.LastRequest()) != nullptr);
    }

    // A session that asked not to be handed a token silently is not handed one here either.
    BOOST_AUTO_TEST_CASE(SignsTheBytesTooWhenTheSessionAskedForSignatures) {
        const EkmClient client(Test::Authenticated([](const Request &) { return FakeGateway::Bytes(200, "sealed"); }),
                               EAM::AuthMode::Signature);

        std::ignore = client.ekm.Encrypt(kKeyId, "plain");
        BOOST_REQUIRE(SigningScheme::Of(requestFor(client.gateway, "encrypt")) != nullptr);
    }

    // Only an available key encrypts; a revoked one is refused, and the refusal has to say so.
    BOOST_AUTO_TEST_CASE(CarriesTheServersReasonWhenAKeyMayNotEncrypt) {
        const EkmClient client(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(403, R"({"error": "Key is not available, status: REVOKED"})");
        }));

        try {
            std::ignore = client.ekm.Encrypt(kKeyId, "plain");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "ekm");
            BOOST_TEST(ex.Action() == "encrypt");
            BOOST_TEST(ex.Status() == 403);
            BOOST_TEST(ex.Reason() == "Key is not available, status: REVOKED");
        }
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EkmCertificateTest)

    BOOST_AUTO_TEST_CASE(ImportsACertificateWithThePrivateKeyThatProvesIt) {
        const EkmClient client(Test::Answering(certificateResponse()));

        const auto certificate = client.ekm.ImportCertificate("listener", "-----BEGIN CERTIFICATE-----\n",
                                                              "-----BEGIN PRIVATE KEY-----\n", "the HTTPS listener");
        BOOST_TEST(certificate.name == "listener");
        BOOST_TEST(certificate.subject == "CN=euclid.example.com");
        BOOST_REQUIRE(certificate.subjectAltNames.size() == 2U);
        BOOST_TEST(certificate.subjectAltNames[1] == "localhost");
        BOOST_TEST(certificate.tags.at("purpose") == "listener");
        BOOST_TEST(certificate.generated);

        // Both halves travel; only one of them ever comes back.
        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("certificate").as_string() == "-----BEGIN CERTIFICATE-----\n");
        BOOST_TEST(body.at("privateKey").as_string() == "-----BEGIN PRIVATE KEY-----\n");
        BOOST_TEST(body.at("description").as_string() == "the HTTPS listener");
    }

    // Left out rather than sent as zero, so the server's own 825 days and 2048 bits apply.
    BOOST_AUTO_TEST_CASE(LeavesTheServersOwnDefaultsOutOfAGeneratedCertificate) {
        const EkmClient client(Test::Answering(certificateResponse()));

        std::ignore = client.ekm.CreateCertificate("listener", {.commonName = "euclid.example.com",
                                                                .subjectAltNames = {"localhost"}});

        auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("commonName").as_string() == "euclid.example.com");
        BOOST_TEST(body.at("subjectAltNames").as_array().size() == 1U);
        BOOST_TEST(!body.contains("validDays"));
        BOOST_TEST(!body.contains("keyBits"));

        std::ignore = client.ekm.CreateCertificate("listener", {.validDays = 90, .keyBits = 4096});
        body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.at("validDays").as_int64() == 90);
        BOOST_TEST(body.at("keyBits").as_int64() == 4096);
    }

    BOOST_AUTO_TEST_CASE(ReadsAndListsCertificates) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"total", 1},
                {"certificates", boost::json::array{boost::json::object{
                                         {"name", "listener"},
                                         {"subject", "CN=euclid.example.com"},
                                         {"generated", false},
                                         {"notAfter", "2028-04-05T00:00:00Z"}}}}});

        const EkmClient client(Test::AnsweringByAction({{"get-certificate", certificateResponse()},
                                                        {"list-certificates", listed}}));

        const auto one = client.ekm.GetCertificate("listener");
        BOOST_TEST(one.fingerprint == "AA:BB:CC");
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "listener");

        const auto page = client.ekm.ListCertificates();
        BOOST_TEST(page.total == 1L);
        BOOST_REQUIRE(page.items.size() == 1U);
        BOOST_TEST(page.items[0].name == "listener");
        // Nobody vouched for the one above; this one somebody did.
        BOOST_TEST(!page.items[0].generated);
    }

    BOOST_AUTO_TEST_CASE(DeletesACertificateOutright) {
        const EkmClient client(Test::Answering(R"({"name": "listener", "ern": "ern:certificate/listener"})"));

        const auto deleted = client.ekm.DeleteCertificate("listener");
        BOOST_TEST(deleted.name == "listener");
        BOOST_TEST(deleted.ern == "ern:certificate/listener");
        // No grace period to ask for: a certificate is public, so nothing becomes unreadable.
        BOOST_TEST(!lastBody(client.gateway).as_object().contains("pendingWindowInDays"));
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EkmTransportTest)

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EkmClient client(Test::Answering(R"({"total": 0, "keys": []})"));

        client.session.ChangeNamespace("reports");
        std::ignore = client.ekm.ListKeys();

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "ekm");
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "reports");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

    // EKM answers get-metrics with a 404, so this SDK has no Metrics() for it - but Call() is there
    // for the day it gains one, or for any other action.
    BOOST_AUTO_TEST_CASE(CallReachesAnActionThisSdkDoesNotWrap) {
        const EkmClient client(Test::Answering(R"({"whatever": 42})"));

        BOOST_TEST(client.ekm.Call("some-future-action", {{"argument", "value"}}).at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-target"]) == "ekm");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

BOOST_AUTO_TEST_SUITE_END()
