// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <string_view>
#include <vector>

// Euclid includes
#include <euclid/cdk/Export.h>

namespace Euclid::CDK {

    /**
     * @brief The hashing and encoding both signing schemes are built out of.
     *
     * @par
     * A trimmed port of the euclid server's Core::CryptoUtils - only what signing a request needs,
     * so that this SDK depends on OpenSSL and nothing else of the server. The names and the
     * behaviour are the server's, which is what makes a signature made here verify there.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API Crypto {
    public:

        /**
         * @brief SHA-256 of a string, hex-encoded.
         *
         * @param data input bytes; may be empty, which still has a digest.
         * @return the digest as a lowercase hex string.
         */
        [[nodiscard]]
        static std::string Sha256Hex(std::string_view data);

        /**
         * @brief SHA-256 of a string, as raw bytes.
         *
         * @param data input bytes; may be empty.
         * @return the 32 raw digest bytes.
         */
        [[nodiscard]]
        static std::string Sha256Raw(std::string_view data);

        /**
         * @brief HMAC-SHA256.
         *
         * @param key  raw key bytes.
         * @param data message to authenticate.
         * @return the 32-byte MAC.
         */
        [[nodiscard]]
        static std::vector<unsigned char> HmacSha256(const std::vector<unsigned char> &key, std::string_view data);

        /**
         * @brief Base64-encodes bytes, standard alphabet, padded, unwrapped.
         *
         * @param data raw bytes.
         * @return the encoded string.
         */
        [[nodiscard]]
        static std::string Base64Encode(std::string_view data);

        /**
         * @brief Decodes standard base64. Also accepts the URL-safe alphabet and missing padding,
         * which is what a JWT's segments are encoded in.
         *
         * @param data the encoded string.
         * @return the raw bytes, or an empty string if the input is not valid base64.
         */
        [[nodiscard]]
        static std::string Base64Decode(std::string_view data);

        /**
         * @brief Hex-encodes bytes, lowercase.
         *
         * @param bytes raw bytes.
         * @return the encoded string.
         */
        [[nodiscard]]
        static std::string ToHexLower(const std::vector<unsigned char> &bytes);

        /**
         * @brief Compares two strings in time that does not depend on where they first differ.
         *
         * @par
         * Used for every signature comparison: the result of a signature check must not leak
         * through how long it took to fail.
         *
         * @param a first string.
         * @param b second string.
         * @return true if they are equal.
         */
        [[nodiscard]]
        static bool ConstantTimeEquals(std::string_view a, std::string_view b);
    };

}// namespace Euclid::CDK
