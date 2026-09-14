// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <optional>
#include <string>

// Euclid includes
#include <euclid/cdk/Export.h>

namespace Euclid::CDK {

    /**
     * @brief The ~/.euclid/credentials file, shared with euclid-cli, euclid-jdk, euclid-pdk and
     * euclid-ndk.
     *
     * @par
     * Every client reads and writes the same file, so a login from any of them is picked up by the
     * others. That makes the field names a wire format rather than an implementation detail: the
     * namespace key is "namespace" (not "nameSpace"), "isAdmin" travels alongside the token, and an
     * absent namespace is written as an empty string rather than null so the CLI's string reader
     * can take it.
     *
     * @par baseUrl
     * "baseUrl" is the one field euclid-cli has no equivalent for - it is what lets a cached session
     * be recognized as belonging to the server being talked to now, and euclid-pdk and euclid-ndk
     * write it for the same reason. A euclid-cli login rewrites the file without it, so a session
     * cached by the CLI is not reused here and a fresh login happens instead. That is the safe
     * direction to be wrong in: the alternative would be presenting one server's token to another.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Credentials {
    public:

        /**
         * @brief Session data persisted to, and reloaded from, the credentials file.
         */
        struct Entry {

            /**
             * @brief Bearer token the login answered with.
             */
            std::string token;

            /**
             * @brief User this session acts as.
             */
            std::string userId;

            /**
             * @brief Account this session acts in.
             */
            std::string accountId;

            /**
             * @brief Region this session acts in.
             */
            std::string region;

            /**
             * @brief Access key ID a signature is made with, or empty when the login returned none.
             */
            std::string accessKeyId;

            /**
             * @brief Secret access key paired with accessKeyId.
             */
            std::string secretAccessKey;

            /**
             * @brief Whether this user has administrator privileges, as reported by the server on
             * login. Lets admin-only calls fail fast client-side instead of only after a round
             * trip - the server enforces it independently regardless, since a locally-cached flag
             * is not a security boundary on its own.
             */
            bool isAdmin{false};

            /**
             * @brief Active namespace, or empty for unscoped. Sent as the x-euclid-namespace
             * header on every request.
             */
            std::string nameSpace;

            /**
             * @brief Server this session belongs to, e.g. "https://euclid.example.com".
             */
            std::string baseUrl;
        };

        /**
         * @brief Where the credentials live.
         *
         * @par
         * Resolved per call rather than captured once, mirroring euclid-cli's
         * Credentials::FilePath(): the home directory is read when the file is actually touched, so
         * a process that changes it is not left talking to a stale path. EUCLID_CREDENTIALS_FILE
         * overrides it, which is also how a euclid-managed application is handed its own
         * credentials.
         *
         * @return the path, e.g. "/home/alice/.euclid/credentials".
         */
        [[nodiscard]]
        static std::string FilePath();

        /**
         * @brief Reads the cached session.
         *
         * @return the stored session, or std::nullopt when there is no readable, well-formed file
         * with a token in it. All of those mean the same thing to a caller: there is nothing
         * cached to reuse, so log in.
         */
        [[nodiscard]]
        static std::optional<Entry> Load();

        /**
         * @brief Writes the session, readable by its owner alone.
         *
         * @param entry session data to persist.
         * @throws EuclidError if the file cannot be written.
         */
        static void Save(const Entry &entry);

        /**
         * @brief Patches the cached namespace in place, if what is cached belongs to baseUrl.
         *
         * @par
         * Keeps the file in step when a session's namespace changes outside of a login. A no-op
         * when nothing is cached for that server, in keeping with the best-effort nature of the
         * cache: failing to record a namespace is not a reason to fail the call that changed it.
         *
         * @param baseUrl   server the change belongs to.
         * @param nameSpace the new namespace, or empty to clear the scope.
         */
        static void UpdateNamespace(const std::string &baseUrl, const std::string &nameSpace);

        /**
         * @brief Removes the credentials file, e.g. after the server rejects the stored token as
         * missing, invalid or expired. A no-op if the file does not exist.
         */
        static void Clear();

        /**
         * @brief Whether a JWT is well-formed and its "exp" is still in the future.
         *
         * @par
         * Checked locally to avoid a round trip that would only tell us what the token already
         * says. The signature is not verified - the client does not hold the server's secret, and a
         * token the client forged for itself would be rejected on arrival anyway.
         *
         * @param token the token.
         * @return true if it can still be presented.
         */
        [[nodiscard]]
        static bool IsTokenValid(const std::string &token);
    };

}// namespace Euclid::CDK
