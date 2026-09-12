// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <utility>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>

namespace Euclid::CDK {

    namespace {

        std::string trim(const std::string &s) {
            const auto begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) return {};
            const auto end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        }

        std::string withReason(const std::string &message, const std::string &reason) {
            return reason.empty() ? message : message + ": " + reason;
        }

    }// namespace

    std::string ReasonOf(const std::string &body) {

        if (body.empty()) return {};

        boost::system::error_code ec;
        if (const auto parsed = boost::json::parse(body, ec); !ec && parsed.is_object()) {
            if (const auto *error = parsed.as_object().if_contains("error"); error != nullptr && error->is_string()) {
                return std::string(error->as_string());
            }
        }

        // Not JSON at all, or not the shape we expect, which is what a proxy's error page looks
        // like. The body itself is then the most useful thing there is to say.
        return trim(body);
    }

    EuclidError::EuclidError(const std::string &message) : std::runtime_error(message) {}

    AuthenticationError::AuthenticationError(const int status, const std::string &body)
        : EuclidError(withReason("login failed with HTTP " + std::to_string(status), ReasonOf(body))),
          _status(status), _body(body), _reason(ReasonOf(body)) {}

    int AuthenticationError::Status() const { return _status; }

    const std::string &AuthenticationError::Body() const { return _body; }

    const std::string &AuthenticationError::Reason() const { return _reason; }

    ServiceError::ServiceError(std::string target, std::string action, const int status, const std::string &body)
        : EuclidError(withReason(target + "/" + action + " failed with HTTP " + std::to_string(status), ReasonOf(body))),
          _target(std::move(target)), _action(std::move(action)), _status(status), _body(body), _reason(ReasonOf(body)) {}

    const std::string &ServiceError::Target() const { return _target; }

    const std::string &ServiceError::Action() const { return _action; }

    int ServiceError::Status() const { return _status; }

    const std::string &ServiceError::Body() const { return _body; }

    const std::string &ServiceError::Reason() const { return _reason; }

}// namespace Euclid::CDK
