// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>

// Boost includes
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/auth/HttpSignature.h>
#include <euclid/cdk/auth/SigningScheme.h>

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
        HttpSignature::Sign(request, kAccessKeyId, kSecret);
        return request;
    }

    auto secretFor(const std::string &secret) {
        return [secret](const std::string &keyId) -> std::optional<std::string> {
            return keyId == kAccessKeyId ? std::optional(secret) : std::nullopt;
        };
    }

}// namespace

BOOST_AUTO_TEST_SUITE(HttpSignatureTest)

    // RFC 9530's worked example: the digest is the raw SHA-256, base64, between colons.
    BOOST_AUTO_TEST_CASE(RendersAContentDigest) {
        BOOST_TEST(HttpSignature::ContentDigest("") == "sha-256=:47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU=:");
        BOOST_TEST(HttpSignature::ContentDigest(R"({"hello": "world"})") == "sha-256=:X48E9qOokqqrvdts8nOJRJN3OWDUoyWxBf7kbu9DBPE=:");
    }

    BOOST_AUTO_TEST_CASE(SignSetsTheHeadersItPromises) {
        const auto request = signedRequest();
        BOOST_TEST(std::string(request["Content-Digest"]) == HttpSignature::ContentDigest(request.body()));
        BOOST_TEST(std::string(request["Signature-Input"]).starts_with("sig1=("));
        BOOST_TEST(std::string(request["Signature"]).starts_with("sig1=:"));
        BOOST_TEST(HttpSignature::IsSigned(request));
    }

    BOOST_AUTO_TEST_CASE(VerifiesItsOwnSignature) {
        const auto request = signedRequest();
        const auto keyId = HttpSignature::Verify(request, secretFor(kSecret));
        BOOST_REQUIRE(keyId.has_value());
        BOOST_TEST(*keyId == kAccessKeyId);
    }

    BOOST_AUTO_TEST_CASE(BuildsTheSignatureBaseRfc9421Describes) {
        Request request(http::verb::post, "/", 11);
        request.set(http::field::host, "Euclid.Example.COM");
        const auto base = HttpSignature::BuildSignatureBase(request, {"@method", "@path", "@authority"}, R"(("@method");created=1)");
        BOOST_REQUIRE(base.has_value());
        // The authority is lowercased - it is case-insensitive, so signer and verifier have to
        // agree on one spelling - and the parameters are repeated verbatim on the last line.
        BOOST_TEST(*base == "\"@method\": POST\n\"@path\": /\n\"@authority\": euclid.example.com\n\"@signature-params\": (\"@method\");created=1");
    }

    // A covered component that is not in the request has no value to sign, and treating it as
    // empty would let a signature cover a header that was then removed.
    BOOST_AUTO_TEST_CASE(RefusesToBuildABaseForAMissingComponent) {
        const Request request(http::verb::post, "/", 11);
        BOOST_TEST(!HttpSignature::BuildSignatureBase(request, {"x-euclid-target"}, "()").has_value());
    }

    BOOST_AUTO_TEST_CASE(RejectsAChangedBody) {
        auto request = signedRequest();
        request.body() = R"({"prefix":"j"})";
        request.prepare_payload();
        BOOST_TEST(!HttpSignature::Verify(request, secretFor(kSecret)).has_value());
    }

    // A body swapped together with its digest still fails: the digest header is itself covered.
    BOOST_AUTO_TEST_CASE(RejectsABodyChangedAlongWithItsDigest) {
        auto request = signedRequest();
        request.body() = R"({"prefix":"j"})";
        request.prepare_payload();
        request.set("Content-Digest", HttpSignature::ContentDigest(request.body()));
        BOOST_TEST(!HttpSignature::Verify(request, secretFor(kSecret)).has_value());
    }

    BOOST_AUTO_TEST_CASE(RejectsAChangedAction) {
        auto request = signedRequest();
        request.set("x-euclid-action", "delete-user");
        BOOST_TEST(!HttpSignature::Verify(request, secretFor(kSecret)).has_value());
    }

    BOOST_AUTO_TEST_CASE(RejectsAChangedAuthority) {
        auto request = signedRequest();
        request.set(http::field::host, "evil.example.com");
        BOOST_TEST(!HttpSignature::Verify(request, secretFor(kSecret)).has_value());
    }

    BOOST_AUTO_TEST_CASE(RejectsTheWrongSecret) {
        const auto request = signedRequest();
        BOOST_TEST(!HttpSignature::Verify(request, secretFor("not-the-secret")).has_value());
    }

    // The downgrade the fixed component list exists to refuse: a signer may not decide to cover
    // less, even if it says so honestly in Signature-Input.
    BOOST_AUTO_TEST_CASE(RejectsANarrowedComponentList) {
        auto request = signedRequest();
        request.set("Signature-Input", R"(sig1=("@method");created=1;keyid="AKIAIOSFODNN7EXAMPLE";alg="hmac-sha256")");
        BOOST_TEST(!HttpSignature::Verify(request, secretFor(kSecret)).has_value());
    }

    BOOST_AUTO_TEST_CASE(ParsesASignatureInput) {
        const auto parsed = HttpSignature::ParseSignatureInput(
                R"(sig1=("@method" "@path");created=1756728000;keyid="AKIA";alg="hmac-sha256";expires=1756731600)");
        BOOST_REQUIRE(parsed.has_value());
        BOOST_TEST(parsed->label == "sig1");
        BOOST_TEST(parsed->components.size() == 2U);
        BOOST_TEST(parsed->components[0] == "@method");
        BOOST_TEST(parsed->components[1] == "@path");
        BOOST_TEST(parsed->keyId == "AKIA");
        BOOST_TEST(parsed->algorithm == "hmac-sha256");
        BOOST_TEST(parsed->created == 1756728000L);
        BOOST_TEST(parsed->expires == 1756731600L);
    }

    BOOST_AUTO_TEST_CASE(RejectsAnUnparsableSignatureInput) {
        BOOST_TEST(!HttpSignature::ParseSignatureInput("").has_value());
        BOOST_TEST(!HttpSignature::ParseSignatureInput("sig1").has_value());
        BOOST_TEST(!HttpSignature::ParseSignatureInput("sig1=;created=1").has_value());
        BOOST_TEST(!HttpSignature::ParseSignatureInput("sig1=();created=1").has_value());
    }

    // euclid's own signer emits no "tag", so a verifier that required one would reject the server.
    // One that arrives is accepted and ignored.
    BOOST_AUTO_TEST_CASE(IgnoresATagParameter) {
        auto request = signedRequest();
        const auto input = std::string(request["Signature-Input"]);
        BOOST_TEST(input.find(";tag=") == std::string::npos);
    }

    // The list is a wire format shared with the server and every other SDK, order included.
    BOOST_AUTO_TEST_CASE(CoversTheAgreedComponentList) {
        const std::vector<std::string> expected{
                "@method", "@path", "@authority", "content-digest",
                "x-euclid-account-id", "x-euclid-action", "x-euclid-region", "x-euclid-target", "x-euclid-user-id"};
        BOOST_TEST(HttpSignature::CoveredComponents() == expected, boost::test_tools::per_element());
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(SigningSchemeTest)

    BOOST_AUTO_TEST_CASE(NamesItsSchemes) {
        BOOST_TEST(SigningScheme::SigV4().Name() == "sigv4");
        BOOST_TEST(SigningScheme::Rfc9421().Name() == "rfc9421");
        BOOST_TEST(&SigningScheme::ByName("SIGV4") == &SigningScheme::SigV4());
        BOOST_TEST(&SigningScheme::ByName("rfc9421") == &SigningScheme::Rfc9421());
        BOOST_CHECK_THROW(std::ignore = SigningScheme::ByName("hmac"), EuclidError);
    }

    // The two do not collide on the wire, so a server can accept both and tell which was used.
    BOOST_AUTO_TEST_CASE(TellsTheSchemesApart) {
        const auto rfc = signedRequest();
        BOOST_REQUIRE(SigningScheme::Of(rfc) != nullptr);
        BOOST_TEST(SigningScheme::Of(rfc)->Name() == "rfc9421");

        Request sigv4(http::verb::post, "/", 11);
        sigv4.set(http::field::host, "euclid.example.com");
        sigv4.prepare_payload();
        SigningScheme::SigV4().Sign(sigv4, kAccessKeyId, kSecret, "eu-central-1", "eam");
        BOOST_REQUIRE(SigningScheme::Of(sigv4) != nullptr);
        BOOST_TEST(SigningScheme::Of(sigv4)->Name() == "sigv4");

        Request bearer(http::verb::post, "/", 11);
        bearer.set(http::field::authorization, "Bearer token");
        BOOST_TEST(SigningScheme::Of(bearer) == nullptr);
    }

BOOST_AUTO_TEST_SUITE_END()
