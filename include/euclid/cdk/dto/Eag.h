// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>

/**
 * @file
 * @brief The shapes EAG sends back, and the readers that parse them.
 *
 * @par
 * One shape, a route, answered by four of the five actions - created, updated, read back and listed,
 * all identically. The fifth deletes and says nothing.
 *
 * @par
 * This is the third module whose wire spells the namespace "namespace" rather than "nameSpace"; EAP
 * and ETS are the others.
 */

namespace Euclid::CDK::EAG {

    /**
     * @brief Where an upload route puts what it receives, and what it will accept.
     *
     * @par
     * Only meaningful on a route of type TypeUpload. A proxy route carries this too, zeroed, because
     * the server answers every route with the same shape - so read it only when the type says to.
     */
    struct EUCLID_CDK_API UploadSpec {

        /**
         * @brief The ERN of the bucket objects are written to.
         */
        std::string bucket;

        /**
         * @brief The prefix every key is confined to, or empty for the whole bucket.
         *
         * @par
         * The home directory of this route, in a bucket: without one, anybody who may upload at all
         * may overwrite any key the bucket holds.
         */
        std::string keyPrefix;

        /**
         * @brief The largest body this route accepts, in bytes; 0 is no limit beyond the listener's.
         */
        long maxBytes{};

        /**
         * @brief Bytes per part streamed to ESM, and the size under which a body is written with one
         * put-object rather than a multipart upload.
         */
        long partSize{};

        /**
         * @brief The content types accepted, or empty for any.
         */
        std::vector<std::string> contentTypes;
    };

    /**
     * @brief One resource the API gateway serves: a path, and what answers it.
     *
     * @par
     * The gateway routes by configuration rather than by convention, which is why an application's
     * name never appears in the URL a caller asks for. That is also what lets an application be
     * renamed, replaced or split across several routes without anything calling it having to change.
     *
     * @par
     * A route says nothing about *where* what it names is. Instances come and go as the autoscaler
     * sizes the pool and their ports are assigned when they start, so the gateway looks them up at
     * the moment it needs one.
     *
     * @par
     * Exactly one of applicationId, moduleTarget and upload.bucket is set, and which one is the
     * route's whole character - an application euclid runs, euclid itself, or a bucket.
     */
    struct EUCLID_CDK_API Route {

        /**
         * @brief The name this route is managed under, unique within the account and namespace.
         */
        std::string routeId;
        std::string ern;
        std::string accountId;
        std::string region;

        /**
         * @brief The namespace this route publishes in, which is not always the namespace of
         * whoever created it.
         */
        std::string nameSpace;

        /**
         * @brief The path this route answers for, matched as a prefix.
         *
         * @par
         * A prefix rather than an exact path, because a REST resource is a tree: one route for
         * "/orders" carries every operation beneath it. Where two routes both match, the longer one
         * wins, so a specific route can be carved out of a general one later without either being
         * rewritten.
         */
        std::string path;

        /**
         * @brief TypeProxy or TypeUpload - what the gateway does with a request this route matches.
         */
        std::string type;

        /**
         * @brief Where an upload route writes. Zeroed on a proxy route.
         */
        UploadSpec upload;

        /**
         * @brief The application that serves this path, as EAP knows it. Empty on a module or
         * upload route.
         */
        std::string applicationId;

        /**
         * @brief The euclid module this route reaches instead of an application, e.g. "eam". Empty
         * on an ordinary route.
         */
        std::string moduleTarget;

        /**
         * @brief The one action moduleTarget answers for on this route, e.g. "login".
         */
        std::string moduleAction;

        /**
         * @brief The HTTP methods this route answers for, upper case. Empty means all of them.
         */
        std::vector<std::string> methods;

        /**
         * @brief AuthNone, AuthEuclid or AuthBasic - what a caller must present before a request is
         * forwarded.
         */
        std::string authentication;

        /**
         * @brief Whether the gateway serves this route at all.
         *
         * @par
         * A route can be taken out of service without being deleted, so that something exposed by
         * mistake can be withdrawn in a hurry and put back knowing it returns exactly as it was.
         */
        bool active{};
        std::string created;
        std::string modified;

