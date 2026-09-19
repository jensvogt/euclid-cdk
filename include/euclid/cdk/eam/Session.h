// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Credentials.h>
#include <euclid/cdk/Export.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/dto/Eam.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/http/HttpClient.h>

namespace Euclid::CDK::EAM {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "eam";

    /**
     * @brief How a session authenticates.
     */
    enum class AuthMode {

        /**
         * @brief Sign when there is an access key to sign with, and present the bearer token
         * otherwise. euclid accepts either for every action (Core::HttpActionServer::Authenticate).
         */
        Auto,

        /**
         * @brief Always sign. Fails loudly if the session has no access key, rather than quietly
         * falling back to a token - which is what a caller who asked for signatures wants to know
         * about.
         */
        Signature,

        /**
         * @brief Always present the bearer token, even when an access key is available.
         */
        Bearer
    };

    // Listings are paged the same way in every module, so how one is asked for is described once in
    // dto/Page.h - named here as well, since EAM::ListOptions is what the calls below take.
    using CDK::ListOptions;

    /**
     * @brief How a role is scoped when it is granted. Everything, everywhere, by default.
     */
    struct EUCLID_CDK_API GrantOptions {

        /**
         * @brief Namespaces of the account it applies in; a single "*" means every one of them.
         */
        std::vector<std::string> namespaces{"*"};

        /**
         * @brief ERN patterns it applies to, each exact or ending in "*"; a single "*" means every
         * resource.
         */
        std::vector<std::string> resources{"*"};

        /**
         * @brief The account to grant in; the caller's own when empty. Naming another needs
         * administrator rights on it.
         */
        std::string accountId;
    };

    /**
     * @brief Which grants to list. Naming none asks for everything granted in the account.
     */
    struct EUCLID_CDK_API ListGrantsOptions {
        std::string principal;
        std::string role;
        std::string accountId;
    };

    /**
     * @brief What CheckPermission() answers: the verdict, and what decided it.
     */
    struct EUCLID_CDK_API PermissionCheck {
        bool allowed{false};
        std::string reason;

        /**
         * @brief The role whose grant allowed it, when one did. Empty on a refusal.
         */
        std::string role;
    };

    /**
     * @brief The optional half of Register().
     */
    struct EUCLID_CDK_API RegisterOptions {

        /**
         * @brief Email address, or empty for a user who logs in by ID alone.
         */
        std::string email;

        /**
         * @brief Account to create the user in; the session's own when empty.
         */
        std::string accountId;

        /**
         * @brief Region to create the user in; the session's own when empty.
         */
        std::string region;

        /**
         * @brief Whether the new user is an administrator.
         */
        bool isAdmin{false};
    };

    /**
     * @brief Everything a Session needs to exist, which is what a login produces.
     */
    struct EUCLID_CDK_API SessionOptions {
        std::string baseUrl;
        std::string token;
        std::string userId;
        std::string accountId;
        std::string region;
        std::string accessKeyId;
        std::string secretAccessKey;
        bool isAdmin{false};
        std::string nameSpace;

        /**
         * @brief The login response as it arrived, for fields this SDK does not name.
         */
        boost::json::object raw;

        /**
         * @brief TLS and timeout settings the session keeps talking on.
         */
        ConnectionOptions connection;

        /**
         * @brief Which scheme signed requests use. Never null.
         */
        const SigningScheme *signingScheme{nullptr};

        /**
         * @brief Whether to sign, present a token, or decide per request.
         */
        AuthMode auth{AuthMode::Auto};

        /**
         * @brief Whether ~/.euclid/credentials may be written as the session changes.
         */
        bool cache{true};
    };

    /**
     * @brief An authenticated session, and every EAM operation that needs one.
     *
     * @par
     * Holds two credentials, because the server accepts two. The bearer token is what a login
     * always produces; the access key and secret are what it produces when the user has one, and
     * they are what a signature is made with. Which of the two a request presents is decided per
     * session by AuthMode.
     *
     * @par
     * Sessions are mutable: ChangeNamespace() changes this session rather than answering with a new
     * one, since the namespace is a property of what the caller is doing next rather than of the
     * login.
     *
     * @par
     * Not thread-safe. A session owns one connection and sends one request at a time; concurrent
     * callers want a session each.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Session {
    public:

        /**
         * @brief Builds a session from what a login answered with. Normally reached through
         * EAM::Eam::Login() rather than directly.
         *
         * @param options the session's credentials and settings.
         */
        explicit Session(SessionOptions options);

        // -- identity ---------------------------------------------------------------------------

        /**
         * @brief The server this session talks to.
         */
        [[nodiscard]]
        const std::string &BaseUrl() const;

        /**
         * @brief The bearer token the login answered with.
         */
        [[nodiscard]]
        const std::string &Token() const;

