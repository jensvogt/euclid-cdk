// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <chrono>
#include <string>
#include <tuple>

// Boost includes
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Credentials.h>
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Url.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/eam/Eam.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;
using Euclid::CDK::Test::FutureToken;
using Euclid::CDK::Test::LoginResponse;

namespace {

    constexpr auto kAccessKeyId = Test::AccessKeyId;

    std::string usersResponse() {
        return boost::json::serialize(boost::json::object{
                {"total", 2},
                {"users", boost::json::array{
                                  boost::json::object{
                                          {"userId", "jens"},
                                          {"ern", "ern:euclid:eam:eu-central-1:000000000000:user/jens"},
                                          {"email", "jens@example.com"},
                                          {"accountId", "000000000000"},
                                          {"region", "eu-central-1"},
                                          {"accountGrants", boost::json::array{boost::json::object{
                                                                    {"accountId", "000000000000"},
                                                                    {"namespaces", boost::json::array{"reports", "archive"}},
                                                                    {"isAdmin", true},
                                                                    {"granted", "2026-09-01T08:00:00Z"}}}},
                                          {"created", "2026-09-01T08:00:00Z"},
                                          {"modified", "2026-09-01T08:00:00Z"}},
                                  boost::json::object{{"userId", "jill"}, {"email", "jill@example.com"}}}},
        });
    }

    // Answers login, verifies the signature on everything else the way the server does, and hands
    // back one canned body - see Test::Authenticated().
    FakeGateway::Handler gatewayHandler(const std::string &body = {}) {
        return Test::Answering(body);
    }

    EAM::Eam builder(const FakeGateway &gateway) {
        return Test::Builder(gateway);
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EamLoginTest)

    BOOST_AUTO_TEST_CASE(LoginCarriesTheServersAnswerIntoTheSession) {
        const FakeGateway gateway(gatewayHandler());
        const auto session = builder(gateway).Login();

        BOOST_TEST(session.UserId() == "jens");
        BOOST_TEST(session.AccountId() == "000000000000");
        BOOST_TEST(session.Region() == "eu-central-1");
        BOOST_TEST(session.AccessKeyId() == kAccessKeyId);
        BOOST_TEST(session.SecretAccessKey() == Test::SecretAccessKey);
        BOOST_TEST(session.IsAdmin());
        BOOST_TEST(session.Namespace().empty());
    }

