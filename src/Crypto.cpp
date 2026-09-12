// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <array>
#include <iomanip>
#include <sstream>

// OpenSSL includes
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

// Euclid includes
#include <euclid/cdk/Crypto.h>

namespace Euclid::CDK {

    namespace {

        constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        // The reverse table, built once. -1 marks a character that is not part of the alphabet;
        // '-' and '_' map to the same values as '+' and '/' so that a JWT's URL-safe segments
        // decode here too, which is what Credentials::IsTokenValid() needs.
        const std::array<signed char, 256> &base64Reverse() {
            static const auto table = [] {
                std::array<signed char, 256> reverse{};
                reverse.fill(-1);
                for (std::size_t i = 0; i < kBase64Alphabet.size(); ++i) {
                    reverse[static_cast<unsigned char>(kBase64Alphabet[i])] = static_cast<signed char>(i);
                }
                reverse[static_cast<unsigned char>('-')] = 62;
                reverse[static_cast<unsigned char>('_')] = 63;
                return reverse;
            }();
            return table;
        }

    }// namespace

    std::string Crypto::Sha256Raw(const std::string_view data) {
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char *>(data.data()), data.size(), digest);
        return {reinterpret_cast<const char *>(digest), SHA256_DIGEST_LENGTH};
    }

    std::string Crypto::Sha256Hex(const std::string_view data) {
        const auto raw = Sha256Raw(data);
        return ToHexLower({raw.begin(), raw.end()});
    }

    std::vector<unsigned char> Crypto::HmacSha256(const std::vector<unsigned char> &key, const std::string_view data) {
        unsigned char mac[EVP_MAX_MD_SIZE];
        unsigned int length = 0;
        // An empty key is still a key as far as HMAC is concerned, but OpenSSL wants a non-null
        // pointer for it - hence the literal, which is never read because the length is zero.
        const unsigned char *keyData = key.empty() ? reinterpret_cast<const unsigned char *>("") : key.data();
        HMAC(EVP_sha256(), keyData, static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char *>(data.data()), data.size(), mac, &length);
        return {mac, mac + length};
    }

    std::string Crypto::Base64Encode(const std::string_view data) {

        std::string out;
        out.reserve((data.size() + 2) / 3 * 4);

        std::size_t i = 0;
        for (; i + 2 < data.size(); i += 3) {
            const auto triple = static_cast<unsigned>(static_cast<unsigned char>(data[i]) << 16 |
                                                      static_cast<unsigned char>(data[i + 1]) << 8 |
                                                      static_cast<unsigned char>(data[i + 2]));
            out += kBase64Alphabet[triple >> 18 & 0x3F];
            out += kBase64Alphabet[triple >> 12 & 0x3F];
            out += kBase64Alphabet[triple >> 6 & 0x3F];
            out += kBase64Alphabet[triple & 0x3F];
        }

        if (const auto remaining = data.size() - i; remaining == 1) {
            const auto value = static_cast<unsigned>(static_cast<unsigned char>(data[i]) << 16);
            out += kBase64Alphabet[value >> 18 & 0x3F];
            out += kBase64Alphabet[value >> 12 & 0x3F];
            out += "==";
        } else if (remaining == 2) {
            const auto value = static_cast<unsigned>(static_cast<unsigned char>(data[i]) << 16 |
                                                     static_cast<unsigned char>(data[i + 1]) << 8);
            out += kBase64Alphabet[value >> 18 & 0x3F];
            out += kBase64Alphabet[value >> 12 & 0x3F];
            out += kBase64Alphabet[value >> 6 & 0x3F];
            out += '=';
        }

        return out;
    }

    std::string Crypto::Base64Decode(const std::string_view data) {

        const auto &reverse = base64Reverse();

        std::string out;
        out.reserve(data.size() / 4 * 3);

        unsigned buffer = 0;
        int bits = 0;
        for (const char c: data) {
            if (c == '=') break;
            const auto value = reverse[static_cast<unsigned char>(c)];
            // Whitespace is ignored rather than rejected - a PEM-style wrapped value is still the
            // bytes it encodes - but anything else means this is not base64 at all.
            if (value < 0) {
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
                return {};
            }
            buffer = buffer << 6 | static_cast<unsigned>(value);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += static_cast<char>(buffer >> bits & 0xFF);
            }
        }

        return out;
    }

    std::string Crypto::ToHexLower(const std::vector<unsigned char> &bytes) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (const unsigned char b: bytes) oss << std::setw(2) << static_cast<int>(b);
        return oss.str();
    }

    bool Crypto::ConstantTimeEquals(const std::string_view a, const std::string_view b) {
        // The length is compared first and in the clear: it is not a secret, and CRYPTO_memcmp
        // needs the two runs to be the same size.
        if (a.size() != b.size()) return false;
        return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
    }

}// namespace Euclid::CDK
