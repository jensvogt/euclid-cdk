// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Eag.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EAG {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "eag";

    /**
     * @brief What the gateway does with a request a route matches.
     *
     * @par
     * TypeProxy forwards it to what the route names. TypeUpload is the endpoint itself: the body is
     * streamed into a bucket and nothing is forwarded anywhere.
     */
    inline constexpr std::string_view TypeProxy = "PROXY";
    inline constexpr std::string_view TypeUpload = "UPLOAD";

    /**
     * @brief What a caller has to present before a request is forwarded.
     *
     * @par
     * A property of the resource rather than of the gateway, because both kinds sit behind the same
     * port: a public read a browser makes with no credentials, and an operation only a euclid
     * principal may perform.
     */
    inline constexpr std::string_view AuthNone = "NONE";

    /**
     * @brief A euclid credential - whatever the gateway accepts, which is a bearer token, an
     * RFC 9421 message signature or SigV4.
     */
    inline constexpr std::string_view AuthEuclid = "EUCLID";

    /**
     * @brief HTTP Basic against a euclid user's password, for the callers a euclid credential does
     * not suit: a browser, which prompts when it is answered with WWW-Authenticate, and a script
     * with nothing but curl. A technical principal is refused here as it is at login.
     */
    inline constexpr std::string_view AuthBasic = "BASIC";

    /**
     * @brief What a listener speaks.
     */
    inline constexpr std::string_view ProtocolHttp = "http";
    inline constexpr std::string_view ProtocolHttps = "https";

    /**
     * @brief The methods a route may name, upper case.
     *
     * @par
     * Matched against this list server-side, so a typo is refused at configuration time rather than
     * becoming a route that quietly answers for nothing - or, on an update, one that stops answering
     * for what it used to.
     */
    inline constexpr std::string_view MethodGet = "GET";
    inline constexpr std::string_view MethodHead = "HEAD";
    inline constexpr std::string_view MethodPost = "POST";
    inline constexpr std::string_view MethodPut = "PUT";
    inline constexpr std::string_view MethodPatch = "PATCH";
    inline constexpr std::string_view MethodDelete = "DELETE";
    inline constexpr std::string_view MethodOptions = "OPTIONS";

    /**
     * @brief Bytes per part an upload route streams to ESM unless it says otherwise, and the size
     * under which a body is written with one put-object instead.
     *
     * @par
     * The same 5 MiB every other part size in euclid uses, for the same reason: small enough that a
     * failed part is cheap to lose, large enough that a big object is not thousands of round trips.
     */
    inline constexpr long DefaultPartSize = 5L * 1024 * 1024;

    /**
     * @brief What a route says beyond its path and what answers it.
     *
     * @par
     * Every field here is left out of the request when it is empty rather than sent empty, because
     * the server tells a field that was sent from one that was not: an empty namespace would publish
     * the route in the installation's root rather than in the session's, and an empty authentication
     * is refused outright.
     */
    struct EUCLID_CDK_API RouteOptions {

        /**
         * @brief The HTTP methods this route answers for; empty is all of them.
         *
         * @par
         * Empty rather than a list of every method, so that "all" keeps meaning all: a route written
         * before PATCH was in anybody's vocabulary should not quietly refuse it.
         *
         * @par
         * Two routes may share a path if their methods do not overlap, which is what lets reads and
         * writes of one resource go to different applications. An overlap is refused with HTTP 409,
         * because the winner would otherwise be whichever the sort happened to put first.
         */
        std::vector<std::string> methods;

        /**
         * @brief AuthNone, AuthEuclid or AuthBasic; empty takes the server's default, which is
         * AuthNone.
         */
        std::string authentication;

        /**
         * @brief The namespace this route publishes in; empty is the session's own.
         *
         * @par
         * Nameable because a route published for one namespace should not act in another just
         * because an administrator of the first happened to configure it. Bear in mind that every
         * other action here resolves a routeId in the namespace the request was made in, so a route
         * created for another namespace is managed from there - EAM::Session::ChangeNamespace().
         */
        std::string nameSpace;

        /**
         * @brief The region requests carried by this route act in; empty is the caller's own.
         */
        std::string region;

        /**
         * @brief Whether the gateway serves this route from the moment it is created.
         */
        bool active{true};
    };

    /**
     * @brief What an upload route accepts, and where it puts it.
     */
    struct EUCLID_CDK_API UploadRouteOptions {

        /**
         * @brief The prefix every key is confined to; empty is the whole bucket.
         *
         * @par
         * Worth setting: without one, anybody who may upload at all may overwrite any key the bucket
         * holds. ESM's grants are per bucket, and "may add but not replace" is not something they can
         * say.
         */
        std::string keyPrefix;

        /**
         * @brief The largest body this route accepts, in bytes; 0 is no limit beyond the listener's.
         *
         * @par
         * Per route because it is a property of what is published: a route taking data deliveries and
         * one taking profile pictures have nothing to say to each other about size.
         */
        long maxBytes{};

        /**
         * @brief Bytes per part streamed to ESM.
         */
        long partSize{DefaultPartSize};

        /**
         * @brief The content types accepted; empty is any.
         */
        std::vector<std::string> contentTypes;

        /**
         * @brief AuthEuclid or AuthBasic.
         *
         * @par
         * Not defaulted to AuthNone the way a proxy route is, and AuthNone is refused: an
         * unauthenticated upload route is a public write endpoint into somebody's bucket, which is
         * not a thing to be able to configure by leaving a field out.
         */
        std::string authentication{AuthEuclid};

        /**
         * @brief The rest of what any route says.
         */
        RouteOptions route;
    };

    /**
     * @brief What an update changes - and only what it names.
     *
     * @par
     * The server distinguishes a field being sent from one that is not rather than one value from
     * another, so an unset field leaves the route as it stands while one set to an empty string
     * clears it. That is what std::optional says here.
     *
     * @par
     * Naming an application clears the module target and action, and naming a module clears the
     * application: a route goes to one or the other, and leaving both set would make which one wins
     * depend on the proxy. Name one, never both - sending both leaves the module, whatever order
     * they were written in here.
     */
    struct EUCLID_CDK_API UpdateRouteOptions {

        /**
         * @brief The path; it still has to start with "/".
         */
        std::optional<std::string> path;
        std::optional<std::string> applicationId;
        std::optional<std::string> moduleTarget;
        std::optional<std::string> moduleAction;
        std::optional<std::vector<std::string>> methods;

        /**
         * @brief AuthNone, AuthEuclid or AuthBasic. An unrecognised one is refused rather than
         * defaulted: somebody asking for AuthEuclid and silently getting AuthNone would be handed a
         * public route they believe is protected.
         */
        std::optional<std::string> authentication;

        /**
         * @brief TypeProxy or TypeUpload. Changing this changes which of the other fields the route
         * is judged by, and the judgement is made on the route the update produces rather than the
         * one it started from.
         */
        std::optional<std::string> type;

        /**
         * @brief The bucket an upload route writes to, as an ERN.
         */
        std::optional<std::string> bucket;
        std::optional<std::string> keyPrefix;
        std::optional<long> maxBytes;
        std::optional<long> partSize;
        std::optional<std::vector<std::string>> contentTypes;
        std::optional<std::string> region;

        /**
         * @brief The namespace the route publishes in. Moving a route out of the session's namespace
         * is the last thing this client can do to it from here.
         */
        std::optional<std::string> nameSpace;

        /**
         * @brief Whether the gateway serves this route. SetRouteActive() is the same thing said
         * shorter.
         */
        std::optional<bool> active;
    };

    /**
     * @brief EAG - euclid's API gateway: the routes it publishes, and the ports it publishes them
     * on.
     *
     * @par
     * @code
     * const auto session = EAM::Eam::ForServer(url).Credentials("jens", "secret").Login();
     * const EAG::Eag eag(session);
     *
     * const auto route = eag.CreateRoute("orders", "/orders", "order-service",
     *                                    {.methods = {std::string(EAG::MethodGet)},
     *                                     .authentication = std::string(EAG::AuthEuclid)});
     * @endcode
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time: an EAM::Session::ChangeNamespace() between two calls scopes the second one.
     * The session has to outlive the client.
     *
     * @par What a route is
     * A path, and the one thing that answers it: an application EAP runs, a euclid module, or a
     * bucket. Which of the three decides everything else about the route, which is why there is a
     * call for each rather than one call with a set of fields that contradict each other. The
     * gateway routes by configuration rather than by convention, so the name of whatever answers
     * never appears in the URL a caller asks for - and an application can be renamed, replaced or
     * split across several routes without anything calling it having to change.
     *
     * @par Namespaces
     * A route is created in the session's namespace unless RouteOptions::nameSpace names another,
     * and every other action here - update, get, delete, list - resolves a routeId in the namespace
     * the request was made in. A route published for another namespace is therefore managed from
     * that namespace rather than from the one it was created in.
     *
     * @par
     * Every action here is administrator-only, server-side. What they configure is which path
     * reaches which application and whether a caller needs a credential at all, which is not
     * something an application's own identity should be able to change.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Eag final : public ModuleClient {
    public:

        /**
         * @brief Builds EAG's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Eag(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Eag(EAM::Session &&) = delete;

        // -- publishing -------------------------------------------------------------------------

        /**
         * @brief Publishes a path, served by an application EAP runs.
         *
         * @par
         * The path is matched as a prefix, because a REST resource is a tree: one route for
         * "/orders" carries every operation beneath it and nobody has to enumerate them. Where two
         * routes both match, the longer one wins - so a general route can be placed over an
         * application and a more specific one carved out of it later without either being reordered
         * or rewritten.
         *
         * @par
         * The application has to exist, in this route's own account and namespace, or the call is
         * refused with HTTP 404. A route to nothing answers 503 for every request, which looks like
         * an application that is down rather than one that was never deployed.
         *
         * @param applicationId the application that serves the path, as EAP knows it.
         * @throws ServiceError 400 if the path does not start with "/", or a method is not a method;
         * 404 if the application is not there; 409 if the routeId is taken, or if another route
         * already answers for this path and one of these methods.
         */
        [[nodiscard]]
        Route CreateRoute(const std::string &routeId, const std::string &path,
                          const std::string &applicationId, const RouteOptions &options = {}) const;

        /**
         * @brief Publishes a path that reaches euclid itself rather than an application.
         *
         * @par
         * The way in for something outside euclid that needs euclid - a browser that has to log in
         * before it can call anything, most of all. Without it a front end would talk to the API
         * gateway for the application and to euclid's own gateway for its credentials: two ports,
         * two origins, and CORS between them.
         *
         * @par
         * One action per route, deliberately, rather than reading it from the rest of the path. A
         * route for "/euclid" that passed its remaining segments through as actions would publish
         * every action the module has, including the ones that delete users - and publishing an
         * administrative interface by accident is not a mistake that announces itself.
         *
         * @param moduleTarget the euclid module, e.g. "eam"; a name that is not one is refused
         * rather than published and left answering 404 forever with nothing saying why.
         * @param moduleAction the one action it answers for on this route, e.g. "login".
         */
        [[nodiscard]]
        Route CreateModuleRoute(const std::string &routeId, const std::string &path,
                                const std::string &moduleTarget, const std::string &moduleAction,
                                const RouteOptions &options = {}) const;

        /**
         * @brief Publishes a path that writes what it receives into a bucket.
         *
         * @par
         * The endpoint rather than a way to one: nothing is forwarded, and the body is streamed into
         * ESM as it arrives - as one put-object when it is smaller than the part size, and as a
         * multipart upload when it is not. Naming an application or a module on such a route is
         * refused rather than ignored, since it says the author expected the request to go somewhere
         * it will not.
         *
         * @par
         * An upload route must be authenticated; see UploadRouteOptions::authentication for why
         * that is refused rather than defaulted.
         *
         * @param bucket the bucket objects are written to, as an ERN. It has to exist: a route to a
         * bucket that is not there accepts a whole upload before discovering it has nowhere to put
         * it, and the caller has by then spent however long it takes to send one.
         */
        [[nodiscard]]
        Route CreateUploadRoute(const std::string &routeId, const std::string &path,
                                const std::string &bucket, const UploadRouteOptions &options = {}) const;

        /**
         * @brief Changes a route. Only what options names changes.
         *
         * @par
         * What makes a route coherent is the combination rather than any one field, so the result is
         * judged as a whole once everything has been applied: an update that turns a proxy route
         * into an upload route has to name a bucket in the same call, and one that names a bucket
         * without changing the type is refused for naming a bucket on a route that forwards.
         *
         * @param routeId the route, resolved in the namespace this session works in.
         * @throws ServiceError 404 if there is no such route in this namespace, 409 if the path and
         * methods it would end up with are already answered by another route.
         */
        [[nodiscard]]
        Route UpdateRoute(const std::string &routeId, const UpdateRouteOptions &options = {}) const;

        /**
         * @brief Puts a route into service, or takes it out.
         *
         * @par
         * What makes it possible to stop exposing something in a hurry and put it back afterwards
         * knowing it returns exactly as it was, rather than deleting it and writing it again from
         * memory. An inactive route still holds its path, so nothing else can claim it meanwhile.
         */
        [[nodiscard]]
        Route SetRouteActive(const std::string &routeId, bool active) const;

        /**
         * @brief Deletes a route. The path stops being served on the gateway's next refresh.
         *
         * @par
         * Deleting a route that is not there is not an error - what was asked for is the case.
         */
        void DeleteRoute(const std::string &routeId) const;

        // -- reading ----------------------------------------------------------------------------

        /**
         * @brief The routes of this account and namespace, in no particular order.
         *
         * @par
         * Unpaged, unlike most listings in this SDK, because the server answers with all of them:
         * routes are configuration written by an administrator rather than data that accumulates.
         *
         * @param pathPrefix the prefix a route's path has to start with, or empty for all of them.
         * A path rather than a routeId, and matched literally - a prefix containing "." matches a
         * dot rather than any character.
         */
        [[nodiscard]]
        std::vector<Route> ListRoutes(const std::string &pathPrefix = "") const;

        /**
         * @brief One route, described exactly as a listing describes each of its own.
         *
         * @throws ServiceError 404 if this namespace has no route of that ID.
         */
        [[nodiscard]]
        Route GetRoute(const std::string &routeId) const;

        /**
         * @brief The ports the gateway serves, and whether they are bound.
         *
         * @par
         * Listed rather than created: a listener is written in the installation's configuration and
         * read when EAG starts. What this answers is therefore what the gateway is doing rather than
         * what somebody asked for, which is the difference worth having - ListenersResult::serving
         * false with listeners configured is a port that was taken or a certificate that could not
         * be loaded.
         */
        [[nodiscard]]
        ListenersResult ListListeners() const;

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief EAG's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to EAG.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

    private:

        /**
         * @brief The actions that answer with one route, which is all but two of them.
         */
        [[nodiscard]]
        Route RouteOf(const std::string &action, const boost::json::object &payload) const;

        /**
         * @brief The fields every route carries, put onto a payload that already says what answers
         * it.
         */
        static void ApplyRouteOptions(boost::json::object &payload, const RouteOptions &options);
    };

}// namespace Euclid::CDK::EAG
