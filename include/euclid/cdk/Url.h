// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>

// Euclid includes
#include <euclid/cdk/Export.h>

namespace Euclid::CDK {

    /**
     * @brief An endpoint URL, split into the three parts a connection needs.
     */
    struct EndpointParts {

        /**
         * @brief URI scheme, lowercase, e.g. "https".
         */
        std::string scheme;

        /**
         * @brief Host as written, e.g. "euclid.example.com" - without a port.
         */
        std::string host;

        /**
         * @brief Port to connect to, the scheme's default when the URL did not name one.
         */
        std::string port;
    };

    /**
     * @brief Small URL helpers, kept in one place because a signature depends on getting them right.
     *
     * @par
     * "host" is a signed header under both schemes, and "\@authority" is a signed component under
     * RFC 9421. If what a client signs is not byte-for-byte what it sends, the signature fails on
     * arrival and the failure says nothing about why - so the value that goes into the signature
     * and the value that goes on the wire are computed here, once, by the same function.
     *
     * @par
     * Parsed by hand rather than with boost::urls, which normalizes a default port away: euclid's
     * server compares the Host header it received against the one the signature covers, so
     * "https://host:443" has to keep the port it was written with.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API Url {
    public:

        /**
         * @brief Drops a trailing slash: "https://host/" and "https://host" name the same server,
         * and this picks the second spelling.
         *
         * @param url the URL.
         * @return the URL without its trailing slash.
         */
        [[nodiscard]]
        static std::string StripTrailingSlash(const std::string &url);

        /**
         * @brief The URL's scheme, lowercase.
         *
         * @param url the URL.
         * @return the scheme, or "https" when the URL names none.
         */
        [[nodiscard]]
        static std::string SchemeOf(const std::string &url);

        /**
         * @brief The Host header for this URL: the authority as written, minus any userinfo.
         *
         * @par
         * Deliberately preserves a port that was written out even when it is the scheme's default,
         * and omits one that was not. The header is set explicitly rather than left to the HTTP
         * client, because a client that drops a default port would send a Host the signature was
         * not made over.
         *
         * @param url the URL.
         * @return the Host header value, lowercase.
         */
        [[nodiscard]]
        static std::string HostHeaderOf(const std::string &url);

        /**
         * @brief The RFC 9421 "\@authority": the Host header, minus the port when it is the
         * scheme's default.
         *
         * @par
         * RFC 9421 §2.2.3 requires the default port to be dropped, which is why this is not simply
         * the Host header.
         *
         * @param url the URL.
         * @return the "\@authority" component value.
         */
        [[nodiscard]]
        static std::string AuthorityOf(const std::string &url);

        /**
         * @brief Splits a URL into the scheme, host and port a connection is opened with.
         *
         * @param url the URL, e.g. "https://euclid.example.com:5566".
         * @return the parts, with the scheme's default port filled in when the URL named none.
         */
        [[nodiscard]]
        static EndpointParts Split(const std::string &url);
    };

}// namespace Euclid::CDK
