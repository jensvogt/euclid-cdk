// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <cctype>

// Euclid includes
#include <euclid/cdk/Url.h>

namespace Euclid::CDK {

    namespace {

        std::string toLower(std::string s) {
            std::ranges::transform(s, s.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        }

        // The authority as written: everything between "://" and the first '/', '?' or '#', with
        // any userinfo dropped.
        std::string authorityOf(const std::string &url) {
            const auto separator = url.find("://");
            const std::string rest = separator == std::string::npos ? url : url.substr(separator + 3);
            const auto end = rest.find_first_of("/?#");
            const std::string authority = end == std::string::npos ? rest : rest.substr(0, end);
            const auto at = authority.rfind('@');
            return at == std::string::npos ? authority : authority.substr(at + 1);
        }

        // The port of an authority, or empty when it names none. An IPv6 literal's colons are
        // inside brackets, so the port - if there is one - is the colon after the closing bracket.
        std::string portOf(const std::string &authority) {
            const auto start = authority.front() == '[' ? authority.find(']') : 0;
            if (start == std::string::npos) return {};
            const auto colon = authority.find(':', start);
            return colon == std::string::npos ? std::string{} : authority.substr(colon + 1);
        }

        std::string defaultPortOf(const std::string &scheme) {
            return scheme == "https" ? "443" : "80";
        }

    }// namespace

    std::string Url::StripTrailingSlash(const std::string &url) {
        return url.ends_with('/') ? url.substr(0, url.size() - 1) : url;
    }

    std::string Url::SchemeOf(const std::string &url) {
        const auto separator = url.find("://");
        return separator == std::string::npos ? "https" : toLower(url.substr(0, separator));
    }

    std::string Url::HostHeaderOf(const std::string &url) {
        return toLower(authorityOf(url));
    }

    std::string Url::AuthorityOf(const std::string &url) {
        const auto host = HostHeaderOf(url);
        const auto suffix = ":" + defaultPortOf(SchemeOf(url));
        return host.ends_with(suffix) ? host.substr(0, host.size() - suffix.size()) : host;
    }

    EndpointParts Url::Split(const std::string &url) {

        const auto scheme = SchemeOf(url);
        const auto authority = authorityOf(url);
        if (authority.empty()) return {.scheme = scheme, .host = {}, .port = defaultPortOf(scheme)};

        const auto port = portOf(authority);
        auto host = port.empty() ? authority : authority.substr(0, authority.size() - port.size() - 1);

        // Boost.Asio resolves an IPv6 address without its brackets - those belong to the URL and to
        // the Host header, not to the address.
        if (host.size() > 1 && host.front() == '[' && host.back() == ']') {
            host = host.substr(1, host.size() - 2);
        }

        return {.scheme = scheme, .host = host, .port = port.empty() ? defaultPortOf(scheme) : port};
    }

}// namespace Euclid::CDK
