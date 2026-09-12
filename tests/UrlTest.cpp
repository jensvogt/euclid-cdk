// SPDX-License-Identifier: Apache-2.0

// Boost includes
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Url.h>

using namespace Euclid::CDK;

BOOST_AUTO_TEST_SUITE(UrlTest)

    BOOST_AUTO_TEST_CASE(StripsOneTrailingSlash) {
        BOOST_TEST(Url::StripTrailingSlash("https://euclid.example.com/") == "https://euclid.example.com");
        BOOST_TEST(Url::StripTrailingSlash("https://euclid.example.com") == "https://euclid.example.com");
    }

    BOOST_AUTO_TEST_CASE(ReadsTheScheme) {
        BOOST_TEST(Url::SchemeOf("HTTPS://euclid.example.com") == "https");
        BOOST_TEST(Url::SchemeOf("http://euclid.example.com") == "http");
        // A bare authority is https, because that is how euclid is reached anywhere the
        // distinction can matter.
        BOOST_TEST(Url::SchemeOf("euclid.example.com") == "https");
    }

    // The whole point of parsing this by hand: a port that was written out stays in the Host
    // header even when it is the scheme's default, because euclid compares the Host it received
    // against the one the signature covers.
    BOOST_AUTO_TEST_CASE(KeepsADefaultPortInTheHostHeader) {
        BOOST_TEST(Url::HostHeaderOf("https://euclid.example.com:443") == "euclid.example.com:443");
        BOOST_TEST(Url::HostHeaderOf("https://euclid.example.com") == "euclid.example.com");
        BOOST_TEST(Url::HostHeaderOf("http://euclid.example.com:5566") == "euclid.example.com:5566");
    }

    BOOST_AUTO_TEST_CASE(HostHeaderDropsUserinfoAndPath) {
        BOOST_TEST(Url::HostHeaderOf("https://alice:secret@euclid.example.com/path?q=1") == "euclid.example.com");
    }

    // RFC 9421 §2.2.3, the other way round: "@authority" drops the default port even when the URL
    // wrote it, which is why it is not simply the Host header.
    BOOST_AUTO_TEST_CASE(AuthorityDropsTheDefaultPort) {
        BOOST_TEST(Url::AuthorityOf("https://euclid.example.com:443") == "euclid.example.com");
        BOOST_TEST(Url::AuthorityOf("http://euclid.example.com:80") == "euclid.example.com");
        BOOST_TEST(Url::AuthorityOf("https://euclid.example.com:5566") == "euclid.example.com:5566");
    }

    BOOST_AUTO_TEST_CASE(SplitsIntoSchemeHostAndPort) {
        const auto [scheme, host, port] = Url::Split("https://euclid.example.com:5566/ignored");
        BOOST_TEST(scheme == "https");
        BOOST_TEST(host == "euclid.example.com");
        BOOST_TEST(port == "5566");
    }

    BOOST_AUTO_TEST_CASE(SplitFillsInTheDefaultPort) {
        BOOST_TEST(Url::Split("https://euclid.example.com").port == "443");
        BOOST_TEST(Url::Split("http://euclid.example.com").port == "80");
    }

    // The brackets belong to the URL and to the Host header, not to the address a resolver takes.
    BOOST_AUTO_TEST_CASE(SplitUnwrapsAnIpv6Literal) {
        const auto parts = Url::Split("http://[::1]:5566");
        BOOST_TEST(parts.host == "::1");
        BOOST_TEST(parts.port == "5566");
        BOOST_TEST(Url::HostHeaderOf("http://[::1]:5566") == "[::1]:5566");
    }

BOOST_AUTO_TEST_SUITE_END()
