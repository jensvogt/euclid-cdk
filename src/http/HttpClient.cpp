// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <filesystem>
#include <string_view>
#include <utility>

// Boost includes
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/Version.h>
#include <euclid/cdk/http/HttpClient.h>

namespace Euclid::CDK {

    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace ssl = asio::ssl;
    using tcp = asio::ip::tcp;

    namespace {

        // Boost.Beast's timeout support (armed below via expires_after) only takes effect for
        // ASYNCHRONOUS operations - the synchronous connect/write/read overloads silently ignore it
        // and can block forever, which is what turns one orphaned connection into a hung process
        // with no error and no retry. Running the whole exchange as one chained async pipeline,
        // driven by a single ioc.run(), makes the configured timeout real: if any step stalls, the
        // stream's internal timer cancels it and the pending handler gets an error instead.

        RawResponse sendPlain(asio::io_context &ioc, const tcp::resolver::results_type &endpoints, const Request &request, const std::chrono::milliseconds timeout) {

            beast::tcp_stream stream(ioc);
            stream.expires_after(timeout);

            beast::error_code opEc;
            beast::flat_buffer buffer;
            RawResponse response;

            stream.async_connect(endpoints, [&](const beast::error_code &ec, const tcp::endpoint &) {
                if (ec) {
                    opEc = ec;
                    return;
                }
                http::async_write(stream, request, [&](const beast::error_code &writeEc, std::size_t) {
                    if (writeEc) {
                        opEc = writeEc;
                        return;
                    }
                    http::async_read(stream, buffer, response, [&](const beast::error_code &readEc, std::size_t) {
                        opEc = readEc;
                    });
                });
            });

            ioc.run();

            beast::error_code ec;
            std::ignore = stream.socket().shutdown(tcp::socket::shutdown_both, ec);

            if (opEc) throw boost::system::system_error(opEc);
            return response;
        }

        RawResponse sendTls(asio::io_context &ioc, const tcp::resolver::results_type &endpoints, const std::string &host,
                            const Request &request, const ConnectionOptions &options) {

            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            if (!options.caCertPath.empty()) {
                boost::system::error_code loadEc;
                ctx.load_verify_file(options.caCertPath, loadEc);
                if (loadEc) throw EuclidError("failed to load CA certificate " + options.caCertPath + ": " + loadEc.message());
            }

            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                throw EuclidError("failed to set TLS server name for " + host);
            }
            stream.set_verify_mode(options.verify ? ssl::verify_peer : ssl::verify_none);
            beast::get_lowest_layer(stream).expires_after(options.timeout);

            beast::error_code opEc;
            beast::flat_buffer buffer;
            RawResponse response;

            beast::get_lowest_layer(stream).async_connect(endpoints, [&](const beast::error_code &ec, const tcp::endpoint &) {
                if (ec) {
                    opEc = ec;
                    return;
                }
                stream.async_handshake(ssl::stream_base::client, [&](const beast::error_code &handshakeEc) {
                    if (handshakeEc) {
                        opEc = handshakeEc;
                        return;
                    }
                    http::async_write(stream, request, [&](const beast::error_code &writeEc, std::size_t) {
                        if (writeEc) {
                            opEc = writeEc;
                            return;
                        }
                        http::async_read(stream, buffer, response, [&](const beast::error_code &readEc, std::size_t) {
                            opEc = readEc;
                        });
                    });
                });
            });

            ioc.run();

            // Deliberately does NOT call stream.shutdown() - the SSL "close_notify" handshake.
            // Unlike every other operation on this stream that overload is synchronous and so, per
            // the comment above, not covered by expires_after(): it blocks waiting for the peer to
            // send its own close_notify back, and hangs forever if that never arrives. This
            // connection is discarded either way, so a graceful close buys nothing worth risking a
            // permanent hang for.
            beast::get_lowest_layer(stream).close();

            if (opEc) throw boost::system::system_error(opEc);
            return response;
        }

    }// namespace

    bool HttpResponse::IsSuccess() const {
        return statusCode >= 200 && statusCode < 300;
    }

    boost::json::value HttpResponse::Json() const {
        return CDK::Json::Parse(body);
    }

    HttpClient::HttpClient(const std::string &baseUrl, ConnectionOptions options)
        : _baseUrl(Url::StripTrailingSlash(baseUrl)), _endpoint(Url::Split(_baseUrl)), _options(std::move(options)) {
        if (_baseUrl.empty()) throw EuclidError("baseUrl must not be empty");
        if (_endpoint.host.empty()) throw EuclidError("baseUrl '" + baseUrl + "' names no host");
    }

    const std::string &HttpClient::BaseUrl() const { return _baseUrl; }

    const ConnectionOptions &HttpClient::Options() const { return _options; }

    Request HttpClient::NewRequest(const std::string &target, const std::string &action, const std::string &body, const std::string &contentType) const {

        Request request(http::verb::post, "/", 11);
        request.set(http::field::host, Url::HostHeaderOf(_baseUrl));
        request.set(http::field::user_agent, std::string(UserAgent));
        request.set(http::field::accept, "application/json");
        request.set(http::field::content_type, contentType);
        request.set("x-euclid-target", target);
        request.set("x-euclid-action", action);
        request.body() = body;
        request.prepare_payload();
        return request;
    }

    HttpResponse HttpClient::Send(const std::string &target, const std::string &action, const Request &request, const std::chrono::milliseconds timeout) const {

        ConnectionOptions options = _options;
        if (timeout > std::chrono::milliseconds::zero()) options.timeout = timeout;

        try {
            asio::io_context ioc;
            tcp::resolver resolver(ioc);
            const auto endpoints = resolver.resolve(_endpoint.host, _endpoint.port);

            const auto raw = _endpoint.scheme == "https"
                                     ? sendTls(ioc, endpoints, _endpoint.host, request, options)
                                     : sendPlain(ioc, endpoints, request, options.timeout);

            return {.statusCode = static_cast<int>(raw.result_int()), .body = raw.body()};

        } catch (const boost::system::system_error &ex) {
            throw EuclidError("failed to reach " + _baseUrl + " (target=" + target + ", action=" + action + "): " + ex.code().message());
        }
    }

    std::string HttpClient::DefaultCaCertPath() {

        // Both places a euclid deployment is known to keep it, in the order the SDKs have always
        // looked. /etc/euclid is what euclid-pdk and euclid-ndk default to; /usr/local/euclid/etc is
        // what euclid-cli defaults to and where the tarball install actually puts it. An
        // installation with only the second one left an application failing its TLS handshake
        // against a certificate sitting on the same disk, which is a poor way to find out the two
        // conventions had drifted.
        //
        // First hit wins, and an empty answer means the system trust store alone - which is the
        // right answer for a deployment behind a certificate a real CA issued.
        static constexpr std::string_view kPaths[] = {
                "/etc/euclid/euclid_cert.crt",
                "/usr/local/euclid/etc/euclid_cert.crt",
        };

        std::error_code ec;
        for (const auto &path: kPaths) {
            if (std::filesystem::exists(path, ec)) return std::string(path);
        }
        return {};
    }

}// namespace Euclid::CDK
