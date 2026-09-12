// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <utility>

// Boost includes
#include <boost/asio/connect.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

// Euclid includes
#include "FakeGateway.h"

namespace Euclid::CDK::Test {

    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    using tcp = asio::ip::tcp;

    FakeGateway::FakeGateway(Handler handler) : _handler(std::move(handler)), _acceptor(_ioc) {

        const tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), 0);
        _acceptor.open(endpoint.protocol());
        _acceptor.set_option(asio::socket_base::reuse_address(true));
        _acceptor.bind(endpoint);
        _acceptor.listen();
        _port = _acceptor.local_endpoint().port();

        _thread = std::thread([this] { Run(); });
    }

    FakeGateway::~FakeGateway() {
        {
            const std::lock_guard lock(_mutex);
            _stopped = true;
        }

        // Closing an acceptor another thread is blocked in accept() on is not reliably enough to
        // wake it, so the loop is woken the way it expects to be: with a connection. It sees the
        // stop flag and returns before reading anything.
        try {
            asio::io_context ioc;
            tcp::socket socket(ioc);
            socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), _port));
        } catch (const std::exception &) {
            // The loop is already gone, which is the state this is trying to reach anyway.
        }

        if (_thread.joinable()) _thread.join();

        boost::system::error_code ec;
        std::ignore = _acceptor.close(ec);
    }

    std::string FakeGateway::BaseUrl() const {
        return "http://127.0.0.1:" + std::to_string(_port);
    }

    std::vector<Request> FakeGateway::Received() const {
        const std::lock_guard lock(_mutex);
        return _received;
    }

    Request FakeGateway::LastRequest() const {
        const std::lock_guard lock(_mutex);
        return _received.empty() ? Request{} : _received.back();
    }

    RawResponse FakeGateway::Json(const int status, const std::string &body) {
        RawResponse response(static_cast<http::status>(status), 11);
        response.set(http::field::content_type, "application/json");
        response.body() = body;
        response.prepare_payload();
        return response;
    }

    void FakeGateway::Run() {

        for (;;) {
            boost::system::error_code ec;
            tcp::socket socket(_ioc);
            std::ignore = _acceptor.accept(socket, ec);
            if (ec) return;

            {
                const std::lock_guard lock(_mutex);
                if (_stopped) return;
            }

            beast::flat_buffer buffer;
            Request request;
            std::ignore = http::read(socket, buffer, request, ec);
            if (ec) continue;

            {
                const std::lock_guard lock(_mutex);
                _received.push_back(request);
            }

            auto response = _handler(request);
            response.keep_alive(false);
            response.prepare_payload();
            std::ignore = http::write(socket, response, ec);
            std::ignore = socket.shutdown(tcp::socket::shutdown_both, ec);
        }
    }

}// namespace Euclid::CDK::Test
