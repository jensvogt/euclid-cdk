// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Session.h>
#include <euclid/cdk/http/HttpClient.h>

namespace Euclid::CDK {

    /**
     * @brief What every module client has in common.
     *
     * @par
     * euclid speaks one request shape, so most of a module client is the same client over and over:
     * POST to "/", name the module in x-euclid-target and the operation in x-euclid-action,
     * authenticate the way the session that created it authenticates, and turn a non-2xx into a
     * ServiceError. That part lives here; what is left in a module - ESM::Esm, and the ones that
     * follow it - is the operations themselves.
     *
     * @par
     * Deriving from this is also how an application reaches a module this SDK has not wrapped yet:
     * give the subclass a target and call Call().
     *
     * @par
     * Holds the session by reference rather than a copy of what it knew at the time, so the client
     * follows it: an EAM::Session::ChangeNamespace() between two calls scopes the second one, and a
     * token the session rotates is the token the next request carries. The session has to outlive
     * the client, which it does in the way these are written - the client is built from a session
     * that is already there and used while it still is.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API ModuleClient {
    public:

        virtual ~ModuleClient() = default;

        ModuleClient(const ModuleClient &) = default;
        ModuleClient &operator=(const ModuleClient &) = delete;

        /**
         * @brief The module this client talks to - what travels in x-euclid-target.
         */
        [[nodiscard]]
        const std::string &Target() const;

        /**
         * @brief The session this client authenticates as.
         */
        [[nodiscard]]
        const EAM::Session &Session() const;

        /**
         * @brief Sends any action of this module, for one this SDK does not wrap yet.
         *
         * @par
         * Public on purpose: a server that gains an action should be reachable without waiting for a
         * release here.
         *
         * @param action  the action, e.g. "list-buckets".
         * @param payload the request body.
         * @return the response body as an object; a response that is not an object is answered under
         * a "result" field.
         * @throws ServiceError if the server refused the call.
         * @throws EuclidError if the server could not be reached.
         */
        [[nodiscard]]
        boost::json::object Call(const std::string &action, const boost::json::object &payload = {}) const;

    protected:

        /**
         * @brief Headers a request carries beyond the ones every request has.
         */
        using Headers = std::map<std::string, std::string>;

        /**
         * @brief Builds a client for one module.
         *
         * @param session     the session whose credentials, identity and connection it uses.
         * @param target      the module, e.g. "esm".
         * @param byteActions the actions of this module that carry raw bytes rather than JSON, and
         * so present the session's bearer token rather than a signature - see Authenticate().
         * @param headers     headers every request of this client carries on top of the session's
         * own, for a second view of a module that differs from the first by one header - see
         * EQS::Eqs::AsInternal().
         */
        ModuleClient(const EAM::Session &session, std::string target, std::vector<std::string> byteActions = {}, Headers headers = {});

        /**
         * @brief One JSON action, sent. The raw response, for a caller that reads a status Result()
         * would throw on.
         *
         * @param action  the action.
         * @param payload the request body.
         * @param headers headers beyond the usual ones.
         * @param timeout how long to allow, or zero for the session's own timeout.
         */
        [[nodiscard]]
        HttpResponse Post(const std::string &action, const boost::json::object &payload = {}, const Headers &headers = {}, std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) const;

        /**
         * @brief One of the actions whose body is bytes rather than JSON, described by its headers
         * instead.
         *
         * @par
         * The content type is the only routing header that changes, and neither signing scheme
         * covers it - both sign a fixed list of headers, which is what lets this differ without the
         * signature having to know.
         *
         * @param action  the action.
         * @param data    the body, verbatim.
         * @param headers headers beyond the usual ones.
         * @param timeout how long to allow, or zero for the session's own timeout.
         */
        [[nodiscard]]
        HttpResponse PostBytes(const std::string &action, const std::string &data, const Headers &headers = {}, std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) const;

        /**
         * @brief The response body as an object, or the server's own reason for refusing.
         *
         * @throws ServiceError if the response is not a 2xx.
         */
        [[nodiscard]]
        boost::json::object Result(const std::string &action, const HttpResponse &response) const;

        /**
         * @brief An action whose answer is one string - an ERN, mostly.
         */
        [[nodiscard]]
        std::string TextOf(const std::string &action, const boost::json::object &payload, const std::string &field) const;

        /**
         * @brief An action whose answer is one number - a size, a count.
         */
        [[nodiscard]]
        long NumberOf(const std::string &action, const boost::json::object &payload, const std::string &field) const;

        /**
         * @brief Builds and authenticates one request of this module, without sending it.
         *
         * @param action      the action.
         * @param body        the request body.
         * @param contentType the body's content type.
         * @param headers     headers beyond the usual ones.
         * @return the request, signed or carrying a bearer token.
         */
        [[nodiscard]]
        Request NewRequest(const std::string &action, const std::string &body, const std::string &contentType, const Headers &headers) const;

    private:

        /**
         * @brief Whether this action carries bytes, and so presents the token rather than a
         * signature.
         */
        [[nodiscard]]
        bool IsByteAction(std::string_view action) const;

        const EAM::Session &_session;
        std::string _target;
        std::vector<std::string> _byteActions;
        Headers _headers;
    };

}// namespace Euclid::CDK
