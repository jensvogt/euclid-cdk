// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

// Euclid includes
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/auth/SigV4.h>

namespace Euclid::CDK {

    namespace {

        namespace http = boost::beast::http;

        constexpr std::string_view kScopeTerminator = "aws4_request";

        std::vector<unsigned char> toBytes(const std::string &s) {
            return {s.begin(), s.end()};
        }

        // Whether c is one of SigV4's unreserved characters (the RFC 3986 unreserved set) - the
        // only characters left un-percent-encoded.
        bool isUnreserved(const unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
        }

        // SigV4's URI-encoding: percent-encode everything except unreserved characters, with
        // uppercase hex digits. '/' is kept literal in the path (encodeSlash=false) but treated
        // like any other character in query keys and values (encodeSlash=true).
        std::string uriEncode(const std::string &value, const bool encodeSlash) {
            std::ostringstream oss;
            oss << std::hex << std::uppercase << std::setfill('0');
            for (const unsigned char c: value) {
                if (isUnreserved(c) || (c == '/' && !encodeSlash)) {
                    oss << static_cast<char>(c);
                } else {
                    oss << '%' << std::setw(2) << static_cast<int>(c);
                }
            }
            return oss.str();
        }

        std::string percentDecode(const std::string &value) {
            std::string out;
            out.reserve(value.size());
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '%' && i + 2 < value.size() && std::isxdigit(static_cast<unsigned char>(value[i + 1])) && std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
                    out += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                } else {
                    out += value[i];
                }
            }
            return out;
        }

        std::string trim(const std::string &s) {
            const auto begin = s.find_first_not_of(" \t");
            if (begin == std::string::npos) return "";
            const auto end = s.find_last_not_of(" \t");
            return s.substr(begin, end - begin + 1);
        }

        std::vector<std::string> split(const std::string &s, const char delim) {
            std::vector<std::string> parts;
            std::size_t start = 0;
            while (start <= s.size()) {
                const auto pos = s.find(delim, start);
                if (pos == std::string::npos) {
                    parts.push_back(s.substr(start));
                    break;
                }
                parts.push_back(s.substr(start, pos - start));
                start = pos + 1;
            }
            return parts;
        }

        // Formats now as SigV4's full timestamp ("20260818T120000Z") and returns the date-only
        // prefix ("20260818") alongside it.
        std::pair<std::string, std::string> nowAmzDate() {
            const std::time_t t = std::time(nullptr);
            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &t);