        /**
         * @brief The user this session acts as.
         */
        [[nodiscard]]
        const std::string &UserId() const;

        /**
         * @brief The account this session acts in.
         */
        [[nodiscard]]
        const std::string &AccountId() const;

        /**
         * @brief The region this session acts in.
         */
        [[nodiscard]]
        const std::string &Region() const;

        /**
         * @brief The access key ID signatures are made with, or empty when the login returned none.
         */
        [[nodiscard]]
        const std::string &AccessKeyId() const;

        /**
         * @brief The secret access key paired with AccessKeyId().
         */
        [[nodiscard]]
        const std::string &SecretAccessKey() const;

        /**
         * @brief Whether the server reported this user as an administrator.
         */
        [[nodiscard]]
        bool IsAdmin() const;

        /**
         * @brief The namespace every namespace-scoped call is currently restricted to, or empty
         * for unscoped.
         */
        [[nodiscard]]
        const std::string &Namespace() const;

        /**
         * @brief The login response as it arrived, for fields this SDK does not name.
         */
        [[nodiscard]]
        const boost::json::object &Raw() const;

        /**
         * @brief The "\@authority" this session's RFC 9421 signatures are made over, for
         * diagnostics: when a signature is refused, this and the Host header are the first two
         * things worth comparing.
         */
        [[nodiscard]]
        std::string Authority() const;

        /**
         * @brief This session in the shape ~/.euclid/credentials holds it.
         */
        [[nodiscard]]
        Credentials::Entry ToCredentialsEntry() const;

        /**
         * @brief The scheme signed requests use.
         */
        [[nodiscard]]
        const SigningScheme &Scheme() const;

        /**
         * @brief Switches signing scheme for every later request of this session.
         *
         * @param scheme SigningScheme::SigV4() or SigningScheme::Rfc9421().
         */
        void SetScheme(const SigningScheme &scheme);

        /**
         * @brief Where the bearer token comes from, when it does not simply come from Token().
         *
         * @par
         * A process that runs for days holds a token that does not last that long. euclid rewrites
         * an application's credentials file before the token in it expires, so an application that
         * cached the first token it saw would start collecting 401s about an hour in. Setting this
         * to something that re-reads that file makes the session follow the rotation.
         *
         * @param provider answers with the token to present now, or nullptr to use Token().
         */
        void SetTokenProvider(std::function<std::string()> provider);

        /**
         * @brief The bearer token to present now - the token provider's, or Token().
         */
        [[nodiscard]]
        std::string CurrentToken() const;

        // -- users ------------------------------------------------------------------------------

        /**
         * @brief One page of users, and how many exist in total.
         *
         * @param options paging, ordering and prefix.
         * @return the page.
         */
        [[nodiscard]]
        Page<User> ListUsers(const ListOptions &options = {}) const;

        /**
         * @brief One user, by the id they are known by.
         *
         * @par
         * What comes back is exactly what ListUsers() describes each of its own with, so this is
         * the single-user form of a listing rather than another view of one.
         *
         * @par
         * The id rather than the ERN, because that is what everything else names a user with: a
         * grant's principal, an application's technical identity, the audit trail's userId column.
         * A user of another account is a 404, the way a listing would not have shown them.
         *
         * @param userId the user's id.
         * @return the user.
         */
        [[nodiscard]]
        User GetUser(const std::string &userId) const;

        /**
         * @brief Creates a user.
         *
         * @param userId   the new user's ID.
         * @param password the new user's password.
         * @param options  email, account, region and admin flag; account and region default to
         * this session's own.
         * @return the created user.
         */
        [[nodiscard]]
        User Register(const std::string &userId, const std::string &password, const RegisterOptions &options = {}) const;

        /**
         * @brief Deletes a user.
         *
         * @param userId the user to delete.
         */
        void DeleteUser(const std::string &userId) const;

        // -- namespace scoping ------------------------------------------------------------------

        /**
         * @brief Switches the namespace every namespace-scoped call is restricted to, until changed
         * again.
         *
         * @par
         * The server validates it against the current account and the caller's grants, so this is a
         * round trip rather than a local assignment. An empty string clears the scope.
         *
         * @param nameSpace the namespace, or empty to clear the scope.
         */
        void ChangeNamespace(const std::string &nameSpace);

        // -- access keys ------------------------------------------------------------------------

        /**
         * @brief Creates an access key for this session's user.
         *
         * @par
         * The secret comes back here and nowhere else - ListAccessKeys() will never show it again -
         * so a caller that does not store it has to create another key.
         *
         * @return the new key, secret included.
         */
        [[nodiscard]]
        CreateAccessKeyResult CreateAccessKey() const;

        /**
         * @brief This user's own access keys, without their secrets.
         */
        [[nodiscard]]
        std::vector<AccessKey> ListAccessKeys() const;

