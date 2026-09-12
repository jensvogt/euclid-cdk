// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <cctype>
#include <string>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/auth/HttpSignature.h>
#include <euclid/cdk/auth/SigV4.h>
#include <euclid/cdk/auth/SigningScheme.h>

namespace Euclid::CDK {

    namespace {

        class SigV4Scheme final : public SigningScheme {
        public:

            [[nodiscard]]
            std::string_view Name() const override { return "sigv4"; }

            void Sign(Request &req, const std::string &accessKeyId, const std::string &secretAccessKey, const std::string &region, const std::string &service) const override {
                CDK::SigV4::Sign(req, accessKeyId, secretAccessKey, region, service);
            }

            [[nodiscard]]
            std::optional<std::string> Verify(const Request &req, const SecretLookup &lookupSecret) const override {
                return CDK::SigV4::Verify(req, lookupSecret);
            }

            [[nodiscard]]
            const std::vector<std::string> &SignatureHeaderNames() const override {
                return CDK::SigV4::SignatureHeaderNames();
            }
        };

        class Rfc9421Scheme final : public SigningScheme {
        public:

            [[nodiscard]]
            std::string_view Name() const override { return "rfc9421"; }

            // region and service are unused: RFC 9421 signs the x-euclid-region and x-euclid-target
            // headers that already carry them, and deriving a key from a second copy would add a
            // way for the two to disagree.
            void Sign(Request &req, const std::string &accessKeyId, const std::string &secretAccessKey, const std::string &, const std::string &) const override {
                HttpSignature::Sign(req, accessKeyId, secretAccessKey);
            }

            [[nodiscard]]
            std::optional<std::string> Verify(const Request &req, const SecretLookup &lookupSecret) const override {
                return HttpSignature::Verify(req, lookupSecret);
            }

            [[nodiscard]]
            const std::vector<std::string> &SignatureHeaderNames() const override {
                return HttpSignature::SignatureHeaderNames();
            }
        };

    }// namespace

    const SigningScheme &SigningScheme::SigV4() {
        static const SigV4Scheme scheme;
        return scheme;
    }

    const SigningScheme &SigningScheme::Rfc9421() {
        static const Rfc9421Scheme scheme;
        return scheme;
    }

    const SigningScheme &SigningScheme::ByName(const std::string_view name) {
        std::string lower(name);
        std::ranges::transform(lower, lower.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower == "sigv4") return SigV4();
        if (lower == "rfc9421") return Rfc9421();
        throw EuclidError("unknown signing scheme '" + std::string(name) + "' - expected 'sigv4' or 'rfc9421'");
    }

    const SigningScheme *SigningScheme::Of(const Request &req) {
        // RFC 9421 first: it is the one that can be told apart without parsing anything, and a
        // request carrying both would be a client bug rather than a scheme to pick between.
        if (HttpSignature::IsSigned(req)) return &Rfc9421();
        if (CDK::SigV4::IsSigned(req)) return &SigV4();
        return nullptr;
    }

}// namespace Euclid::CDK
