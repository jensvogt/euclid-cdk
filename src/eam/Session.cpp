// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>
#include <utility>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Url.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EAM {

    namespace {

        namespace http = boost::beast::http;

    }// namespace

    Session::Session(SessionOptions options)
        : _options(std::move(options)), _client(_options.baseUrl, _options.connection) {
        if (_options.signingScheme == nullptr) _options.signingScheme = &SigningScheme::Rfc9421();
    }

    // -- identity -------------------------------------------------------------------------------

    const std::string &Session::BaseUrl() const { return _options.baseUrl; }

    const std::string &Session::Token() const { return _options.token; }

    const std::string &Session::UserId() const { return _options.userId; }

    const std::string &Session::AccountId() const { return _options.accountId; }

    const std::string &Session::Region() const { return _options.region; }

    const std::string &Session::AccessKeyId() const { return _options.accessKeyId; }

    const std::string &Session::SecretAccessKey() const { return _options.secretAccessKey; }

    bool Session::IsAdmin() const { return _options.isAdmin; }

    const std::string &Session::Namespace() const { return _options.nameSpace; }

    const boost::json::object &Session::Raw() const { return _options.raw; }

    std::string Session::Authority() const { return Url::AuthorityOf(_options.baseUrl); }

    const SigningScheme &Session::Scheme() const { return *_options.signingScheme; }

    void Session::SetScheme(const SigningScheme &scheme) { _options.signingScheme = &scheme; }

    void Session::SetTokenProvider(std::function<std::string()> provider) { _tokenProvider = std::move(provider); }

    std::string Session::CurrentToken() const {
        return _tokenProvider ? _tokenProvider() : _options.token;
    }

    Credentials::Entry Session::ToCredentialsEntry() const {
        return {
                .token = _options.token,
                .userId = _options.userId,
                .accountId = _options.accountId,
                .region = _options.region,
                .accessKeyId = _options.accessKeyId,
                .secretAccessKey = _options.secretAccessKey,
                .isAdmin = _options.isAdmin,
                .nameSpace = _options.nameSpace,
                .baseUrl = _options.baseUrl,
        };
    }

    const HttpClient &Session::Client() const { return _client; }

    // -- users ----------------------------------------------------------------------------------

    Page<User> Session::ListUsers(const ListOptions &options) const {
        return ToPage<User>(Call("list-users", ListPayload(options, "userId")), "users", ToUser);
    }

    User Session::Register(const std::string &userId, const std::string &password, const RegisterOptions &options) const {
        const boost::json::object payload{
                {"userId", userId},
                {"password", password},
                {"email", options.email},
                {"accountId", options.accountId.empty() ? _options.accountId : options.accountId},
                {"region", options.region.empty() ? _options.region : options.region},
                {"isAdmin", options.isAdmin},
        };
        return ToUser(Json::Child(Call("register", payload), "user"));
    }

    void Session::DeleteUser(const std::string &userId) const {
        std::ignore = Call("delete-user", {{"userId", userId}});
    }

    // -- namespace scoping ----------------------------------------------------------------------

    void Session::ChangeNamespace(const std::string &nameSpace) {
        std::ignore = Call("change-namespace", {{"namespace", nameSpace}});
        _options.nameSpace = nameSpace;
        if (_options.cache) Credentials::UpdateNamespace(_options.baseUrl, nameSpace);
    }

    // -- access keys ----------------------------------------------------------------------------

    CreateAccessKeyResult Session::CreateAccessKey() const {
        return ToCreateAccessKeyResult(Call("create-access-key"));
    }

    std::vector<AccessKey> Session::ListAccessKeys() const {
        std::vector<AccessKey> keys;
        for (const auto &document: Json::Documents(Call("list-access-keys"), "accessKeys")) {
            keys.push_back(ToAccessKey(document));
        }
        return keys;
    }

    void Session::DeleteAccessKey(const std::string &accessKeyId) const {
        std::ignore = Call("delete-access-key", {{"accessKeyId", accessKeyId}});
    }

    // -- user groups ----------------------------------------------------------------------------

    UserGroup Session::CreateUserGroup(const std::string &name, const std::string &description) const {
        return ToUserGroup(Json::Child(Call("create-user-group", {{"name", name}, {"description", description}}), "userGroup"));
    }

    Page<UserGroup> Session::ListUserGroups(const ListOptions &options) const {
        return ToPage<UserGroup>(Call("list-user-groups", ListPayload(options, "name")), "userGroups", ToUserGroup);
    }

    void Session::AddUserToUserGroup(const std::string &userGroup, const std::string &user) const {
        std::ignore = Call("user-group-add-user", {{"userGroup", userGroup}, {"user", user}});
    }

    void Session::RemoveUserFromUserGroup(const std::string &userGroup, const std::string &user) const {
        std::ignore = Call("user-group-remove-user", {{"userGroup", userGroup}, {"user", user}});
    }

    void Session::DeleteUserGroup(const std::string &name) const {
        std::ignore = Call("delete-user-group", {{"name", name}});
    }

    // -- accounts -------------------------------------------------------------------------------

    Account Session::CreateAccount(const std::string &accountId, const std::string &name, const std::string &description) const {
        const boost::json::object payload{{"accountId", accountId}, {"name", name}, {"description", description}};
        return ToAccount(Json::Child(Call("create-account", payload), "account"));
    }

    Page<Account> Session::ListAccounts(const ListOptions &options) const {
        return ToPage<Account>(Call("list-accounts", ListPayload(options, "accountId")), "accounts", ToAccount);
    }

    void Session::DeleteAccount(const std::string &accountId) const {
        std::ignore = Call("delete-account", {{"accountId", accountId}});
    }

    // -- namespaces -----------------------------------------------------------------------------

    // "EAM::Namespace" rather than "Namespace" inside these two: within Session the latter is the
    // accessor of the same name, which hides the type.
    EAM::Namespace Session::CreateNamespace(const std::string &accountId, const std::string &name, const std::string &description) const {
        const boost::json::object payload{{"accountId", accountId}, {"name", name}, {"description", description}};
        return ToNamespace(Json::Child(Call("create-namespace", payload), "namespace"));
    }

    Page<EAM::Namespace> Session::ListNamespaces(const std::string &accountId, const ListOptions &options) const {
        auto payload = ListPayload(options, "name");
        payload["accountId"] = accountId;
        return ToPage<EAM::Namespace>(Call("list-namespaces", payload), "namespaces", ToNamespace);
    }

    void Session::DeleteNamespace(const std::string &accountId, const std::string &name) const {
        std::ignore = Call("delete-namespace", {{"accountId", accountId}, {"name", name}});
    }

    void Session::GrantNamespaceAccess(const std::string &user, const std::string &accountId, const std::string &nameSpace) const {
        std::ignore = Call("grant-namespace-access", {{"user", user}, {"accountId", accountId}, {"namespace", nameSpace}});
    }

    void Session::RevokeNamespaceAccess(const std::string &user, const std::string &accountId, const std::string &nameSpace) const {
        std::ignore = Call("revoke-namespace-access", {{"user", user}, {"accountId", accountId}, {"namespace", nameSpace}});
    }

    // -- monitoring -----------------------------------------------------------------------------

    boost::json::object Session::Metrics() const {
        return Call("get-metrics");
    }

    // -- transport ------------------------------------------------------------------------------

    boost::json::object Session::Call(const std::string &action, const boost::json::object &payload) const {

        const auto request = NewRequest(std::string(Target), action, boost::json::serialize(payload));
        const auto response = _client.Send(std::string(Target), action, request);

        if (!response.IsSuccess()) throw ServiceError(std::string(Target), action, response.statusCode, response.body);

        const auto parsed = response.Json();
        // A response that is not an object still says something - an action answering with a bare
        // array or number should reach a caller rather than be flattened into nothing.
        if (parsed.is_object()) return parsed.as_object();
        return parsed.is_null() ? boost::json::object{} : boost::json::object{{"result", parsed}};
    }

    Request Session::NewRequest(const std::string &target, const std::string &action, const std::string &body) const {
        Request request = _client.NewRequest(target, action, body);
        ApplyRoutingHeaders(request);
        Authenticate(request, target);
        return request;
    }

    // -- internals ------------------------------------------------------------------------------

    void Session::ApplyRoutingHeaders(Request &request) const {
        // Set unconditionally, empty value included: both schemes sign a fixed list of header
        // names and canonicalize an absent header as empty, so sending the header with an empty
        // value and omitting it produce the same signature - but only sending it always keeps the
        // request the server receives identical to the one that was signed here.
        request.set("x-euclid-region", _options.region);
        request.set("x-euclid-account-id", _options.accountId);
        request.set("x-euclid-user-id", _options.userId);
        // Not covered by either signature scheme, deliberately and in every SDK: a namespace scopes
        // what a request may touch, and the server checks it against the caller's grants rather
        // than against the signature.
        if (!_options.nameSpace.empty()) request.set("x-euclid-namespace", _options.nameSpace);
    }

    void Session::Authenticate(Request &request, const std::string &target) const {
        if (ShouldSign()) {
            _options.signingScheme->Sign(request, _options.accessKeyId, _options.secretAccessKey, _options.region, target);
        } else {
            ApplyBearerToken(request);
        }
    }

    void Session::ApplyBearerToken(Request &request) const {
        request.set(http::field::authorization, "Bearer " + CurrentToken());
    }

    bool Session::AlwaysSigns() const { return _options.auth == AuthMode::Signature; }

    bool Session::ShouldSign() const {

        const bool hasKey = !_options.accessKeyId.empty() && !_options.secretAccessKey.empty();

        switch (_options.auth) {
            case AuthMode::Bearer:
                return false;
            case AuthMode::Signature:
                if (!hasKey) {
                    throw EuclidError("AuthMode::Signature was requested but this session has no access key - "
                                      "the login returned none, so there is nothing to sign with");
                }
                return true;
            case AuthMode::Auto:
            default:
                return hasKey;
        }
    }

}// namespace Euclid::CDK::EAM