    // The server resolves the user by ID first and only falls back to the email, so sending both
    // would silently ignore the email.
    BOOST_AUTO_TEST_CASE(LoginSendsOneIdentifier) {
        const FakeGateway gateway(gatewayHandler());
        std::ignore = EAM::Eam::ForServer(gateway.BaseUrl()).UseCache(false).Username("jens").Email("jens@example.com").Password("secret").Login();

        const auto body = boost::json::parse(gateway.LastRequest().body());
        BOOST_TEST(body.at("userId").as_string() == "jens");
        BOOST_TEST(body.at("email").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(LoginSendsTheEmailWhenThereIsNoUserId) {
        const FakeGateway gateway(gatewayHandler());
        std::ignore = EAM::Eam::ForServer(gateway.BaseUrl()).UseCache(false).Email("jens@example.com").Password("secret").Login();

        const auto body = boost::json::parse(gateway.LastRequest().body());
        BOOST_TEST(body.at("userId").as_string() == "");
        BOOST_TEST(body.at("email").as_string() == "jens@example.com");
    }

    // Login is the one call that cannot be signed: it is what fetches the key to sign with.
    BOOST_AUTO_TEST_CASE(LoginIsNotSigned) {
        const FakeGateway gateway(gatewayHandler());
        std::ignore = builder(gateway).Login();

        const auto request = gateway.LastRequest();
        BOOST_TEST(SigningScheme::Of(request) == nullptr);
        BOOST_TEST(std::string(request["x-euclid-target"]) == "eam");
        BOOST_TEST(std::string(request["x-euclid-action"]) == "login");
    }

    BOOST_AUTO_TEST_CASE(RefusedCredentialsThrowWithTheServersOwnReason) {
        const FakeGateway gateway([](const Request &) { return FakeGateway::Json(401, R"({"error": "wrong password"})"); });

        try {
            std::ignore = builder(gateway).Login();
            BOOST_FAIL("expected an AuthenticationError");
        } catch (const AuthenticationError &ex) {
            BOOST_TEST(ex.Status() == 401);
            BOOST_TEST(ex.Reason() == "wrong password");
            BOOST_TEST(std::string(ex.what()) == "login failed with HTTP 401: wrong password");
        }
    }

    BOOST_AUTO_TEST_CASE(IncompleteCredentialsNeverReachTheServer) {
        const FakeGateway gateway(gatewayHandler());
        BOOST_CHECK_THROW(std::ignore = EAM::Eam::ForServer(gateway.BaseUrl()).UseCache(false).Password("secret").Login(), EuclidError);
        BOOST_CHECK_THROW(std::ignore = EAM::Eam::ForServer(gateway.BaseUrl()).UseCache(false).Username("jens").Login(), EuclidError);
        BOOST_TEST(gateway.Received().empty());
    }

    BOOST_AUTO_TEST_CASE(AnEmptyServerUrlIsRefusedUpFront) {
        BOOST_CHECK_THROW(std::ignore = EAM::Eam::ForServer(""), EuclidError);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EamSessionTest)

    BOOST_AUTO_TEST_CASE(SignsEveryCallAfterTheLogin) {
        const FakeGateway gateway(gatewayHandler(usersResponse()));
        const auto session = builder(gateway).Login();

        const auto page = session.ListUsers();

        // The gateway answered at all, which it only does for a signature it could verify.
        BOOST_TEST(page.total == 2L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[0].userId == "jens");
        BOOST_TEST(page.items[0].email == "jens@example.com");
        BOOST_REQUIRE(page.items[0].accountGrants.size() == 1U);
        BOOST_TEST(page.items[0].accountGrants[0].isAdmin);
        BOOST_TEST(page.items[0].accountGrants[0].namespaces.size() == 2U);
        BOOST_TEST(page.items[1].userId == "jill");

        const auto request = gateway.LastRequest();
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
        BOOST_TEST(SigningScheme::Of(request)->Name() == "rfc9421");
    }

    // The same session, the same server, the other scheme - which is the point of being able to
    // move one deployment at a time.
    BOOST_AUTO_TEST_CASE(SignsWithSigV4WhenAskedTo) {
        const FakeGateway gateway(gatewayHandler(usersResponse()));
        const auto session = builder(gateway).Scheme(SigningScheme::SigV4()).Login();

        BOOST_TEST(session.ListUsers().total == 2L);
        BOOST_REQUIRE(SigningScheme::Of(gateway.LastRequest()) != nullptr);
        BOOST_TEST(SigningScheme::Of(gateway.LastRequest())->Name() == "sigv4");
    }

    BOOST_AUTO_TEST_CASE(PresentsTheTokenWhenAskedTo) {
        const FakeGateway gateway(gatewayHandler(usersResponse()));
        const auto session = builder(gateway).Auth(EAM::AuthMode::Bearer).Login();

        BOOST_TEST(session.ListUsers().total == 2L);
        const auto request = gateway.LastRequest();
        BOOST_TEST(SigningScheme::Of(request) == nullptr);
        BOOST_TEST(std::string(request[boost::beast::http::field::authorization]) == "Bearer " + session.Token());
    }

    BOOST_AUTO_TEST_CASE(SendsTheRoutingHeadersEverySignatureCovers) {
        const FakeGateway gateway(gatewayHandler(usersResponse()));
        const auto session = builder(gateway).Login();
        std::ignore = session.ListUsers();

        const auto request = gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "eam");
        BOOST_TEST(std::string(request["x-euclid-action"]) == "list-users");
        BOOST_TEST(std::string(request["x-euclid-region"]) == "eu-central-1");
        BOOST_TEST(std::string(request["x-euclid-account-id"]) == "000000000000");
        BOOST_TEST(std::string(request["x-euclid-user-id"]) == "jens");
    }

    // The Host the signature covers has to be the Host that goes on the wire, port included.
    BOOST_AUTO_TEST_CASE(SendsTheHostTheSignatureCovers) {
        const FakeGateway gateway(gatewayHandler(usersResponse()));
        const auto session = builder(gateway).Login();
        std::ignore = session.ListUsers();

        BOOST_TEST(std::string(gateway.LastRequest()[boost::beast::http::field::host]) == Url::HostHeaderOf(gateway.BaseUrl()));
    }

    BOOST_AUTO_TEST_CASE(FillsInTheServersPagingDefaults) {
        const FakeGateway gateway(gatewayHandler(usersResponse()));
        const auto session = builder(gateway).Login();
        std::ignore = session.ListUsers({.prefix = "j", .pageSize = 25});

        const auto body = boost::json::parse(gateway.LastRequest().body());
        BOOST_TEST(body.at("prefix").as_string() == "j");
        BOOST_TEST(body.at("pageSize").as_int64() == 25);
        BOOST_TEST(body.at("pageIndex").as_int64() == 0);
        BOOST_TEST(body.at("sortColumn").as_string() == "userId");
        BOOST_TEST(body.at("sortDirection").as_string() == "asc");
    }

    BOOST_AUTO_TEST_CASE(ChangingTheNamespaceScopesLaterCalls) {
        const FakeGateway gateway(gatewayHandler(usersResponse()));
        auto session = builder(gateway).Login();

        session.ChangeNamespace("reports");
        BOOST_TEST(session.Namespace() == "reports");
        BOOST_TEST(boost::json::parse(gateway.LastRequest().body()).at("namespace").as_string() == "reports");

        std::ignore = session.ListUsers();
        BOOST_TEST(std::string(gateway.LastRequest()["x-euclid-namespace"]) == "reports");
    }

    BOOST_AUTO_TEST_CASE(RegisterDefaultsToTheSessionsOwnAccountAndRegion) {
        const FakeGateway gateway(gatewayHandler(R"({"user": {"userId": "jill"}})"));
        const auto session = builder(gateway).Login();

        const auto user = session.Register("jill", "secret", {.email = "jill@example.com"});
        BOOST_TEST(user.userId == "jill");

        const auto body = boost::json::parse(gateway.LastRequest().body());
        BOOST_TEST(body.at("accountId").as_string() == "000000000000");
        BOOST_TEST(body.at("region").as_string() == "eu-central-1");
        BOOST_TEST(body.at("email").as_string() == "jill@example.com");
        BOOST_TEST(body.at("isAdmin").as_bool() == false);
    }

    BOOST_AUTO_TEST_CASE(ReadsACreatedAccessKey) {
        const FakeGateway gateway(gatewayHandler(R"({"accessKeyId": "AKIANEW", "secretAccessKey": "s3cr3t", "createdAt": "2026-09-12T10:00:00Z"})"));
        const auto session = builder(gateway).Login();

        const auto key = session.CreateAccessKey();
        BOOST_TEST(key.accessKeyId == "AKIANEW");
        BOOST_TEST(key.secretAccessKey == "s3cr3t");
    }

    BOOST_AUTO_TEST_CASE(ReadsAccessKeysWithoutTheirSecrets) {
        const FakeGateway gateway(gatewayHandler(R"({"accessKeys": [{"accessKeyId": "AKIA1", "active": true}, {"accessKeyId": "AKIA2", "active": false}]})"));
        const auto session = builder(gateway).Login();

        const auto keys = session.ListAccessKeys();
        BOOST_REQUIRE(keys.size() == 2U);
        BOOST_TEST(keys[0].accessKeyId == "AKIA1");
        BOOST_TEST(keys[0].active);
        BOOST_TEST(!keys[1].active);
    }

    BOOST_AUTO_TEST_CASE(ReadsNamespacesOfAnAccount) {
        const FakeGateway gateway(gatewayHandler(R"({"total": 1, "namespaces": [{"accountId": "000000000000", "name": "reports"}]})"));
        const auto session = builder(gateway).Login();

        const auto page = session.ListNamespaces("000000000000", {.prefix = "rep"});
        BOOST_TEST(page.total == 1L);
        BOOST_REQUIRE(page.items.size() == 1U);
        BOOST_TEST(page.items[0].name == "reports");
        BOOST_TEST(boost::json::parse(gateway.LastRequest().body()).at("accountId").as_string() == "000000000000");
    }

    BOOST_AUTO_TEST_CASE(ARefusedActionThrowsWithTheTargetAndAction) {
        const FakeGateway gateway([](const Request &request) {
            if (std::string(request["x-euclid-action"]) == "login") return FakeGateway::Json(200, LoginResponse());
            return FakeGateway::Json(403, R"({"error": "not an administrator"})");
        });
        const auto session = builder(gateway).Login();

        try {
            std::ignore = session.ListUsers();
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "eam");
            BOOST_TEST(ex.Action() == "list-users");
            BOOST_TEST(ex.Status() == 403);
            BOOST_TEST(ex.Reason() == "not an administrator");
        }
    }