        /**
         * @brief Deletes one of this user's own access keys.
         *
         * @param accessKeyId the key to delete.
         */
        void DeleteAccessKey(const std::string &accessKeyId) const;

        // -- user groups ------------------------------------------------------------------------

        /**
         * @brief Creates an empty user group. Administrator only.
         *
         * @param name        the group's name.
         * @param description a description, or empty.
         * @return the created group.
         */
        [[nodiscard]]
        UserGroup CreateUserGroup(const std::string &name, const std::string &description = {}) const;

        /**
         * @brief One page of user groups, and how many exist in total.
         */
        [[nodiscard]]
        Page<UserGroup> ListUserGroups(const ListOptions &options = {}) const;

        /**
         * @brief One user group, by name or by ERN, with its members.
         *
         * @par
         * What comes back is exactly what ListUserGroups() describes each of its own with, member
         * ids included, so this is the single-group form of a listing rather than another view of
         * one.
         *
         * @par
         * A value starting with "ern:" is taken as an ERN; anything else is a name. Groups are
         * installation-wide rather than scoped to an account, so a name identifies one without
         * further qualification, and the ERN is accepted only because that is what a grant's
         * principal carries.
         *
         * @param nameOrErn the group's name, or its ERN.
         * @return the group.
         */
        [[nodiscard]]
        UserGroup GetUserGroup(const std::string &nameOrErn) const;

        /**
         * @brief Adds a user to a group.
         *
         * @param userGroup the group's ERN.
         * @param user      the user's ERN.
         */
        void AddUserToUserGroup(const std::string &userGroup, const std::string &user) const;

        /**
         * @brief Removes a user from a group.
         *
         * @param userGroup the group's ERN.
         * @param user      the user's ERN.
         */
        void RemoveUserFromUserGroup(const std::string &userGroup, const std::string &user) const;

        /**
         * @brief Deletes a user group. Administrator only.
         *
         * @param name the group's name.
         */
        void DeleteUserGroup(const std::string &name) const;

        // -- accounts ---------------------------------------------------------------------------

        /**
         * @brief Creates an account. Administrator only - account creation is platform-level and is
         * not delegated to account owners.
         *
         * @param accountId   the new account's ID.
         * @param name        the new account's name.
         * @param description a description, or empty.
         * @return the created account.
         */
        [[nodiscard]]
        Account CreateAccount(const std::string &accountId, const std::string &name, const std::string &description = {}) const;

        /**
         * @brief One page of accounts, and how many exist in total.
         */
        [[nodiscard]]
        Page<Account> ListAccounts(const ListOptions &options = {}) const;

        /**
         * @brief Deletes an account. Administrator only, and it must have no namespaces or grants
         * left.
         *
         * @param accountId the account to delete.
         */
        void DeleteAccount(const std::string &accountId) const;

        // -- namespaces -------------------------------------------------------------------------

        /**
         * @brief Creates a namespace under an account. Requires admin rights on that account.
         *
         * @param accountId   the owning account.
         * @param name        the namespace's name, unique within the account.
         * @param description a description, or empty.
         * @return the created namespace.
         */
        // Qualified: inside this class "Namespace" is the accessor above, which hides the type of
        // the same name. The two are both worth their names, so the type is spelled out here.
        [[nodiscard]]
        EAM::Namespace CreateNamespace(const std::string &accountId, const std::string &name, const std::string &description = {}) const;

        /**
         * @brief One page of an account's namespaces, and how many exist in total.
         *
         * @param accountId the owning account.
         * @param options   paging, ordering and prefix.
         */
        [[nodiscard]]
        Page<EAM::Namespace> ListNamespaces(const std::string &accountId, const ListOptions &options = {}) const;

        /**
         * @brief Deletes a namespace. Requires admin rights on the account, and no grants may
         * remain.
         *
         * @param accountId the owning account.
         * @param name      the namespace's name.
         */
        void DeleteNamespace(const std::string &accountId, const std::string &name) const;

        // -- roles and grants -------------------------------------------------------------------

        /**
         * @brief Gives a role to a user or a user group, scoped.
         *
         * @par
         * Replaced GrantNamespaceAccess(): access to a namespace is now a role granted in it, so
         * the same call says *what* the principal may do there as well as *where*.
         *
         * @param role      a role of the account, or a built-in one - "account-administrator",
         * "operator", "reader", "publisher", "consumer", "application".
         * @param principal a user ERN or a user-group ERN. One argument for both, because the ERN
         * says which.
         * @param options   how it is scoped; everything, everywhere, in this account by default.
         * @return the grant, whose grantId is what RevokeRole() takes.
         */
        [[nodiscard]]
        Grant GrantRole(const std::string &role, const std::string &principal, const GrantOptions &options = {}) const;

