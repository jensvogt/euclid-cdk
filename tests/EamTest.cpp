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

    // One user is described exactly as a listing describes each of its own, so that a field added
    // to the one is in the other by construction rather than by somebody remembering.
    BOOST_AUTO_TEST_CASE(GetsOneUserTheWayAListingDescribesEach) {
        const FakeGateway gateway(gatewayHandler(R"({"user": {"userId": "jens", "ern": "ern:eam:user/jens",
                                                              "email": "jens@example.com", "accountId": "000000000000",
                                                              "region": "eu-central-1"}})"));
        const auto session = builder(gateway).Login();

        const auto user = session.GetUser("jens");
        BOOST_TEST(user.userId == "jens");
        BOOST_TEST(user.ern == "ern:eam:user/jens");
        BOOST_TEST(user.email == "jens@example.com");

        // The id rather than the ERN: that is what everything else names a user with.
        BOOST_TEST(boost::json::parse(gateway.LastRequest().body()).at("userId").as_string() == "jens");
    }

    BOOST_AUTO_TEST_CASE(GetsOneAccountByItsId) {
        const FakeGateway gateway(gatewayHandler(R"({"account": {"accountId": "111", "name": "acme",
                                                                 "ern": "ern:eam:account/111",
                                                                 "description": "an account"}})"));
        const auto session = builder(gateway).Login();

        const auto account = session.GetAccount("111");
        BOOST_TEST(account.accountId == "111");
        BOOST_TEST(account.name == "acme");
        BOOST_TEST(account.ern == "ern:eam:account/111");

        // The ID rather than the name: the ID is what an ERN carries and what everything is
        // scoped by, and it is left out of the ERN field rather than sent empty.
        const auto body = boost::json::parse(gateway.LastRequest().body()).as_object();
        BOOST_TEST(body.at("accountId").as_string() == "111");
        BOOST_TEST(!body.contains("ern"));
    }

    BOOST_AUTO_TEST_CASE(GetsAnAccountByErnWhenGivenOne) {
        const FakeGateway gateway(gatewayHandler(R"({"account": {"accountId": "111"}})"));
        const auto session = builder(gateway).Login();

        std::ignore = session.GetAccount("ern:euclid:eam:eu-central-1:111::account/111");

        const auto body = boost::json::parse(gateway.LastRequest().body()).as_object();
        BOOST_TEST(body.at("ern").as_string() == "ern:euclid:eam:eu-central-1:111::account/111");
        BOOST_TEST(!body.contains("accountId"));
    }

    BOOST_AUTO_TEST_CASE(GetsOneUserGroupWithItsMembers) {
        const FakeGateway gateway(gatewayHandler(R"({"userGroup": {"name": "ops", "ern": "ern:eam:user-group/ops",
                                                                   "description": "operations",
                                                                   "userIds": ["jens", "jill"]}})"));
        const auto session = builder(gateway).Login();

        const auto group = session.GetUserGroup("ops");
        BOOST_TEST(group.name == "ops");
        BOOST_REQUIRE(group.userIds.size() == 2U);
        BOOST_TEST(group.userIds[0] == "jens");

        const auto body = boost::json::parse(gateway.LastRequest().body()).as_object();
        BOOST_TEST(body.at("name").as_string() == "ops");
        BOOST_TEST(!body.contains("ern"));
    }

    // Groups are installation-wide, so a name is enough - but a grant's principal carries the ERN,
    // and that has to reach the server as the field it is rather than as a name.
    BOOST_AUTO_TEST_CASE(GetsAUserGroupByErnWhenGivenOne) {
        const FakeGateway gateway(gatewayHandler(R"({"userGroup": {"name": "ops"}})"));
        const auto session = builder(gateway).Login();

        std::ignore = session.GetUserGroup("ern:euclid:eam:eu-central-1:000000000000:user-group/ops");

        const auto body = boost::json::parse(gateway.LastRequest().body()).as_object();
        BOOST_TEST(body.at("ern").as_string() == "ern:euclid:eam:eu-central-1:000000000000:user-group/ops");
        BOOST_TEST(!body.contains("name"));
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

BOOST_AUTO_TEST_SUITE(EamRoleTest)

    // What a user may do used to arrive on the user, as an accountGrants array. It is its own
    // record now, so the SDK asks for it separately - and the call that grants one says what the
    // principal may do as well as where, which GrantNamespaceAccess never did.

    BOOST_AUTO_TEST_CASE(GrantingAnswersWithTheIdThatRevokesIt) {
        const FakeGateway gateway(gatewayHandler(R"({"grant": {"grantId": "g-1", "role": "operator",
                                                               "principal": "ern:...:user/jens",
                                                               "namespaces": ["production"]}})"));
        const auto session = builder(gateway).Login();

        const auto grant = session.GrantRole("operator", "ern:...:user/jens", {.namespaces = {"production"}});

        BOOST_TEST(grant.grantId == "g-1");
        BOOST_TEST(grant.role == "operator");
        const auto body = boost::json::parse(gateway.LastRequest().body());
        BOOST_TEST(body.at("role").as_string() == "operator");
        BOOST_TEST(body.at("namespaces").as_array().size() == 1U);
        BOOST_TEST(body.at("resources").as_array().at(0).as_string() == "*");
    }

    BOOST_AUTO_TEST_CASE(RevokingTakesTheGrantsOwnId) {
        const FakeGateway gateway(gatewayHandler("{}"));
        const auto session = builder(gateway).Login();

        session.RevokeRole("g-1");

        BOOST_TEST(boost::json::parse(gateway.LastRequest().body()).at("grantId").as_string() == "g-1");
        BOOST_TEST(std::string(gateway.LastRequest()["x-euclid-action"]) == "revoke-role");
    }

    // The third question - what is granted here at all - which one request per user would
    // otherwise cost.
    BOOST_AUTO_TEST_CASE(ListingWithNoOptionsAsksForTheWholeAccount) {
        const FakeGateway gateway(gatewayHandler(R"({"total": 1, "grants": [{"grantId": "g-1",
                                                     "role": "operator", "namespaces": ["production"]}]})"));
        const auto session = builder(gateway).Login();

        const auto page = session.ListGrants();

        BOOST_TEST(page.total == 1L);
        BOOST_REQUIRE(page.items.size() == 1U);
        BOOST_TEST(page.items[0].role == "operator");
        const auto body = boost::json::parse(gateway.LastRequest().body());
        BOOST_TEST(body.at("principal").as_string() == "");
        BOOST_TEST(body.at("role").as_string() == "");
        // A page size of zero is every grant, which is what this call returned before paging
        // existed - so naming no options still means what it meant.
        BOOST_TEST(body.at("pageSize").as_int64() == 0L);
        BOOST_TEST(body.at("sortColumn").as_string() == "principal");
    }

    // The total counts every grant matching the filter rather than the page, which is what says
    // there is another page to ask for.
    BOOST_AUTO_TEST_CASE(GrantsArePagedAndTheTotalIsNotThePageSize) {
        const FakeGateway gateway(gatewayHandler(R"({"total": 57, "grants": [{"grantId": "g-1", "role": "operator"}]})"));
        const auto session = builder(gateway).Login();

        const auto page = session.ListGrants({.pageSize = 25, .pageIndex = 2, .sortColumn = "created", .sortDirection = "desc"});

        BOOST_TEST(page.total == 57L);
        BOOST_TEST(page.items.size() == 1U);

        const auto body = boost::json::parse(gateway.LastRequest().body());
        BOOST_TEST(body.at("pageSize").as_int64() == 25L);
        BOOST_TEST(body.at("pageIndex").as_int64() == 2L);
        BOOST_TEST(body.at("sortColumn").as_string() == "created");
        BOOST_TEST(body.at("sortDirection").as_string() == "desc");
    }

    BOOST_AUTO_TEST_CASE(CheckPermissionSaysWhy) {
        const FakeGateway gateway(gatewayHandler(R"({"allowed": false,
                                                     "reason": "no role granted here holds 'ens:publish-message'",
                                                     "role": ""})"));
        const auto session = builder(gateway).Login();

        const auto answer = session.CheckPermission("order-service", "ens", "publish-message", "production");

        BOOST_TEST(!answer.allowed);
        BOOST_TEST(answer.reason.find("ens:publish-message") != std::string::npos);
    }

BOOST_AUTO_TEST_SUITE_END()