#else
            gmtime_r(&t, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
            return {std::string(buf), std::string(buf, 8)};
        }

        // Parses a SigV4 timestamp ("20260818T120000Z") back to a time_point, for skew checking.
        std::optional<std::chrono::system_clock::time_point> parseAmzDate(const std::string &amzDate) {
            std::tm tm{};
            std::istringstream iss(amzDate);
            iss >> std::get_time(&tm, "%Y%m%dT%H%M%SZ");
            if (iss.fail()) return std::nullopt;
#if defined(_WIN32)
            const std::time_t t = _mkgmtime(&tm);
#else
            const std::time_t t = timegm(&tm);
#endif
            return std::chrono::system_clock::from_time_t(t);
        }

        std::map<std::string, std::string> headerMap(const Request &req) {
            std::map<std::string, std::string> headers;
            for (const auto &field: req) {
                std::string name(field.name_string());
                std::ranges::transform(name, name.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                headers[name] = trim(std::string(field.value()));
            }
            return headers;
        }

        // The signed header list as it appears in the Authorization header, and as Verify()
        // insists on receiving it back.
        std::string signedHeaderList() {
            std::string joined;
            for (std::size_t i = 0; i < SigV4::SignedHeaderNames().size(); ++i) {
                if (i > 0) joined += ';';
                joined += SigV4::SignedHeaderNames()[i];
            }
            return joined;
        }

    }// namespace

    const std::vector<std::string> &SigV4::SignedHeaderNames() {
        static const std::vector<std::string> kNames = {
                "host", "x-amz-content-sha256", "x-amz-date",
                "x-euclid-account-id", "x-euclid-action", "x-euclid-region", "x-euclid-target", "x-euclid-user-id"};
        return kNames;
    }

    const std::vector<std::string> &SigV4::SignatureHeaderNames() {
        static const std::vector<std::string> kNames = {"x-amz-date", "x-amz-content-sha256", "Authorization"};
        return kNames;
    }

    std::string SigV4::CanonicalizeQueryString(const std::string &rawQuery) {
        if (rawQuery.empty()) return "";

        std::vector<std::pair<std::string, std::string>> params;
        for (const auto &pair: split(rawQuery, '&')) {
            if (pair.empty()) continue;
            const auto eq = pair.find('=');
            const std::string rawName = eq == std::string::npos ? pair : pair.substr(0, eq);
            const std::string rawValue = eq == std::string::npos ? "" : pair.substr(eq + 1);
            params.emplace_back(uriEncode(percentDecode(rawName), true), uriEncode(percentDecode(rawValue), true));
        }
        std::ranges::sort(params);

        std::string out;
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (i > 0) out += '&';
            out += params[i].first + "=" + params[i].second;
        }
        return out;
    }

    std::optional<SigV4::ParsedAuthorization> SigV4::ParseAuthorizationHeader(const std::string &headerValue) {

        if (!headerValue.starts_with(Algorithm)) return std::nullopt;

        const std::string rest = trim(headerValue.substr(Algorithm.size()));
        std::string credential, signedHeaders, signature;

        for (const auto &part: split(rest, ',')) {
            const auto trimmed = trim(part);
            if (trimmed.starts_with("Credential=")) credential = trimmed.substr(std::strlen("Credential="));
            else if (trimmed.starts_with("SignedHeaders=")) signedHeaders = trimmed.substr(std::strlen("SignedHeaders="));
            else if (trimmed.starts_with("Signature=")) signature = trimmed.substr(std::strlen("Signature="));
        }
        if (credential.empty() || signedHeaders.empty() || signature.empty()) return std::nullopt;

        const auto scopeParts = split(credential, '/');
        if (scopeParts.size() != 5 || scopeParts[4] != kScopeTerminator) return std::nullopt;

        ParsedAuthorization parsed;
        parsed.scope = {.accessKeyId = scopeParts[0], .dateStamp = scopeParts[1], .region = scopeParts[2], .service = scopeParts[3]};
        parsed.signedHeaders = signedHeaders;
        parsed.signature = signature;
        return parsed;
    }

    std::string SigV4::BuildCanonicalRequest(const std::string &method, const std::string &canonicalUri, const std::string &canonicalQuery,
                                             const std::map<std::string, std::string> &headers, const std::vector<std::string> &signedHeaderNames,
                                             const std::string &payloadHashHex) {
        std::string canonicalHeaders;
        std::string signedHeadersStr;
        for (std::size_t i = 0; i < signedHeaderNames.size(); ++i) {
            const auto &name = signedHeaderNames[i];
            const auto it = headers.find(name);
            canonicalHeaders += name + ":" + (it != headers.end() ? it->second : "") + "\n";
            if (i > 0) signedHeadersStr += ";";
            signedHeadersStr += name;
        }

        return method + "\n" + canonicalUri + "\n" + canonicalQuery + "\n" + canonicalHeaders + "\n" + signedHeadersStr + "\n" + payloadHashHex;
    }

    std::string SigV4::BuildStringToSign(const std::string &amzDate, const std::string &credentialScope, const std::string &canonicalRequestHashHex) {
        return std::string(Algorithm) + "\n" + amzDate + "\n" + credentialScope + "\n" + canonicalRequestHashHex;
    }

    std::vector<unsigned char> SigV4::DeriveSigningKey(const std::string &secretAccessKey, const std::string &dateStamp, const std::string &region, const std::string &service) {
        const auto kDate = Crypto::HmacSha256(toBytes("AWS4" + secretAccessKey), dateStamp);
        const auto kRegion = Crypto::HmacSha256(kDate, region);
        const auto kService = Crypto::HmacSha256(kRegion, service);
        return Crypto::HmacSha256(kService, kScopeTerminator);
    }

    void SigV4::Sign(Request &req, const std::string &accessKeyId, const std::string &secretAccessKey, const std::string &region, const std::string &service) {

        const auto [amzDate, dateStamp] = nowAmzDate();
        req.set("x-amz-date", amzDate);
        req.set("x-amz-content-sha256", Crypto::Sha256Hex(req.body()));

        const std::string target(req.target());
        const auto q = target.find('?');
        const std::string canonicalUri = q == std::string::npos ? target : target.substr(0, q);
        const std::string canonicalQuery = q == std::string::npos ? "" : CanonicalizeQueryString(target.substr(q + 1));

        const auto canonicalRequest = BuildCanonicalRequest(std::string(req.method_string()), canonicalUri, canonicalQuery,
                                                            headerMap(req), SignedHeaderNames(), std::string(req["x-amz-content-sha256"]));

        const std::string credentialScope = dateStamp + "/" + region + "/" + service + "/" + std::string(kScopeTerminator);
        const auto stringToSign = BuildStringToSign(amzDate, credentialScope, Crypto::Sha256Hex(canonicalRequest));

        const auto signingKey = DeriveSigningKey(secretAccessKey, dateStamp, region, service);
        const auto signature = Crypto::ToHexLower(Crypto::HmacSha256(signingKey, stringToSign));

        req.set(http::field::authorization, std::string(Algorithm) + " Credential=" + accessKeyId + "/" + credentialScope +
                                                    ", SignedHeaders=" + signedHeaderList() + ", Signature=" + signature);
    }

    bool SigV4::IsSigned(const Request &req) {
        return std::string(req[http::field::authorization]).starts_with(Algorithm);
    }

    std::optional<std::string> SigV4::Verify(const Request &req,
                                             const std::function<std::optional<std::string>(const std::string &)> &lookupSecret,
                                             const std::chrono::seconds maxSkew) {

        const auto parsed = ParseAuthorizationHeader(std::string(req[http::field::authorization]));
        if (!parsed.has_value()) return std::nullopt;

        // Fixed policy, not client-negotiated: reject anything that does not cover exactly the
        // headers Sign() always signs - see the class comment for why this cannot be a
        // client-chosen list the way real AWS allows.
        if (parsed->signedHeaders != signedHeaderList()) return std::nullopt;

        const auto secret = lookupSecret(parsed->scope.accessKeyId);
        if (!secret.has_value()) return std::nullopt;

        const auto headers = headerMap(req);
        const auto amzDateIt = headers.find("x-amz-date");
        if (amzDateIt == headers.end()) return std::nullopt;
        const auto &amzDate = amzDateIt->second;

        if (amzDate.substr(0, 8) != parsed->scope.dateStamp) return std::nullopt;

        const auto requestTime = parseAmzDate(amzDate);
        if (!requestTime.has_value()) return std::nullopt;
        if (const auto skew = std::chrono::system_clock::now() - *requestTime; skew > maxSkew || skew < -maxSkew) return std::nullopt;

        const auto payloadHashIt = headers.find("x-amz-content-sha256");
        if (payloadHashIt == headers.end()) return std::nullopt;
        if (payloadHashIt->second != Crypto::Sha256Hex(req.body())) return std::nullopt;

        const std::string target(req.target());
        const auto q = target.find('?');
        const std::string canonicalUri = q == std::string::npos ? target : target.substr(0, q);
        const std::string canonicalQuery = q == std::string::npos ? "" : CanonicalizeQueryString(target.substr(q + 1));

        const auto canonicalRequest = BuildCanonicalRequest(std::string(req.method_string()), canonicalUri, canonicalQuery,
                                                            headers, SignedHeaderNames(), payloadHashIt->second);

        const std::string credentialScope = parsed->scope.dateStamp + "/" + parsed->scope.region + "/" + parsed->scope.service + "/" + std::string(kScopeTerminator);
        const auto stringToSign = BuildStringToSign(amzDate, credentialScope, Crypto::Sha256Hex(canonicalRequest));

        const auto signingKey = DeriveSigningKey(*secret, parsed->scope.dateStamp, parsed->scope.region, parsed->scope.service);
        const auto expectedSignature = Crypto::ToHexLower(Crypto::HmacSha256(signingKey, stringToSign));

        if (!Crypto::ConstantTimeEquals(expectedSignature, parsed->signature)) return std::nullopt;

        return parsed->scope.accessKeyId;
    }

}// namespace Euclid::CDK
