// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/auth/SigningScheme.h>

#include "TestSupport.h"

namespace Euclid::CDK::Test {

    std::string FutureToken() {
        const auto expiry = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + 3600;
        auto payload = Crypto::Base64Encode(boost::json::serialize(boost::json::object{{"exp", expiry}}));
        std::erase(payload, '=');
        return "header." + payload + ".signature";
    }

    std::string LoginResponse() {
        return boost::json::serialize(boost::json::object{
                {"token", FutureToken()},
                {"accessKeyId", AccessKeyId},
                {"secretAccessKey", SecretAccessKey},
                {"createdAt", "2026-09-12T10:00:00Z"},
                {"isAdmin", true},
                {"metadata", boost::json::object{{"region", "eu-central-1"}, {"accountId", "000000000000"}, {"user", "jens"}}},
        });
    }

    FakeGateway::Handler Authenticated(FakeGateway::Handler handler) {

        return [handler = std::move(handler)](const Request &request) {
            if (std::string(request["x-euclid-action"]) == "login") return FakeGateway::Json(200, LoginResponse());

            const auto *scheme = SigningScheme::Of(request);
            if (scheme == nullptr) {
                // No signature at all is fine when a bearer token was presented instead - which is
                // what the byte-carrying actions of a module do.
                if (!std::string(request[boost::beast::http::field::authorization]).starts_with("Bearer ")) {
                    return FakeGateway::Json(401, R"({"error": "unauthenticated"})");
                }
                return handler(request);
            }

            const auto keyId = scheme->Verify(request, [](const std::string &id) -> std::optional<std::string> {
                return id == AccessKeyId ? std::optional<std::string>(SecretAccessKey) : std::nullopt;
            });
            if (!keyId.has_value()) return FakeGateway::Json(401, R"({"error": "signature verification failed"})");

            return handler(request);
        };
    }

    FakeGateway::Handler Answering(const std::string &body) {
        return Authenticated([body](const Request &) { return FakeGateway::Json(200, body.empty() ? "{}" : body); });
    }

    EAM::Eam Builder(const FakeGateway &gateway) {
        return EAM::Eam::ForServer(gateway.BaseUrl()).UseCache(false).Credentials("jens", "secret");
    }

}// namespace Euclid::CDK::Test
