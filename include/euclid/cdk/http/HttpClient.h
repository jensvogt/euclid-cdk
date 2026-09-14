// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <chrono>
#include <string>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Url.h>
#include <euclid/cdk/http/Message.h>

namespace Euclid::CDK {

    /**
     * @brief One HTTP response: the status, and the body as it arrived.
     *
     * @par
     * The body is kept raw rather than parsed on the way in, because a failed call's body is often
     * not JSON at all - a gateway's error page, say - and because the error types want to quote it
     * verbatim.
     *
     * @author jensvogt47\@gmail.com
     */
    struct EUCLID_CDK_API HttpResponse {

        /**
         * @brief HTTP status code, e.g. 200.
         */
        int statusCode{};

        /**
         * @brief Response body, verbatim.
         */
        std::string body;

        /**
         * @brief True for 2xx status codes.
         */
        [[nodiscard]]
        bool IsSuccess() const;

        /**
         * @brief The body parsed as JSON.
         *
         * @return the parsed value, or null when the body is empty or is not JSON.
         */
        [[nodiscard]]
        boost::json::value Json() const;
    };

    /**
     * @brief How a connection to a euclid server is made.
     */
    struct EUCLID_CDK_API ConnectionOptions {

        /**
         * @brief Path to a PEM CA certificate trusted in addition to the system store, e.g. the
         * self-signed certificate a development deployment uses. Empty for the system store alone.
         */
        std::string caCertPath;

        /**
         * @brief Whether to verify the server's certificate. Turn it off for development servers
         * only - an unverified TLS connection authenticates nobody.
         */
        bool verify{true};

        /**
         * @brief How long a single request may take, connect and handshake included.
         */
        std::chrono::milliseconds timeout{std::chrono::seconds(30)};
    };

    /**
     * @brief The connection to one euclid server.
     *
     * @par
     * Deliberately small: euclid speaks one request shape - POST to "/" with the module in
     * x-euclid-target and the operation in x-euclid-action - so what a client has to do is build
     * that request and send it. Authentication is not done here but by whoever owns the
     * credentials, which is EAM::Session: a signature covers headers this class does not know
     * about, and splitting it any other way would mean signing something that was then changed.
     *
     * @par
     * Synchronous, one request at a time, and not thread-safe: a session hands out one of these per
     * module, and a caller that wants concurrency makes more sessions. The timeout is armed through
     * Boost.Beast's asynchronous operations even though the call blocks, because the synchronous
     * overloads silently ignore expires_after() and can block forever.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API HttpClient {
    public:

        /**
         * @brief Opens no connection yet - one is made per request.
         *
         * @param baseUrl euclid server endpoint, e.g. "https://euclid.example.com:5566". A
         * trailing slash is dropped.
         * @param options TLS and timeout settings.
         */
        explicit HttpClient(const std::string &baseUrl, ConnectionOptions options = {});

        /**
         * @brief The server this client talks to.
         */
        [[nodiscard]]
        const std::string &BaseUrl() const;

        /**
         * @brief The connection settings, so that a second client for the same server can be made
         * on the same terms.
         */
        [[nodiscard]]
        const ConnectionOptions &Options() const;

        /**
         * @brief Builds a POST to "/" with everything set that does not depend on who is calling.
         *
         * @par
         * The Host header is set here rather than left to the transport, and to the authority
         * exactly as the base URL wrote it: euclid compares the Host it received against the one
         * the signature covers, so a client that dropped a default port would send a Host its own
         * signature was not made over.
         *
         * @param target      module target, e.g. "eam" - the x-euclid-target header.
         * @param action      module action, e.g. "login" - the x-euclid-action header.
         * @param body        request body; JSON unless contentType says otherwise.
         * @param contentType body content type.
         * @return the request, ready for routing headers, authentication and Send().
         */
        [[nodiscard]]
        Request NewRequest(const std::string &target, const std::string &action, const std::string &body, const std::string &contentType = "application/json") const;

        /**
         * @brief Sends a fully-built request and reads the response.
         *
         * @param target  module target, used only for the error message on failure.
         * @param action  module action, used only for the error message on failure.
         * @param request the request to send.
         * @param timeout how long to allow, or zero for this client's own timeout.
         * @return the status and the body.
         * @throws EuclidError on any connection, TLS or transport failure. A non-2xx status is not
         * one of those - it is an answer, and the caller decides what it means.
         */
        [[nodiscard]]
        HttpResponse Send(const std::string &target, const std::string &action, const Request &request, std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) const;

        /**
         * @brief Where euclid installs its own CA certificate, when it is installed at all.
         *
         * @par
         * Applied only when the file is actually there, so it is a no-op on a machine with no
         * euclid deployment and the right thing on one that has.
         *
         * @return the path, or an empty string when there is no such file.
         */
        [[nodiscard]]
        static std::string DefaultCaCertPath();

    private:

        std::string _baseUrl;
        EndpointParts _endpoint;
        ConnectionOptions _options;
    };

}// namespace Euclid::CDK
