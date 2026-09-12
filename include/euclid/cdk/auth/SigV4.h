// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/http/Message.h>

namespace Euclid::CDK {

    /**
     * @brief AWS Signature Version 4 request signing and verification.
     *
     * @par
     * A port of the euclid server's Core::SigV4, kept deliberately line-for-line: what a signature
     * covers and how it is canonicalized is a wire format shared by the server, euclid-cli and
     * every SDK, so the two implementations have to agree byte for byte or nothing verifies.
     *
     * @par
     * Adapted to euclid's own request shape: unlike real AWS, the target service and the action
     * live in custom "x-euclid-*" headers rather than in the URI, so those headers are always part
     * of the signed set - a fixed, non-negotiable list rather than a client-chosen "SignedHeaders"
     * list, so nothing in transit can narrow what a signature actually covers and still verify.
     *
     * @par Verification
     * Verification is here as well as signing, because it is the only way to demonstrate that the
     * two canonicalizations are one canonicalization, and because a C++ service fronting euclid
     * has to check the signatures it receives with the rules the server applies.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API SigV4 {
    public:

        /**
         * @brief The algorithm as it appears in the Authorization header.
         */
        static constexpr std::string_view Algorithm = "AWS4-HMAC-SHA256";

        /**
         * @brief Headers that are always part of the signature, in the (already alphabetical)
         * order the canonical request requires.
         *
         * @par
         * host and x-amz-content-sha256/x-amz-date give the usual SigV4 transport and payload
         * integrity; the x-euclid-* headers are signed too because that is where euclid carries
         * the routing information AWS would put in the URI, and a signature that left them out
         * would authenticate a request without authenticating what it asks for.
         *
         * @par
         * Compare against Core::SigV4::SignedHeaderNames() before changing anything here - the
         * server compares the presented list against its own for exact equality, order included.
         */
        static const std::vector<std::string> &SignedHeaderNames();

        /**
         * @brief The headers Sign() writes, in the case they are sent in, for callers that copy
         * them onto some other request object afterwards.
         */
        static const std::vector<std::string> &SignatureHeaderNames();

        /**
         * @brief The "<accessKeyId>/<date>/<region>/<service>/aws4_request" scope parsed out of an
         * Authorization header.
         */
        struct CredentialScope {
            std::string accessKeyId;
            std::string dateStamp;// YYYYMMDD
            std::string region;
            std::string service;
        };

        /**
         * @brief An Authorization header, split into its components.
         */
        struct ParsedAuthorization {
            CredentialScope scope;
            std::string signedHeaders;// as literally presented, semicolon-joined
            std::string signature;    // lowercase hex
        };

        /**
         * @brief Parses "AWS4-HMAC-SHA256 Credential=..., SignedHeaders=..., Signature=...".
         *
         * @param headerValue raw value of the Authorization header.
         * @return the parsed components, or std::nullopt if the header is not present or not
         * well-formed.
         */
        [[nodiscard]]
        static std::optional<ParsedAuthorization> ParseAuthorizationHeader(const std::string &headerValue);

        /**
         * @brief Canonicalizes a raw query string: percent-decodes each name and value, re-encodes
         * per SigV4's URI-encoding rules, and sorts by name then value.
         *
         * @par
         * Exposed standalone (rather than folded into Sign/Verify) so it can be tested directly
         * against AWS's published SigV4 test vectors.
         *
         * @param rawQuery the query string as it appears on the wire, without the leading '?',
         * e.g. "Param2=value2&Param1=value1". Empty if the request has none.
         * @return the canonical query string, e.g. "Param1=value1&Param2=value2".
         */
        [[nodiscard]]
        static std::string CanonicalizeQueryString(const std::string &rawQuery);

        /**
         * @brief Builds the SigV4 canonical request string.
         *
         * @param method            HTTP method, e.g. "POST".
         * @param canonicalUri      URI-encoded path, e.g. "/".
         * @param canonicalQuery    canonical query string, empty if none.
         * @param headers           all request headers, keyed by lowercase name.
         * @param signedHeaderNames headers to include, already sorted - see SignedHeaderNames().
         * @param payloadHashHex    lowercase hex SHA-256 of the request body.
         * @return the canonical request string.
         */
        [[nodiscard]]
        static std::string BuildCanonicalRequest(const std::string &method, const std::string &canonicalUri, const std::string &canonicalQuery,
                                                 const std::map<std::string, std::string> &headers, const std::vector<std::string> &signedHeaderNames,
                                                 const std::string &payloadHashHex);

        /**
         * @brief Builds the SigV4 string-to-sign.
         *
         * @param amzDate                 full request timestamp, e.g. "20260818T120000Z".
         * @param credentialScope         "<date>/<region>/<service>/aws4_request".
         * @param canonicalRequestHashHex lowercase hex SHA-256 of the canonical request.
         * @return the string-to-sign.
         */
        [[nodiscard]]
        static std::string BuildStringToSign(const std::string &amzDate, const std::string &credentialScope, const std::string &canonicalRequestHashHex);

        /**
         * @brief Derives the SigV4 signing key via the kDate -> kRegion -> kService -> kSigning
         * HMAC-SHA256 chain.
         *
         * @param secretAccessKey the caller's secret.
         * @param dateStamp       "YYYYMMDD".
         * @param region          e.g. "eu-central-1".
         * @param service         e.g. "eqs".
         * @return the derived signing key, raw bytes.
         */
        [[nodiscard]]
        static std::vector<unsigned char> DeriveSigningKey(const std::string &secretAccessKey, const std::string &dateStamp, const std::string &region, const std::string &service);

        /**
         * @brief Signs a request in place: sets x-amz-date, x-amz-content-sha256 and Authorization.
         *
         * @par
         * Call after every other header the signature must cover (host, x-euclid-target,
         * x-euclid-action, x-euclid-region, x-euclid-account-id, x-euclid-user-id) and the body are
         * already set on req.
         *
         * @param req             request to sign; mutated in place.
         * @param accessKeyId     the caller's access key ID.
         * @param secretAccessKey the caller's secret access key.
         * @param region          region to scope the signature to, e.g. "eu-central-1".
         * @param service         module to scope the signature to, e.g. "eam".
         */
        static void Sign(Request &req, const std::string &accessKeyId, const std::string &secretAccessKey, const std::string &region, const std::string &service);

        /**
         * @brief Whether a request presents a SigV4 signature at all.
         *
         * @param req request to inspect.
         * @return true if the Authorization header names this algorithm.
         */
        [[nodiscard]]
        static bool IsSigned(const Request &req);

        /**
         * @brief Verifies a SigV4-signed request.
         *
         * @par
         * Recomputes the signature from the request exactly as received and compares it in
         * constant time against the one presented in Authorization - any change to a signed header
         * or to the body between signing and here makes this fail.
         *
         * @param req          the request to verify.
         * @param lookupSecret resolves an access key ID to its secret, or std::nullopt if unknown.
         * @param maxSkew      maximum age, in either direction, x-amz-date may have relative to now
         * before the request is rejected as stale or replayed.
         * @return the resolved access key ID on success, std::nullopt on any failure - missing or
         * malformed header, unknown key, stale timestamp, or signature mismatch. Callers do not get
         * to distinguish which, mirroring how JWT verification collapses failure modes into one
         * rejection.
         */
        [[nodiscard]]
        static std::optional<std::string> Verify(const Request &req,
                                                 const std::function<std::optional<std::string>(const std::string &)> &lookupSecret,
                                                 std::chrono::seconds maxSkew = std::chrono::minutes(15));
    };

}// namespace Euclid::CDK
