// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Boost includes
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

// Euclid includes
#include <euclid/cdk/http/Message.h>

namespace Euclid::CDK::Test {

    /**
     * @brief A euclid server, in this process, on a port the operating system picks.
     *
     * @par
     * These are the tests that would catch a client that signs one thing and sends another: the
     * gateway verifies every signed request with the same rules euclid's HttpActionServer applies,
     * so a mismatch between the Host header a signature covers and the one that goes on the wire
     * fails here rather than in production. A mock that only recorded the call would not.
     *
     * @par
     * Plain HTTP, one connection at a time, one request per connection - which is exactly what the
     * client does, and keeps the whole thing to one accept loop on one thread.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class FakeGateway {
    public:

        /**
         * @brief Answers one request. Called on the gateway's own thread.
         */
        using Handler = std::function<RawResponse(const Request &)>;

        /**
         * @brief Starts listening on 127.0.0.1 and serving with handler.
         *
         * @param handler answers each request.
         */
        explicit FakeGateway(Handler handler);

        ~FakeGateway();

        FakeGateway(const FakeGateway &) = delete;
        FakeGateway &operator=(const FakeGateway &) = delete;

        /**
         * @brief Where this gateway is reachable, e.g. "http://127.0.0.1:41234".
         */
        [[nodiscard]]
        std::string BaseUrl() const;

        /**
         * @brief Every request served so far, in order.
         */
        [[nodiscard]]
        std::vector<Request> Received() const;

        /**
         * @brief The last request served.
         */
        [[nodiscard]]
        Request LastRequest() const;

        /**
         * @brief A JSON response.
         *
         * @param status HTTP status.
         * @param body   the body, already serialized.
         */
        [[nodiscard]]
        static RawResponse Json(int status, const std::string &body);

        /**
         * @brief A response whose body is bytes - what ESM's get-object and download-part answer
         * with.
         *
         * @param status HTTP status.
         * @param body   the bytes.
         */
        [[nodiscard]]
        static RawResponse Bytes(int status, const std::string &body);

    private:

        void Run();

        Handler _handler;
        boost::asio::io_context _ioc;
        boost::asio::ip::tcp::acceptor _acceptor;
        unsigned short _port{};
        std::thread _thread;
        mutable std::mutex _mutex;
        std::vector<Request> _received;
        bool _stopped{false};
    };

}// namespace Euclid::CDK::Test
