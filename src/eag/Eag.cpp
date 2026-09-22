// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>

// Euclid includes
#include <euclid/cdk/Json.h>
#include <euclid/cdk/eag/Eag.h>

namespace Euclid::CDK::EAG {

    namespace {

        /**
         * @brief A JSON array of strings, for the payload fields that take a list.
         */
        boost::json::array stringsOf(const std::vector<std::string> &values) {
            boost::json::array array;
            for (const auto &value: values) array.emplace_back(value);
            return array;
        }

        /**
         * @brief A field sent only when it says something.
         *
         * @par
         * The server reads a field that is present, whatever it holds, and falls back to a default
         * only when it is absent - so an empty namespace sent along would publish the route at the
         * account root instead of in the session's namespace, and an empty authentication would be
         * refused outright rather than read as "NONE".
         */
        void putIfSet(boost::json::object &payload, const std::string &field, const std::string &value) {
            if (!value.empty()) payload[field] = value;
        }

        /**
         * @brief A field of an update, sent only when the caller named it.
         */
        template<typename T>
        void put(boost::json::object &payload, const std::string &field, const std::optional<T> &value) {
            if (!value.has_value()) return;
            if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                payload[field] = stringsOf(*value);
            } else {
                payload[field] = *value;
            }
        }

    }// namespace

    Eag::Eag(const EAM::Session &session) : ModuleClient(session, std::string(EAG::Target)) {}

    // -- publishing ---------------------------------------------------------------------------------

    Route Eag::CreateRoute(const std::string &routeId, const std::string &path,
                           const std::string &applicationId, const RouteOptions &options) const {

        boost::json::object payload{
                {"routeId", routeId},
                {"path", path},
                {"type", std::string(TypeProxy)},
                {"applicationId", applicationId},
        };
        ApplyRouteOptions(payload, options);

        return RouteOf("create-route", payload);
    }

    Route Eag::CreateModuleRoute(const std::string &routeId, const std::string &path,
                                 const std::string &moduleTarget, const std::string &moduleAction,
                                 const RouteOptions &options) const {

        boost::json::object payload{
                {"routeId", routeId},
                {"path", path},
                {"type", std::string(TypeProxy)},
                {"moduleTarget", moduleTarget},
                {"moduleAction", moduleAction},
        };
        ApplyRouteOptions(payload, options);

        return RouteOf("create-route", payload);
    }

    Route Eag::CreateUploadRoute(const std::string &routeId, const std::string &path,
                                 const std::string &bucket, const UploadRouteOptions &options) const {

        boost::json::object payload{
                {"routeId", routeId},
                {"path", path},
                {"type", std::string(TypeUpload)},
                {"bucket", bucket},
                {"keyPrefix", options.keyPrefix},
                {"maxBytes", options.maxBytes},
                {"partSize", options.partSize},
                {"contentTypes", stringsOf(options.contentTypes)},
        };
        ApplyRouteOptions(payload, options.route);
        // The upload route's own, which is not defaulted to NONE and overrides whatever the common
        // options carry - an upload route that is not authenticated is refused by the server.
        putIfSet(payload, "authentication", options.authentication);

        return RouteOf("create-route", payload);
    }

    Route Eag::UpdateRoute(const std::string &routeId, const UpdateRouteOptions &options) const {

        boost::json::object payload{{"routeId", routeId}};
        put(payload, "path", options.path);
        put(payload, "applicationId", options.applicationId);
        put(payload, "moduleTarget", options.moduleTarget);
        put(payload, "moduleAction", options.moduleAction);
        put(payload, "methods", options.methods);
        put(payload, "authentication", options.authentication);
        put(payload, "type", options.type);
        put(payload, "bucket", options.bucket);
        put(payload, "keyPrefix", options.keyPrefix);
        put(payload, "maxBytes", options.maxBytes);
        put(payload, "partSize", options.partSize);
        put(payload, "contentTypes", options.contentTypes);
        put(payload, "region", options.region);
        // "namespace" on the wire, as in EAP and ETS.
        put(payload, "namespace", options.nameSpace);
        put(payload, "active", options.active);

        return RouteOf("update-route", payload);
    }

    Route Eag::SetRouteActive(const std::string &routeId, const bool active) const {
        return UpdateRoute(routeId, {.active = active});
    }

    void Eag::DeleteRoute(const std::string &routeId) const {
        std::ignore = Call("delete-route", {{"routeId", routeId}});
    }

    // -- reading ------------------------------------------------------------------------------------

    std::vector<Route> Eag::ListRoutes(const std::string &pathPrefix) const {
        std::vector<Route> routes;
        for (const auto &document: Json::Documents(Call("list-routes", {{"prefix", pathPrefix}}), "routes")) {
            routes.push_back(ToRoute(document));
        }
        return routes;
    }

    Route Eag::GetRoute(const std::string &routeId) const {
        return RouteOf("get-route", {{"routeId", routeId}});
    }

    ListenersResult Eag::ListListeners() const {
        return ToListenersResult(Call("list-listeners"));
    }

    // -- monitoring ---------------------------------------------------------------------------------

    boost::json::object Eag::Metrics() const {
        return Call("get-metrics");
    }

    Route Eag::RouteOf(const std::string &action, const boost::json::object &payload) const {
        return ToRoute(Call(action, payload));
    }

    void Eag::ApplyRouteOptions(boost::json::object &payload, const RouteOptions &options) {
        payload["methods"] = stringsOf(options.methods);
        payload["active"] = options.active;
        putIfSet(payload, "authentication", options.authentication);
        putIfSet(payload, "namespace", options.nameSpace);
        putIfSet(payload, "region", options.region);
    }

}// namespace Euclid::CDK::EAG
