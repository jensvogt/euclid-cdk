// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <utility>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/ModuleClient.h>

namespace Euclid::CDK {

    ModuleClient::ModuleClient(const EAM::Session &session, std::string target, std::vector<std::string> byteActions, Headers headers)
        : _session(session), _target(std::move(target)), _byteActions(std::move(byteActions)), _headers(std::move(headers)) {
        if (_target.empty()) throw EuclidError("a module client must name its target, e.g. \"esm\"");
    }

    const std::string &ModuleClient::Target() const { return _target; }

    const EAM::Session &ModuleClient::Session() const { return _session; }

    boost::json::object ModuleClient::Call(const std::string &action, const boost::json::object &payload) const {
        return Result(action, Post(action, payload));
    }

    // -- transport ----------------------------------------------------------------------------------

    HttpResponse ModuleClient::Post(const std::string &action, const boost::json::object &payload, const Headers &headers, const std::chrono::milliseconds timeout) const {
        const auto request = NewRequest(action, boost::json::serialize(payload), "application/json", headers);
        return _session.Client().Send(_target, action, request, timeout);
    }

    HttpResponse ModuleClient::PostBytes(const std::string &action, const std::string &data, const Headers &headers, const std::chrono::milliseconds timeout) const {
        const auto request = NewRequest(action, data, "application/octet-stream", headers);
        return _session.Client().Send(_target, action, request, timeout);
    }

    Request ModuleClient::NewRequest(const std::string &action, const std::string &body, const std::string &contentType, const Headers &headers) const {

        Request request = _session.Client().NewRequest(_target, action, body, contentType);
        // Before the routing headers and the signature, so that a header this module sets can never
        // overwrite one a signature was made over. This client's own first, so that a call can still
        // say something different from what the client says by default.
        for (const auto &[name, value]: _headers) request.set(name, value);
        for (const auto &[name, value]: headers) request.set(name, value);
        _session.ApplyRoutingHeaders(request);

        // A byte-carrying action presents the session's bearer token rather than a signature, which
        // is what euclid-cli and the other SDKs do for the same actions, so every client writes an
        // object the same way. A session that asked to always sign signs them too.
        if (IsByteAction(action) && !_session.AlwaysSigns()) {
            _session.ApplyBearerToken(request);
        } else {
            _session.Authenticate(request, _target);
        }
        return request;
    }

    boost::json::object ModuleClient::Result(const std::string &action, const HttpResponse &response) const {

        if (!response.IsSuccess()) throw ServiceError(_target, action, response.statusCode, response.body);

        const auto parsed = response.Json();
        // A response that is not an object still says something - an action answering with a bare
        // array or number should reach a caller rather than be flattened into nothing.
        if (parsed.is_object()) return parsed.as_object();
        return parsed.is_null() ? boost::json::object{} : boost::json::object{{"result", parsed}};
    }

    // -- reading answers ----------------------------------------------------------------------------

    std::string ModuleClient::TextOf(const std::string &action, const boost::json::object &payload, const std::string &field) const {
        return Json::Text(Call(action, payload), field);
    }

    long ModuleClient::NumberOf(const std::string &action, const boost::json::object &payload, const std::string &field) const {
        return Json::Number(Call(action, payload), field);
    }

    bool ModuleClient::IsByteAction(const std::string_view action) const {
        return std::ranges::find(_byteActions, action) != _byteActions.end();
    }

}// namespace Euclid::CDK