    // A body that is not JSON is what a proxy in front of euclid answers with, and quoting it is
    // more useful than saying it could not be parsed.
    BOOST_AUTO_TEST_CASE(ANonJsonFailureBodyIsQuotedVerbatim) {
        const FakeGateway gateway([](const Request &request) {
            if (std::string(request["x-euclid-action"]) == "login") return FakeGateway::Json(200, LoginResponse());
            return FakeGateway::Json(502, "<html>Bad Gateway</html>");
        });
        const auto session = builder(gateway).Login();

        try {
            std::ignore = session.ListUsers();
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Reason() == "<html>Bad Gateway</html>");
        }
    }

    BOOST_AUTO_TEST_CASE(AnUnreachableServerThrowsRatherThanHangs) {
        // Port 1 on the loopback interface: nothing listens there, and the connection is refused
        // rather than dropped, so this fails fast instead of waiting out the timeout.
        auto unreachable = EAM::Eam::ForServer("http://127.0.0.1:1").UseCache(false).Credentials("jens", "secret");
        BOOST_CHECK_THROW(std::ignore = unreachable.Login(), EuclidError);
    }

    // Reaching an action this SDK does not wrap should not need a release.
    BOOST_AUTO_TEST_CASE(CallReachesAnyAction) {
        const FakeGateway gateway(gatewayHandler(R"({"whatever": 42})"));
        const auto session = builder(gateway).Login();

        const auto answer = session.Call("some-future-action", {{"argument", "value"}});
        BOOST_TEST(answer.at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

    BOOST_AUTO_TEST_CASE(TheTokenProviderIsAskedPerRequest) {
        const FakeGateway gateway(gatewayHandler(usersResponse()));
        auto session = builder(gateway).Auth(EAM::AuthMode::Bearer).Login();

        session.SetTokenProvider([] { return std::string("rotated-token"); });
        std::ignore = session.ListUsers();

        BOOST_TEST(std::string(gateway.LastRequest()[boost::beast::http::field::authorization]) == "Bearer rotated-token");
    }

    // A caller who asked for signatures is told there is no key, rather than quietly handed a token.
    BOOST_AUTO_TEST_CASE(SignatureModeWithoutAKeyFailsLoudly) {
        const FakeGateway gateway([](const Request &) {
            return FakeGateway::Json(200, boost::json::serialize(boost::json::object{
                                                  {"token", FutureToken()},
                                                  {"metadata", boost::json::object{{"region", "eu-central-1"}, {"accountId", "000000000000"}, {"user", "jens"}}}}));
        });
        const auto session = builder(gateway).Auth(EAM::AuthMode::Signature).Login();

        BOOST_TEST(session.AccessKeyId().empty());
        BOOST_CHECK_THROW(std::ignore = session.ListUsers(), EuclidError);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EamCacheTest)

    // The cache is what makes a login shared with euclid-cli and the other SDKs: the second login
    // never reaches the server.
    BOOST_AUTO_TEST_CASE(ASecondLoginIsServedFromTheCache) {
        Credentials::Clear();
        const FakeGateway gateway(gatewayHandler(usersResponse()));

        const auto first = EAM::Eam::ForServer(gateway.BaseUrl()).Credentials("jens", "secret").Login();
        BOOST_TEST(gateway.Received().size() == 1U);

        const auto second = EAM::Eam::ForServer(gateway.BaseUrl()).Credentials("jens", "secret").Login();
        BOOST_TEST(gateway.Received().size() == 1U);
        BOOST_TEST(second.Token() == first.Token());
        BOOST_TEST(second.AccessKeyId() == first.AccessKeyId());

        Credentials::Clear();
    }

    // A session cached for one server must never be presented to another.
    BOOST_AUTO_TEST_CASE(ACacheEntryForAnotherServerIsNotReused) {
        Credentials::Clear();
        const FakeGateway first(gatewayHandler());
        const FakeGateway second(gatewayHandler());

        std::ignore = EAM::Eam::ForServer(first.BaseUrl()).Credentials("jens", "secret").Login();
        std::ignore = EAM::Eam::ForServer(second.BaseUrl()).Credentials("jens", "secret").Login();

        BOOST_TEST(second.Received().size() == 1U);
        Credentials::Clear();
    }

    BOOST_AUTO_TEST_CASE(UseCacheFalseNeitherReadsNorWrites) {
        Credentials::Clear();
        const FakeGateway gateway(gatewayHandler());

        std::ignore = builder(gateway).Login();
        BOOST_TEST(!Credentials::Load().has_value());

        std::ignore = builder(gateway).Login();
        BOOST_TEST(gateway.Received().size() == 2U);
    }

BOOST_AUTO_TEST_SUITE_END()
