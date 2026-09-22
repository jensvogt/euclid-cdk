// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EAG, end to end against a fake euclid server.
 *
 * @par
 * A route decides what the outside world can reach and on what terms, so what these check is mostly
 * what does *not* go on the wire: a namespace left empty, an authentication left unset, a field an
 * update did not name. The server reads a field that is present whatever it holds, so a value sent
 * along to be tidy is a decision nobody made - and on this module the decision is whether a path is
 * public.
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
#include <euclid/cdk/eag/Eag.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    /**
     * @brief A gateway, a logged-in session and an EAG client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EagClient {

        explicit EagClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), eag(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EAG::Eag eag;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief One route, as the server describes it - every action but delete answers with this.
     */
    boost::json::object route(const std::string &type = "PROXY") {
        return {
                {"routeId", "orders"},
                {"ern", "ern:euclid:eag:eu-central-1:000000000000:route/orders"},
                {"accountId", "000000000000"},
                {"region", "eu-central-1"},
                // "namespace" here, as in EAP and ETS; everywhere else it is "nameSpace".
                {"namespace", "integration"},
                {"path", "/orders"},
                {"type", type},
                {"upload", boost::json::object{{"bucket", type == "UPLOAD" ? "ern:bucket/deliveries" : ""},
                                               {"keyPrefix", type == "UPLOAD" ? "incoming/" : ""},
                                               {"maxBytes", type == "UPLOAD" ? 1048576 : 0},
                                               {"partSize", type == "UPLOAD" ? 5242880 : 0},
                                               {"contentTypes", type == "UPLOAD"
                                                                        ? boost::json::array{"application/xml"}
                                                                        : boost::json::array{}}}},
                {"applicationId", type == "UPLOAD" ? "" : "order-service"},
                {"moduleTarget", ""},
                {"moduleAction", ""},
                {"methods", boost::json::array{"GET", "POST"}},
                {"authentication", "EUCLID"},
                {"active", true},
                {"created", "2026-09-01T08:00:00Z"},
                {"modified", "2026-09-19T08:00:00Z"},
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EagRouteTest)

    BOOST_AUTO_TEST_CASE(PublishesAPathServedByAnApplication) {
        const EagClient client(Test::Answering(boost::json::serialize(route())));

        const auto published = client.eag.CreateRoute("orders", "/orders", "order-service",
                                                      {.methods = {std::string(EAG::MethodGet), std::string(EAG::MethodPost)},
                                                       .authentication = std::string(EAG::AuthEuclid)});

        BOOST_TEST(published.routeId == "orders");
        BOOST_TEST(published.path == "/orders");
        BOOST_TEST(published.applicationId == "order-service");
        BOOST_TEST(published.nameSpace == "integration");
        BOOST_REQUIRE(published.methods.size() == 2U);
        BOOST_TEST(published.methods[0] == "GET");
        BOOST_TEST(published.active);
        BOOST_TEST(published.IsAuthenticated());
        BOOST_TEST(!published.IsUpload());
        BOOST_TEST(!published.IsModuleRoute());

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("routeId").as_string() == "orders");
        BOOST_TEST(body.at("path").as_string() == "/orders");
        BOOST_TEST(body.at("type").as_string() == "PROXY");
        BOOST_TEST(body.at("applicationId").as_string() == "order-service");
        BOOST_TEST(body.at("authentication").as_string() == "EUCLID");
        BOOST_TEST(body.at("methods").as_array().size() == 2U);
        BOOST_TEST(body.at("active").as_bool() == true);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "create-route");
    }

    // The server falls back to its own default only for a field that is absent, so a namespace,
    // region or authentication sent along empty is not "unset" - it is a decision. An empty
    // namespace would publish the route at the account root rather than in the session's, and an
    // empty authentication is refused with a 400 rather than read as NONE.
    BOOST_AUTO_TEST_CASE(LeavesOutWhatItWasNotTold) {
        const EagClient client(Test::Answering(boost::json::serialize(route())));

        std::ignore = client.eag.CreateRoute("orders", "/orders", "order-service");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(!body.as_object().contains("namespace"));
        BOOST_TEST(!body.as_object().contains("region"));
        BOOST_TEST(!body.as_object().contains("authentication"));
        // Sent whatever happens: an empty method list is what says "every method", and a route
        // created without one said nothing about being out of service.
        BOOST_TEST(body.at("methods").as_array().empty());
        BOOST_TEST(body.at("active").as_bool() == true);
    }

    BOOST_AUTO_TEST_CASE(PublishesAPathThatReachesEuclidItself) {
        auto answer = route();
        answer["applicationId"] = "";
        answer["moduleTarget"] = "eam";
        answer["moduleAction"] = "login";
        const EagClient client(Test::Answering(boost::json::serialize(answer)));

        const auto published = client.eag.CreateModuleRoute("login", "/euclid/login", "eam", "login");

        BOOST_TEST(published.IsModuleRoute());
        BOOST_TEST(published.moduleTarget == "eam");
        BOOST_TEST(published.moduleAction == "login");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("moduleTarget").as_string() == "eam");
        // One action per route: a route that passed its remaining path through as actions would
        // publish every action the module has, including the ones that delete users.
        BOOST_TEST(body.at("moduleAction").as_string() == "login");
        BOOST_TEST(!body.as_object().contains("applicationId"));
    }

    BOOST_AUTO_TEST_CASE(PublishesAPathThatWritesIntoABucket) {
        const EagClient client(Test::Answering(boost::json::serialize(route("UPLOAD"))));

        const auto published = client.eag.CreateUploadRoute("deliveries", "/deliveries", "ern:bucket/deliveries",
                                                            {.keyPrefix = "incoming/",
                                                             .maxBytes = 1048576,
                                                             .contentTypes = {"application/xml"}});

        BOOST_TEST(published.IsUpload());
        BOOST_TEST(published.upload.bucket == "ern:bucket/deliveries");
        BOOST_TEST(published.upload.keyPrefix == "incoming/");
        BOOST_TEST(published.upload.maxBytes == 1048576L);
        BOOST_REQUIRE(published.upload.contentTypes.size() == 1U);
        BOOST_TEST(published.upload.contentTypes[0] == "application/xml");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("type").as_string() == "UPLOAD");
        BOOST_TEST(body.at("bucket").as_string() == "ern:bucket/deliveries");
        BOOST_TEST(body.at("keyPrefix").as_string() == "incoming/");
        BOOST_TEST(body.at("partSize").as_int64() == EAG::DefaultPartSize);
        // An upload route names a bucket, not a backend - naming one is refused by the server
        // rather than ignored, so this client does not send either.
        BOOST_TEST(!body.as_object().contains("applicationId"));
        BOOST_TEST(!body.as_object().contains("moduleTarget"));
    }

    // An unauthenticated upload route is a public write endpoint into somebody's bucket, which is
    // not something to end up with by leaving a field out - so this one defaults to a credential
    // where a proxy route defaults to none.
    BOOST_AUTO_TEST_CASE(AnUploadRouteAsksForACredentialUnlessToldOtherwise) {
        const EagClient client(Test::Answering(boost::json::serialize(route("UPLOAD"))));

        std::ignore = client.eag.CreateUploadRoute("deliveries", "/deliveries", "ern:bucket/deliveries");
        BOOST_TEST(lastBody(client.gateway).at("authentication").as_string() == "EUCLID");

        std::ignore = client.eag.CreateUploadRoute("deliveries", "/deliveries", "ern:bucket/deliveries",
                                                   {.authentication = std::string(EAG::AuthBasic)});
        BOOST_TEST(lastBody(client.gateway).at("authentication").as_string() == "BASIC");
    }

    BOOST_AUTO_TEST_CASE(UpdatesOnlyWhatItWasGiven) {
        const EagClient client(Test::Answering(boost::json::serialize(route())));

        std::ignore = client.eag.UpdateRoute("orders", {.path = "/orders/v2",
                                                        .methods = std::vector<std::string>{"GET"}});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("routeId").as_string() == "orders");
        BOOST_TEST(body.at("path").as_string() == "/orders/v2");
        BOOST_REQUIRE(body.at("methods").as_array().size() == 1U);
        // Everything the caller did not name stays away, so the route keeps what it had.
        BOOST_TEST(!body.as_object().contains("applicationId"));
        BOOST_TEST(!body.as_object().contains("authentication"));
        BOOST_TEST(!body.as_object().contains("active"));
        BOOST_TEST(!body.as_object().contains("type"));
    }

    // An empty string is a value rather than an absence here: it is how a field is cleared, which
    // is why the options are optional rather than defaulted to "".
    BOOST_AUTO_TEST_CASE(AnEmptyStringClearsAFieldRatherThanLeavingIt) {
        const EagClient client(Test::Answering(boost::json::serialize(route())));

        std::ignore = client.eag.UpdateRoute("orders", {.moduleAction = ""});

        const auto body = lastBody(client.gateway);
        BOOST_REQUIRE(body.as_object().contains("moduleAction"));
        BOOST_TEST(body.at("moduleAction").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(TakesARouteOutOfServiceWithoutDeletingIt) {
        auto answer = route();
        answer["active"] = false;
        const EagClient client(Test::Answering(boost::json::serialize(answer)));

        const auto withdrawn = client.eag.SetRouteActive("orders", false);

        BOOST_TEST(!withdrawn.active);
        // The path is still the route's, so nothing else can claim it meanwhile.
        BOOST_TEST(withdrawn.path == "/orders");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("active").as_bool() == false);
        BOOST_TEST(body.as_object().size() == 2U);// routeId and active, and nothing else
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "update-route");
    }

    BOOST_AUTO_TEST_CASE(ListsRoutesUnderAPathPrefix) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"routes", boost::json::array{route(), route("UPLOAD")}}});
        const EagClient client(Test::Answering(listed));

        const auto routes = client.eag.ListRoutes("/ord");

        BOOST_REQUIRE(routes.size() == 2U);
        BOOST_TEST(routes[0].applicationId == "order-service");
        BOOST_TEST(routes[1].IsUpload());
        // A path prefix rather than a routeId, and the field is "prefix" on the wire.
        BOOST_TEST(lastBody(client.gateway).at("prefix").as_string() == "/ord");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "list-routes");
    }

    // One route is described exactly as a listing describes each of its own, so that a field added
    // to the one is in the other by construction rather than by somebody remembering.
    BOOST_AUTO_TEST_CASE(DescribesOneRouteTheWayAListingDescribesEach) {
        const EagClient client(Test::Answering(boost::json::serialize(route())));

        const auto read = client.eag.GetRoute("orders");

        BOOST_TEST(read.ern == "ern:euclid:eag:eu-central-1:000000000000:route/orders");
        BOOST_TEST(read.accountId == "000000000000");
        BOOST_TEST(read.region == "eu-central-1");
        BOOST_TEST(read.created == "2026-09-01T08:00:00Z");
        BOOST_TEST(lastBody(client.gateway).at("routeId").as_string() == "orders");
    }

    // A stored route that says nothing about being active was written before the field existed,
    // and it serves. Reading it as inactive would take every one of them offline in the eyes of
    // whatever asks.
    BOOST_AUTO_TEST_CASE(ARouteThatSaysNothingAboutBeingActiveIsOne) {
        const EagClient client(Test::Answering(R"({"routeId": "orders", "path": "/orders"})"));

        BOOST_TEST(client.eag.GetRoute("orders").active);
    }

    BOOST_AUTO_TEST_CASE(DeletesARoute) {
        const EagClient client(Test::Answering("{}"));

        client.eag.DeleteRoute("orders");

        BOOST_TEST(lastBody(client.gateway).at("routeId").as_string() == "orders");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "delete-route");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EagListenerTest)

    BOOST_AUTO_TEST_CASE(ListsThePortsTheGatewayServes) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"listeners", boost::json::array{
                                      boost::json::object{{"namespace", "development"}, {"port", 8080},
                                                          {"protocol", "http"}, {"serving", true}},
                                      boost::json::object{{"namespace", "production"}, {"port", 8443},
                                                          {"protocol", "https"}, {"serving", true},
                                                          {"certificate", "eag-production"},
                                                          {"certificateConfigured", ""},
                                                          {"certificateFound", true},
                                                          {"certificateErn", "ern:euclid:ekm:eu-central-1:000000000000:certificate/eag-production"},
                                                          {"certificateSubject", "CN=euclid"},
                                                          {"certificateSubjectAltNames", boost::json::array{"euclid.example"}},
                                                          {"certificateGenerated", true},
                                                          {"certificateNotAfter", "2027-09-01T08:00:00Z"},
                                                          {"certificateExpired", false}}}},
                {"total", 2},
                {"serving", true}});
        const EagClient client(Test::Answering(listed));

        const auto listeners = client.eag.ListListeners();

        BOOST_TEST(listeners.total == 2L);
        BOOST_TEST(listeners.serving);
        BOOST_REQUIRE(listeners.listeners.size() == 2U);
        BOOST_TEST(!listeners.listeners[0].IsHttps());
        BOOST_TEST(listeners.listeners[0].port == 8080L);

        const auto &tls = listeners.listeners[1];
        BOOST_TEST(tls.IsHttps());
        // The certificate it serves rather than the one it named: naming none means the
        // conventional one for the namespace, and an empty name here would send somebody looking
        // for a certificate that is there under a name nothing told them.
        BOOST_TEST(tls.certificate == "eag-production");
        BOOST_TEST(tls.certificateConfigured == "");
        BOOST_TEST(tls.certificateFound);
        // Worth reading: a caller rejects a self-signed certificate until it has been given it.
        BOOST_TEST(tls.certificateGenerated);
        BOOST_TEST(!tls.certificateExpired);
        BOOST_REQUIRE(tls.certificateSubjectAltNames.size() == 1U);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "list-listeners");
    }

    // A listener is configuration read when EAG starts, so a port that was taken or a certificate
    // that could not be loaded is still listed - it is the one somebody is looking for - and says
    // it is serving nothing.
    BOOST_AUTO_TEST_CASE(AListenerThatIsNotBoundIsStillListed) {
        const EagClient client(Test::Answering(
                R"({"listeners": [{"namespace": "production", "port": 8443, "protocol": "https", "serving": false,
                                   "certificateFound": false}], "total": 1, "serving": false})"));

        const auto listeners = client.eag.ListListeners();

        BOOST_TEST(!listeners.serving);
        BOOST_REQUIRE(listeners.listeners.size() == 1U);
        BOOST_TEST(!listeners.listeners[0].serving);
        BOOST_TEST(!listeners.listeners[0].certificateFound);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EagTransportTest)

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndFollowsTheSession) {
        EagClient client(Test::Answering(boost::json::serialize(route())));

        client.session.ChangeNamespace("integration");
        std::ignore = client.eag.GetRoute("orders");

        const auto request = client.gateway.LastRequest();
        BOOST_TEST(std::string(request["x-euclid-target"]) == "eag");
        // Every action here resolves a routeId in the namespace the request was made in, which is
        // why a route published for another namespace is managed from that one.
        BOOST_TEST(std::string(request["x-euclid-namespace"]) == "integration");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
    }

    // What a route configures is which path reaches which application and whether a caller needs a
    // credential at all, so every action here is administrator-only.
    BOOST_AUTO_TEST_CASE(CarriesTheServersReasonWhenTheCallerIsNotAnAdministrator) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(403, R"({"error": "Administrator privileges required"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EAG::Eag eag(session);

        try {
            std::ignore = eag.CreateRoute("orders", "/orders", "order-service");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "eag");
            BOOST_TEST(ex.Action() == "create-route");
            BOOST_TEST(ex.Status() == 403);
            BOOST_TEST(ex.Reason() == "Administrator privileges required");
        }
    }

    // Two routes may share a path as long as their methods do not overlap; an overlap is refused,
    // because the winner would otherwise be whichever the sort happened to put first.
    BOOST_AUTO_TEST_CASE(CarriesTheRouteThatAlreadyClaimedThePath) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(409, R"({"error": "Path and method are already routed by routeId: orders-read"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EAG::Eag eag(session);

        try {
            std::ignore = eag.CreateRoute("orders-write", "/orders", "order-writer");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Status() == 409);
            BOOST_TEST(ex.Reason() == "Path and method are already routed by routeId: orders-read");
        }
    }

    BOOST_AUTO_TEST_CASE(ReachesItsMetricsAndAnActionThisSdkDoesNotWrap) {
        const EagClient client(Test::Answering(R"({"eag_route_count": 7, "whatever": 42})"));

        BOOST_TEST(client.eag.Metrics().at("eag_route_count").as_int64() == 7);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");

        BOOST_TEST(client.eag.Call("some-future-action").at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

BOOST_AUTO_TEST_SUITE_END()
