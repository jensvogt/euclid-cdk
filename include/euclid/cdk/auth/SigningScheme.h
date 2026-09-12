// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/http/Message.h>

namespace Euclid::CDK {

    /**
     * @brief Resolves an access key ID to its secret, or std::nullopt when the key is unknown.
     */
    using SecretLookup = std::function<std::optional<std::string>(const std::string &)>;

    /**
     * @brief Which request-signing scheme a client uses when it authenticates with an access key.
     *
     * @par
     * Two schemes exist because euclid is moving from one to the other, not because both are wanted
     * in the end. SigV4() is what euclid has always spoken; Rfc9421() is the standard scheme meant
     * to replace it, and is what euclid-cli signs with today, so it is the default here too.
     * Choosing one is a per-session decision, so a deployment can move one service at a time and
     * roll back by changing a line rather than a release.
     *
     * @par
     * Neither is consulted when a client authenticates with a bearer token: with no access key
     * there is nothing to sign with, and the token goes in Authorization as before.
     *
     * @par
     * The two do not collide on the wire - SigV4 puts its signature in Authorization, RFC 9421 in
     * Signature/Signature-Input - so a server can accept both at once and tell which a request
     * used. Of() is that test.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API SigningScheme {
    public:

        virtual ~SigningScheme() = default;

        /**
         * @brief The scheme's name, "sigv4" or "rfc9421".
         */
        [[nodiscard]]
        virtual std::string_view Name() const = 0;

        /**
         * @brief Signs a request in place.
         *
         * @par
         * region and service are ignored by Rfc9421(): SigV4 needs them to derive its signing key,
         * whereas RFC 9421 signs the x-euclid-region and x-euclid-target headers that carry the
         * same facts, and binding them twice would add a way for the two copies to disagree.
         *
         * @param req             request to sign; mutated in place.
         * @param accessKeyId     the caller's access key ID.
         * @param secretAccessKey the caller's secret access key.
         * @param region          region to scope the signature to.
         * @param service         module to scope the signature to.
         */
        virtual void Sign(Request &req, const std::string &accessKeyId, const std::string &secretAccessKey, const std::string &region, const std::string &service) const = 0;

        /**
         * @brief Verifies a request signed with this scheme.
         *
         * @param req          the request to verify.
         * @param lookupSecret resolves an access key ID to its secret.
         * @return the access key ID on success, std::nullopt on any failure.
         */
        [[nodiscard]]
        virtual std::optional<std::string> Verify(const Request &req, const SecretLookup &lookupSecret) const = 0;

        /**
         * @brief The headers Sign() writes, for callers that copy them onto another request.
         */
        [[nodiscard]]
        virtual const std::vector<std::string> &SignatureHeaderNames() const = 0;

        /**
         * @brief What euclid has always spoken.
         */
        [[nodiscard]]
        static const SigningScheme &SigV4();

        /**
         * @brief The standard scheme meant to replace SigV4, and this SDK's default.
         */
        [[nodiscard]]
        static const SigningScheme &Rfc9421();

        /**
         * @brief A scheme by name, for a value that came out of a configuration file.
         *
         * @param name "sigv4" or "rfc9421"; matched case-insensitively.
         * @return the named scheme.
         * @throws EuclidError if the name is neither.
         */
        [[nodiscard]]
        static const SigningScheme &ByName(std::string_view name);

        /**
         * @brief Which scheme, if either, a received request presents a signature for.
         *
         * @par
         * A routing decision, not a verification: it says which Verify() to call and nothing about
         * whether that call will succeed. A null result means the request presents no signature at
         * all - a bearer-token request, or an unsigned one.
         *
         * @param req request to inspect.
         * @return the scheme, or nullptr.
         */
        [[nodiscard]]
        static const SigningScheme *Of(const Request &req);
    };

}// namespace Euclid::CDK
