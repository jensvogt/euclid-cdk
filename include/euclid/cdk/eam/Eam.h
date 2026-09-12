// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EAM {

    /**
     * @brief Builder for authenticating against a euclid server.
     *
     * @par
     * @code
     * const Session session = Eam::ForServer("https://euclid.example.com")
     *                                 .Credentials("jens", "secret")
     *                                 .Login();
     * @endcode
     *
     * @par
     * Split from Session because logging in and being logged in need different arguments. A builder
     * that also carried the operations would let a caller write ListUsers() on an object that has
     * not authenticated yet, and a session that also carried the login options would have a
     * namespace field that means one thing before login and another after.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API Eam {
    public:

        /**
         * @brief Targets a euclid server.
         *
         * @param baseUrl e.g. "https://euclid.example.com". A trailing slash is dropped.
         * @return the builder.
         * @throws EuclidError if the URL is empty.
         */
        [[nodiscard]]
        static Eam ForServer(const std::string &baseUrl);

        // -- builder ----------------------------------------------------------------------------

        /**
         * @brief The user ID to log in as.
         */
        Eam &Username(const std::string &username);

        /**
         * @brief The email address to log in with, when there is no user ID.
         *
         * @par
         * The server resolves the user by ID first and only falls back to the email, so setting
         * both silently ignores the email; Login() sends whichever one identifies the account.
         */
        Eam &Email(const std::string &email);

        /**
         * @brief The password.
         */
        Eam &Password(const std::string &password);

        /**
         * @brief User ID and password together.
         */
        Eam &Credentials(const std::string &username, const std::string &password);

        /**
         * @brief The namespace to make active once login succeeds.
         *
         * @par
         * Applied with a follow-up "change-namespace" call, mirroring euclid-cli's
         * "eam login --namespace", and applied whether the login was fresh or served from the
         * cache - a cached session may have been established before this was ever asked for, or
         * scoped to another namespace.
         */
        Eam &Namespace(const std::string &nameSpace);

        /**
         * @brief A PEM CA certificate to trust alongside the system store.
         *
         * @par
         * Defaults to euclid's own, when that is installed on this machine - see
         * HttpClient::DefaultCaCertPath(). Pass an empty string for the system store alone.
         */
        Eam &CaCertPath(const std::string &path);

        /**
         * @brief Whether to verify the server's certificate. Turn it off for development servers
         * only.
         */
        Eam &Verify(bool verify);

        /**
         * @brief How long to wait for a response.
         */
        Eam &Timeout(std::chrono::milliseconds timeout);

        /**
         * @brief Which signing scheme the session signs with. RFC 9421 unless told otherwise, as
         * that is what euclid-cli and euclid's own applications sign with today.
         */
        Eam &Scheme(const SigningScheme &scheme);

        /**
         * @brief Whether the session signs, presents a token, or decides per request.
         */
        Eam &Auth(AuthMode mode);

        /**
         * @brief Whether ~/.euclid/credentials may be read and written. On by default, which is
         * what makes a login shared with euclid-cli and the other SDKs.
         */
        Eam &UseCache(bool useCache);

        /**
         * @brief The path to post the login to. "/" unless a deployment puts the gateway elsewhere.
         */
        Eam &LoginPath(const std::string &path);

        // -- login ------------------------------------------------------------------------------

        /**
         * @brief Authenticates, and answers with the session every other call goes through.
         *
         * @par
         * A still-valid cached session for this server is reused rather than re-authenticating, so
         * calling this repeatedly costs nothing after the first time. UseCache(false) forces a
         * fresh login.
         *
         * @return the session.
         * @throws AuthenticationError if the server refuses the credentials.
         * @throws EuclidError if no password, or neither a username nor an email, was set and there
         * is no cached session to fall back on - or if the server could not be reached.
         */
        [[nodiscard]]
        Session Login();

        /**
         * @brief Sets the credentials and logs in, for the common case that needs no builder.
         *
         * @param username the user ID.
         * @param password the password.
         * @return the session.
         */
        [[nodiscard]]
        Session Login(const std::string &username, const std::string &password);

    private:

        explicit Eam(const std::string &baseUrl);

        /**
         * @brief A session rebuilt from ~/.euclid/credentials, if one is cached for this server and
         * its token has not expired.
         */
        [[nodiscard]]
        std::optional<Session> CachedSession() const;

        /**
         * @brief The session options every login fills in the same way.
         */
        [[nodiscard]]
        SessionOptions BaseOptions() const;

        std::string _baseUrl;
        std::string _loginPath{"/"};
        std::string _username;
        std::string _email;
        std::string _password;
        std::string _nameSpace;
        bool _nameSpaceSet{false};
        bool _passwordSet{false};
        ConnectionOptions _connection;
        const SigningScheme *_scheme;
        AuthMode _auth{AuthMode::Auto};
        bool _useCache{true};
    };

}// namespace Euclid::CDK::EAM