        /**
         * @brief Removes one grant, by its own id.
         *
         * @par
         * Not by role and principal: the same role may be granted to the same principal twice with
         * different scope, and revoking has to say which. ListGrants() shows the ids.
         *
         * @param grantId the grant's own id.
         */
        void RevokeRole(const std::string &grantId) const;

        /**
         * @brief Lists grants: by principal, by role, or - naming neither - a whole account.
         *
         * @par
         * The two questions this model exists to answer are "what may they do" and "who can do
         * this"; naming neither answers a third, "what is granted here at all", which is what an
         * overview wants and what one request per user would otherwise cost.
         *
         * @par
         * Note that a principal's grants are its *own* and not those of the groups it belongs to,
         * which is a different question - CheckPermission() answers the combined one.
         *
         * @param options which grants to list.
         */
        [[nodiscard]]
        Page<Grant> ListGrants(const ListGrantsOptions &options = {}) const;

        /**
         * @brief Asks whether a user would be allowed to do something, and says why.
         *
         * @par
         * Counts the grants of every group the user belongs to, the way a real request would.
         *
         * @param userId      the user to ask about.
         * @param target      module, e.g. "ens".
         * @param action      action, e.g. "publish-message".
         * @param nameSpace   namespace to ask about; empty is the account root.
         * @param resourceErn the resource, for the actions that name one.
         */
        [[nodiscard]]
        PermissionCheck CheckPermission(const std::string &userId, const std::string &target, const std::string &action,
                                        const std::string &nameSpace = {}, const std::string &resourceErn = {}) const;

        /**
         * @brief Every permission a role can hold, as "<module>:<action>".
         *
         * @par
         * Generated from what the modules actually dispatch, so it is exactly what can be granted -
         * and the two modules that are never grantable, "emd" and "emm", are named separately
         * rather than silently missing.
         */
        [[nodiscard]]
        boost::json::object ListPermissions() const;

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief EAM's own metrics, as the server collects them.
         *
         * @par
         * Answered unparsed - the shape belongs to the monitoring module rather than to EAM.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

        // -- transport --------------------------------------------------------------------------

        /**
         * @brief Sends any EAM action, for one this SDK does not wrap yet.
         *
         * @par
         * Public on purpose: a server that gains an action should be reachable without waiting for
         * a release here.
         *
         * @param action  the action, e.g. "list-users".
         * @param payload the request body.
         * @return the response body as an object; a response that is not an object is answered
         * under a "result" field.
         * @throws ServiceError if the server refused the call.
         * @throws EuclidError if the server could not be reached.
         */
        [[nodiscard]]
        boost::json::object Call(const std::string &action, const boost::json::object &payload = {}) const;

        /**
         * @brief Builds and authenticates one request, without sending it.
         *
         * @par
         * Exposed because it is what a caller debugging a refused signature wants to look at, and
         * what a module this SDK does not wrap yet is built out of.
         *
         * @param target the module, e.g. "eam".
         * @param action the action.
         * @param body   the request body.
         * @return the request, signed or carrying a bearer token as this session's AuthMode says.
         * @throws EuclidError if the mode is Signature and this session has no access key.
         */
        [[nodiscard]]
        Request NewRequest(const std::string &target, const std::string &action, const std::string &body) const;

        /**
         * @brief The connection this session sends on, for a module client built on top of it.
         */
        [[nodiscard]]
        const HttpClient &Client() const;

        // -- what a module client builds its requests out of ------------------------------------

        /**
         * @brief Sets the headers that say who is asking and what they are scoped to. Signed,
         * apart from the namespace.
         *
         * @par
         * Public because a module client is a request of another module made on this session's
         * identity, and these are what say whose identity that is - see CDK::ModuleClient.
         */
        void ApplyRoutingHeaders(Request &request) const;

        /**
         * @brief Signs the request, or sets the bearer token, as AuthMode says.
         *
         * @param request the request to authenticate.
         * @param target  the module it is addressed to, which the signature covers.
         * @throws EuclidError if the mode is Signature and this session has no access key.
         */
        void Authenticate(Request &request, const std::string &target) const;

        /**
         * @brief Sets the bearer token, whatever AuthMode says.
         *
         * @par
         * What the byte-carrying actions of a module use - ESM's put-object and the three like it -
         * so that every euclid client writes an object the same way. A session that asked for
         * AuthMode::Signature is signed instead: it asked not to be handed a token silently.
         */
        void ApplyBearerToken(Request &request) const;

        /**
         * @brief Whether this session was told to sign every request, byte-carrying ones included.
         */
        [[nodiscard]]
        bool AlwaysSigns() const;

    private:

        /**
         * @brief Whether this request is to be signed rather than to present the token.
         */
        [[nodiscard]]
        bool ShouldSign() const;

        SessionOptions _options;
        HttpClient _client;
        std::function<std::string()> _tokenProvider;
    };

}// namespace Euclid::CDK::EAM