        /**
         * @brief Whether this route writes into a bucket rather than forwarding.
         */
        [[nodiscard]]
        bool IsUpload() const;

        /**
         * @brief Whether this route reaches euclid itself rather than an application.
         */
        [[nodiscard]]
        bool IsModuleRoute() const;

        /**
         * @brief Whether a caller has to present a credential to use this route.
         */
        [[nodiscard]]
        bool IsAuthenticated() const;
    };

    /**
     * @brief One port the gateway serves, and the certificate it serves it with.
     *
     * @par
     * A listener is configuration rather than a resource: it is written in the installation's
     * configuration file and read when EAG starts, which is why it can be listed and not created.
     * One port per namespace, or one unscoped port carrying everything.
     *
     * @par
     * Every certificate field is empty on an HTTP listener, which has no certificate to describe.
     */
    struct EUCLID_CDK_API Listener {

        /**
         * @brief The namespace this port carries, or empty for one that carries every route.
         */
        std::string nameSpace;
        long port{};

        /**
         * @brief ProtocolHttp or ProtocolHttps.
         */
        std::string protocol;

        /**
         * @brief Whether the gateway's ports are actually bound.
         *
         * @par
         * The same value on every listener, because the proxy binds all of them or none. A listener
         * that is listed and not serving is one whose port was taken or whose certificate could not
         * be loaded - it is listed precisely because that is the one somebody is looking for.
         */
        bool serving{};

        /**
         * @brief The name of the certificate this listener actually serves.
         *
         * @par
         * Not the one it named: naming none means the conventional certificate for its namespace,
         * and reporting that as empty would send somebody looking for a certificate that is there
         * under a name nothing told them.
         */
        std::string certificate;

        /**
         * @brief What the configuration wrote, empty when it named none - which is how "this
         * listener names its certificate" and "this listener takes the conventional one" are told
         * apart.
         */
        std::string certificateConfigured;

        /**
         * @brief Whether that certificate is in EKM.
         *
         * @par
         * False on an HTTPS listener means the port never came up: the certificate is generated
         * when it starts. Everything below is empty unless this is true.
         */
        bool certificateFound{};
        std::string certificateErn;
        std::string certificateSubject;
        std::string certificateIssuer;
        std::string certificateSerialNumber;
        std::string certificateFingerprint;
        std::vector<std::string> certificateSubjectAltNames;

        /**
         * @brief Whether euclid minted this itself because the listener needed something to start
         * with.
         *
         * @par
         * Worth reading: a caller rejects a self-signed certificate until it has been given it, so
         * which of the two this is decides whether the port works for anybody who was not told
         * about it.
         */
        bool certificateGenerated{};
        std::string certificateNotBefore;
        std::string certificateNotAfter;
        bool certificateExpired{};

        /**
         * @brief Whether this listener speaks TLS.
         */
        [[nodiscard]]
        bool IsHttps() const;
    };

    /**
     * @brief The gateway's ports, and whether any of them are bound.
     */
    struct EUCLID_CDK_API ListenersResult {
        std::vector<Listener> listeners;
        long total{};

        /**
         * @brief Whether the proxy is serving at all - the same value each listener carries.
         */
        bool serving{};
    };

    /**
     * @brief Reads an upload specification.
     */
    [[nodiscard]]
    EUCLID_CDK_API UploadSpec ToUploadSpec(const boost::json::value &value);

    /**
     * @brief Reads a route.
     */
    [[nodiscard]]
    EUCLID_CDK_API Route ToRoute(const boost::json::value &value);

    /**
     * @brief Reads one listener.
     */
    [[nodiscard]]
    EUCLID_CDK_API Listener ToListener(const boost::json::value &value);

    /**
     * @brief Reads a list-listeners response.
     */
    [[nodiscard]]
    EUCLID_CDK_API ListenersResult ToListenersResult(const boost::json::value &value);

}// namespace Euclid::CDK::EAG
