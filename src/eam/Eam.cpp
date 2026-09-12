// SPDX-License-Identifier: Apache-2.0

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Credentials.h>
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Url.h>
#include <euclid/cdk/eam/Eam.h>

namespace Euclid::CDK::EAM {

    Eam::Eam(const std::string &baseUrl) : _baseUrl(Url::StripTrailingSlash(baseUrl)), _scheme(&SigningScheme::Rfc9421()) {
        // Applied only when the file is actually there, so this default is a no-op on a machine
        // with no euclid deployment and the right thing on one that has.
        _connection.caCertPath = HttpClient::DefaultCaCertPath();
    }

    Eam Eam::ForServer(const std::string &baseUrl) {
        if (baseUrl.empty()) throw EuclidError("baseUrl must not be empty");
        return Eam(baseUrl);
    }

    // -- builder --------------------------------------------------------------------------------

    Eam &Eam::Username(const std::string &username) {
        _username = username;
        return *this;
    }

    Eam &Eam::Email(const std::string &email) {
        _email = email;
        return *this;
    }

    Eam &Eam::Password(const std::string &password) {
        _password = password;
        _passwordSet = true;
        return *this;
    }

    Eam &Eam::Credentials(const std::string &username, const std::string &password) {
        return Username(username).Password(password);
    }

    Eam &Eam::Namespace(const std::string &nameSpace) {
        _nameSpace = nameSpace;
        // Recorded separately from the value, because "" is a namespace instruction of its own -
        // it clears the scope - and has to be told apart from never having asked.
        _nameSpaceSet = true;
        return *this;
    }

    Eam &Eam::CaCertPath(const std::string &path) {
        _connection.caCertPath = path;
        return *this;
    }

    Eam &Eam::Verify(const bool verify) {
        _connection.verify = verify;
        return *this;
    }

    Eam &Eam::Timeout(const std::chrono::milliseconds timeout) {
        _connection.timeout = timeout;
        return *this;
    }

    Eam &Eam::Scheme(const SigningScheme &scheme) {
        _scheme = &scheme;
        return *this;
    }

    Eam &Eam::Auth(const AuthMode mode) {
        _auth = mode;
        return *this;
    }

    Eam &Eam::UseCache(const bool useCache) {
        _useCache = useCache;
        return *this;
    }

    Eam &Eam::LoginPath(const std::string &path) {
        _loginPath = path.starts_with('/') ? path : "/" + path;
        return *this;
    }

    // -- login ----------------------------------------------------------------------------------

    SessionOptions Eam::BaseOptions() const {
        SessionOptions options;
        options.baseUrl = _baseUrl;
        options.connection = _connection;
        options.signingScheme = _scheme;
        options.auth = _auth;
        options.cache = _useCache;
        return options;
    }

    std::optional<Session> Eam::CachedSession() const {

        if (!_useCache) return std::nullopt;

        const auto cached = CDK::Credentials::Load();
        if (!cached.has_value() || cached->token.empty() || cached->baseUrl != _baseUrl) return std::nullopt;
        if (!CDK::Credentials::IsTokenValid(cached->token)) return std::nullopt;

        SessionOptions options = BaseOptions();
        options.token = cached->token;
        options.userId = cached->userId;
        options.accountId = cached->accountId;
        options.region = cached->region;
        options.accessKeyId = cached->accessKeyId;
        options.secretAccessKey = cached->secretAccessKey;
        options.isAdmin = cached->isAdmin;
        options.nameSpace = cached->nameSpace;
        return Session(std::move(options));
    }

    Session Eam::Login() {

        if (auto cached = CachedSession(); cached.has_value()) {
            if (_nameSpaceSet && _nameSpace != cached->Namespace()) cached->ChangeNamespace(_nameSpace);
            return std::move(*cached);
        }

        if (_username.empty() && _email.empty()) throw EuclidError("username or email must be set before calling Login()");
        if (!_passwordSet) throw EuclidError("password must be set before calling Login()");

        // Only one identifier goes out: the server resolves the user by ID when one is present and
        // only falls back to the email, so sending both would silently ignore the email.
        const boost::json::object body{
                {"userId", _username},
                {"password", _password},
                {"email", _username.empty() ? _email : std::string{}},
        };

        const HttpClient client(_baseUrl, _connection);
        // Login is the one request that is never signed: there is no access key yet to sign with,
        // which is what this call is here to fetch.
        Request request = client.NewRequest(std::string(Target), "login", boost::json::serialize(body));
        request.target(_loginPath);
        const auto response = client.Send(std::string(Target), "login", request);

        if (!response.IsSuccess()) throw AuthenticationError(response.statusCode, response.body);

        const auto result = ToLoginResult(response.Json());

        SessionOptions options = BaseOptions();
        options.token = result.token;
        options.userId = result.metadata.user;
        options.accountId = result.metadata.accountId;
        options.region = result.metadata.region;
        options.accessKeyId = result.accessKeyId;
        options.secretAccessKey = result.secretAccessKey;
        options.isAdmin = result.isAdmin;
        options.raw = result.raw;

        Session session(std::move(options));
        if (_nameSpaceSet && !_nameSpace.empty()) session.ChangeNamespace(_nameSpace);
        if (_useCache) CDK::Credentials::Save(session.ToCredentialsEntry());
        return session;
    }

    Session Eam::Login(const std::string &username, const std::string &password) {
        return Credentials(username, password).Login();
    }

}// namespace Euclid::CDK::EAM
