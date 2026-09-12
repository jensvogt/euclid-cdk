// SPDX-License-Identifier: Apache-2.0

// Boost includes
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/auth/SigV4.h>

using namespace Euclid::CDK;

namespace {

    namespace http = boost::beast::http;

    constexpr auto kAccessKeyId = "AKIAIOSFODNN7EXAMPLE";
    constexpr auto kSecret = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

    Request signedRequest(const std::string &body = R"({"prefix":""})") {
        Request request(http::verb::post, "/", 11);
        request.set(http::field::host, "euclid.example.com");
        request.set("x-euclid-target", "eam");
        request.set("x-euclid-action", "list-users");
        request.set("x-euclid-region", "eu-central-1");
        request.set("x-euclid-account-id", "000000000000");
        request.set("x-euclid-user-id", "jens");
        request.body() = body;
        request.prepare_payload();
        SigV4::Sign(request, kAccessKeyId, kSecret, "eu-central-1", "eam");
        return request;
    }

    auto secretFor(const std::string &secret) {
        return [secret](const std::string &keyId) -> std::optional<std::string> {
            return keyId == kAccessKeyId ? std::optional(secret) : std::nullopt;
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(SigV4Test)

    // AWS's own worked example: the parameters sort by name, and each is re-encoded rather than
    // passed through.
    BOOST_AUTO_TEST_CASE(CanonicalizesAQueryString) {
        BOOST_TEST(SigV4::CanonicalizeQueryString("Param2=value2&Param1=value1") == "Param1=value1&Param2=value2");
        BOOST_TEST(SigV4::CanonicalizeQueryString("") == "");
        BOOST_TEST(SigV4::CanonicalizeQueryString("a=b c") == "a=b%20c");
        // A value that arrives percent-encoded is decoded and re-encoded, so two spellings of one
        // value canonicalize alike.
        BOOST_TEST(SigV4::CanonicalizeQueryString("a=b%20c") == "a=b%20c");
    }

    // The kDate -> kRegion -> kService -> kSigning chain, against a value computed by an
    // independent implementation of it rather than by this one. Pinned because the chain is what
    // makes the same secret produce the same signature on the server.
    BOOST_AUTO_TEST_CASE(DerivesTheSigningKeyFromTheHmacChain) {
        const auto key = SigV4::DeriveSigningKey(kSecret, "20150830", "us-east-1", "iam");
        BOOST_TEST(Crypto::ToHexLower(key) == "2c94c0cf5378ada6887f09bb697df8fc0affdb34ba1cdd5bda32b664bd55b73c");
    }

    BOOST_AUTO_TEST_CASE(BuildsTheCanonicalRequest) {
        const std::map<std::string, std::string> headers{{"host", "euclid.example.com"}, {"x-amz-date", "20260818T120000Z"}};
        const auto canonical = SigV4::BuildCanonicalRequest("POST", "/", "", headers, {"host", "x-amz-date"}, "abc");
        BOOST_TEST(canonical == "POST\n/\n\nhost:euclid.example.com\nx-amz-date:20260818T120000Z\n\nhost;x-amz-date\nabc");
    }

    // A header the request never carried canonicalizes as empty rather than being skipped, which
    // is what makes the signed list fixed rather than negotiated.
    BOOST_AUTO_TEST_CASE(AMissingSignedHeaderCanonicalizesAsEmpty) {
        const auto canonical = SigV4::BuildCanonicalRequest("POST", "/", "", {}, {"host"}, "abc");
        BOOST_TEST(canonical == "POST\n/\n\nhost:\n\nhost\nabc");
    }

    BOOST_AUTO_TEST_CASE(SignSetsTheHeadersItPromises) {
        const auto request = signedRequest();
        BOOST_TEST(!request["x-amz-date"].empty());
        BOOST_TEST(std::string(request["x-amz-content-sha256"]) == Crypto::Sha256Hex(request.body()));
        BOOST_TEST(std::string(request[http::field::authorization]).starts_with("AWS4-HMAC-SHA256 "));
        BOOST_TEST(SigV4::IsSigned(request));
    }

    BOOST_AUTO_TEST_CASE(VerifiesItsOwnSignature) {
        const auto request = signedRequest();
        const auto keyId = SigV4::Verify(request, secretFor(kSecret));
        BOOST_REQUIRE(keyId.has_value());
        BOOST_TEST(*keyId == kAccessKeyId);
    }

    BOOST_AUTO_TEST_CASE(RejectsAChangedBody) {
        auto request = signedRequest();
        request.body() = R"({"prefix":"j"})";
        request.prepare_payload();
        BOOST_TEST(!SigV4::Verify(request, secretFor(kSecret)).has_value());
    }

    // The whole reason the x-euclid-* headers are signed: an action swapped in flight must not
    // still verify.
    BOOST_AUTO_TEST_CASE(RejectsAChangedAction) {
        auto request = signedRequest();
        request.set("x-euclid-action", "delete-user");
        BOOST_TEST(!SigV4::Verify(request, secretFor(kSecret)).has_value());
    }

    BOOST_AUTO_TEST_CASE(RejectsAChangedHost) {
        auto request = signedRequest();
        request.set(http::field::host, "evil.example.com");
        BOOST_TEST(!SigV4::Verify(request, secretFor(kSecret)).has_value());
    }

    BOOST_AUTO_TEST_CASE(RejectsTheWrongSecret) {
        const auto request = signedRequest();
        BOOST_TEST(!SigV4::Verify(request, secretFor("not-the-secret")).has_value());
    }

    BOOST_AUTO_TEST_CASE(RejectsAnUnknownKey) {
        const auto request = signedRequest();
        BOOST_TEST(!SigV4::Verify(request, [](const std::string &) { return std::nullopt; }).has_value());
    }

    // The downgrade the fixed list exists to refuse: a signer that covered fewer headers, and an
    // Authorization header that says so, is rejected rather than verified on its own terms.
    BOOST_AUTO_TEST_CASE(RejectsANarrowedSignedHeaderList) {
        auto request = signedRequest();
        request.set(http::field::authorization, "AWS4-HMAC-SHA256 Credential=" + std::string(kAccessKeyId) +
                                                        "/20260818/eu-central-1/eam/aws4_request, SignedHeaders=host, Signature=deadbeef");
        BOOST_TEST(!SigV4::Verify(request, secretFor(kSecret)).has_value());
    }

    BOOST_AUTO_TEST_CASE(RejectsAStaleTimestamp) {
        auto request = signedRequest();
        request.set("x-amz-date", "20200101T000000Z");
        BOOST_TEST(!SigV4::Verify(request, secretFor(kSecret)).has_value());
    }

    BOOST_AUTO_TEST_CASE(ParsesAnAuthorizationHeader) {
        const auto parsed = SigV4::ParseAuthorizationHeader(
                "AWS4-HMAC-SHA256 Credential=AKIA/20260818/eu-central-1/eam/aws4_request, SignedHeaders=host;x-amz-date, Signature=abc");
        BOOST_REQUIRE(parsed.has_value());
        BOOST_TEST(parsed->scope.accessKeyId == "AKIA");
        BOOST_TEST(parsed->scope.dateStamp == "20260818");
        BOOST_TEST(parsed->scope.region == "eu-central-1");
        BOOST_TEST(parsed->scope.service == "eam");
        BOOST_TEST(parsed->signedHeaders == "host;x-amz-date");
        BOOST_TEST(parsed->signature == "abc");
    }

    BOOST_AUTO_TEST_CASE(RejectsAnUnparsableAuthorizationHeader) {
        BOOST_TEST(!SigV4::ParseAuthorizationHeader("").has_value());
        BOOST_TEST(!SigV4::ParseAuthorizationHeader("Bearer token").has_value());
        BOOST_TEST(!SigV4::ParseAuthorizationHeader("AWS4-HMAC-SHA256 Credential=a/b/c, Signature=x").has_value());
    }

    // The list is a wire format shared with the server and every other SDK; changing it here alone
    // would silently stop every signature from verifying.
    BOOST_AUTO_TEST_CASE(SignsTheAgreedHeaderList) {
        const std::vector<std::string> expected{
                "host", "x-amz-content-sha256", "x-amz-date",
                "x-euclid-account-id", "x-euclid-action", "x-euclid-region", "x-euclid-target", "x-euclid-user-id"};
        BOOST_TEST(SigV4::SignedHeaderNames() == expected, boost::test_tools::per_element());
    }

BOOST_AUTO_TEST_SUITE_END()
