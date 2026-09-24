// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>

// Boost includes
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Credentials.h>
#include <euclid/cdk/Crypto.h>

using namespace Euclid::CDK;

namespace {

    long nowSeconds() {
        return static_cast<long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    }

    // A JWT is three base64url segments; only the middle one is ever read here, and only for its
    // "exp" - the signature is the server's to check.
    std::string tokenExpiring(const long at) {
        const auto payload = boost::json::serialize(boost::json::object{{"exp", at}, {"sub", "jens"}});
        auto encoded = Crypto::Base64Encode(payload);
        std::ranges::replace(encoded, '+', '-');
        std::ranges::replace(encoded, '/', '_');
        std::erase(encoded, '=');
        return "header." + encoded + ".signature";
    }

    Credentials::Entry entry() {
        return {
                .token = tokenExpiring(nowSeconds() + 3600),
                .userId = "jens",
                .accountId = "000000000000",
                .region = "eu-central-1",
                .accessKeyId = "AKIAIOSFODNN7EXAMPLE",
                .secretAccessKey = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
                .isAdmin = true,
                .nameSpace = "reports",
                .baseUrl = "https://euclid.example.com",
        };
    }

    std::string fileContents() {
        std::ifstream in(Credentials::FilePath());
        return {std::istreambuf_iterator(in), std::istreambuf_iterator<char>()};
    }

}// namespace

BOOST_AUTO_TEST_SUITE(CredentialsTest)

    BOOST_AUTO_TEST_CASE(RoundTripsASession) {
        Credentials::Save(entry());
        const auto loaded = Credentials::Load();
        BOOST_REQUIRE(loaded.has_value());
        BOOST_TEST(loaded->userId == "jens");
        BOOST_TEST(loaded->accountId == "000000000000");
        BOOST_TEST(loaded->region == "eu-central-1");
        BOOST_TEST(loaded->accessKeyId == "AKIAIOSFODNN7EXAMPLE");
        BOOST_TEST(loaded->secretAccessKey == "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY");
        BOOST_TEST(loaded->isAdmin);
        BOOST_TEST(loaded->nameSpace == "reports");
        BOOST_TEST(loaded->baseUrl == "https://euclid.example.com");
    }

    // The field names are a wire format, not an implementation detail: euclid-cli reads this same
    // file, and it looks for "namespace" rather than the C++ member's "nameSpace".
    BOOST_AUTO_TEST_CASE(WritesTheFieldNamesTheOtherClientsRead) {
        Credentials::Save(entry());
        const auto document = boost::json::parse(fileContents());
        BOOST_REQUIRE(document.is_object());
        const auto &object = document.as_object();
        BOOST_TEST(object.contains("namespace"));
        BOOST_TEST(!object.contains("nameSpace"));
        BOOST_TEST(object.contains("token"));
        BOOST_TEST(object.contains("isAdmin"));
        BOOST_TEST(object.contains("baseUrl"));
    }

    BOOST_AUTO_TEST_CASE(UpdatesTheNamespaceOfTheMatchingServerOnly) {
        Credentials::Save(entry());

        Credentials::UpdateNamespace("https://somewhere.else", "other");
        BOOST_TEST(Credentials::Load()->nameSpace == "reports");

        Credentials::UpdateNamespace("https://euclid.example.com", "archive");
        BOOST_TEST(Credentials::Load()->nameSpace == "archive");

        // An empty namespace is an instruction of its own - it clears the scope.
        Credentials::UpdateNamespace("https://euclid.example.com", "");
        BOOST_TEST(Credentials::Load()->nameSpace.empty());
    }

    BOOST_AUTO_TEST_CASE(LoadsNothingWhenThereIsNoFile) {
        Credentials::Clear();
        BOOST_TEST(!Credentials::Load().has_value());
    }

    BOOST_AUTO_TEST_CASE(LoadsNothingFromAFileWithoutAToken) {
        std::ofstream(Credentials::FilePath(), std::ios::trunc) << R"({"userId": "jens"})";
        BOOST_TEST(!Credentials::Load().has_value());
        Credentials::Clear();
    }

    BOOST_AUTO_TEST_CASE(ReadsATokensExpiry) {
        BOOST_TEST(Credentials::IsTokenValid(tokenExpiring(nowSeconds() + 3600)));
        BOOST_TEST(!Credentials::IsTokenValid(tokenExpiring(nowSeconds() - 1)));
    }

    BOOST_AUTO_TEST_CASE(RejectsATokenItCannotRead) {
        BOOST_TEST(!Credentials::IsTokenValid(""));
        BOOST_TEST(!Credentials::IsTokenValid("not-a-jwt"));
        BOOST_TEST(!Credentials::IsTokenValid("header..signature"));
        // Well-formed but with nothing to go on: a token that never expires is not something this
        // can vouch for, so it is treated as one that cannot be reused.
        BOOST_TEST(!Credentials::IsTokenValid("header." + Crypto::Base64Encode(R"({"sub":"jens"})") + ".signature"));
    }

    // The file a euclid-managed application is handed, which the manager writes: the server is
    // called "endpoint" there and "baseUrl" in a file this SDK wrote. Reading only "baseUrl" left an
    // application with a valid token and nowhere to send it - the one field it cannot do without,
    // and the one the header's own promise about EUCLID_CREDENTIALS_FILE depends on.
    BOOST_AUTO_TEST_CASE(ReadsTheServerFromAManagedApplicationsCredentials) {

        std::ofstream(Credentials::FilePath(), std::ios::trunc) << boost::json::serialize(boost::json::object{
                {"token", tokenExpiring(nowSeconds() + 3600)},
                {"expiresAt", "2026-09-24T15:07:36.000Z"},
                {"userId", "app-echo-worker"},
                {"accountId", "000000000000"},
                {"region", "eu-central-1"},
                {"namespace", "development"},
                {"endpoint", "https://localhost:5566"}});

        const auto loaded = Credentials::Load();
        BOOST_REQUIRE(loaded.has_value());
        BOOST_TEST(loaded->baseUrl == "https://localhost:5566");
        BOOST_TEST(loaded->userId == "app-echo-worker");
        BOOST_TEST(loaded->nameSpace == "development");
        // No access key at all: a technical principal's secret never leaves EAM, so the token is
        // the whole of what the process holds.
        BOOST_TEST(loaded->accessKeyId.empty());

        Credentials::Clear();
    }

    // And "baseUrl" still wins where both are present, so a file this SDK wrote is unaffected.
    BOOST_AUTO_TEST_CASE(PrefersBaseUrlWhenTheFileCarriesBoth) {

        std::ofstream(Credentials::FilePath(), std::ios::trunc) << boost::json::serialize(boost::json::object{
                {"token", tokenExpiring(nowSeconds() + 3600)},
                {"baseUrl", "https://euclid.example.com"},
                {"endpoint", "https://localhost:5566"}});

        const auto loaded = Credentials::Load();
        BOOST_REQUIRE(loaded.has_value());
        BOOST_TEST(loaded->baseUrl == "https://euclid.example.com");

        Credentials::Clear();
    }

BOOST_AUTO_TEST_SUITE_END()
