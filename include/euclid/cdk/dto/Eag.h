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
     * @brief Reads an upload specification.
     */
    [[nodiscard]]
    EUCLID_CDK_API UploadSpec ToUploadSpec(const boost::json::value &value);

    /**
     * @brief Reads a route.
     */
    [[nodiscard]]
    EUCLID_CDK_API Route ToRoute(const boost::json::value &value);

}// namespace Euclid::CDK::EAG
