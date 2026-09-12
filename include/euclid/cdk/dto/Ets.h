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
 * @brief The shape ETS sends back, and the reader that parses it.
 *
 * @par
 * One shape: every action but the listing answers with a transfer server, and the listing answers
 * with an array of them. Field names are the server's own; note that this is the one module whose
 * wire spells the namespace "namespace" rather than "nameSpace".
 */

namespace Euclid::CDK::ETS {

    /**
     * @brief A transfer server definition: an FTP or SFTP endpoint onto an ESM bucket.
     *
     * @par
     * ETS never speaks either protocol itself. It owns the definition - which protocol, which port,
     * who may log in, which bucket the files really live in - and euclid's manager is what turns
     * that into a running process. So this is a description of what should exist rather than of a
     * socket.
     *
     * @par
     * "desiredState" is what somebody asked for and "state" is what is observed running: the two
     * differing is a server starting up, and differing for long is one that cannot.
     */
    struct EUCLID_CDK_API TransferServer {
        std::string serverId;
        std::string ern;
        std::string accountId;
        std::string region;

        /**
         * @brief The namespace this server was defined in. Part of what identifies it: a serverId is
         * unique within an account and a namespace rather than across the installation.
         */
        std::string nameSpace;

        /**
         * @brief What the process, its socket and its log channel are named after - none of which
         * has an account or a namespace to live in, so none can be keyed by a server ID two
         * namespaces may each have. Issued once and never touched, so moving a server does not
         * orphan its process.
         */
        std::string runtimeName;

        /**
         * @brief "FTP" or "SFTP".
         */
        std::string protocol;

        /**
         * @brief The address the server binds to; "0.0.0.0" is every interface.
         */
        std::string address;
        long port{};

        /**
         * @brief The bucket the files live in, by name and by ERN. Named by name at creation.
         */
        std::string bucketName;
        std::string bucketErn;

        /**
         * @brief The key prefix a logged-in user lands in, which is what makes one bucket serve
         * several servers without either seeing the other's files.
         */
        std::string homeDirectory;

        /**
         * @brief The EAM users who may log in, and the groups whose members may.
         */
        std::vector<std::string> userIds;
        std::vector<std::string> userGroups;

        /**
         * @brief Key prefixes presented as directories, for the clients that will not show what they
         * cannot list.
         */
        std::vector<std::string> directories;

        /**
         * @brief "RUNNING" or "STOPPED", as asked for.
         */
        std::string desiredState;

        /**
         * @brief "RUNNING" or "STOPPED", as observed.
         */
        std::string state;

        /**
         * @brief SFTP only: the private SSH host key, generated on first start when this is empty.
         */
        std::string hostKey;

        /**
         * @brief FTP only: the passive-mode port range, which has to be open in whatever sits in
         * front.
         */
        long pasvMin{};
        long pasvMax{};
        std::string created;
        std::string modified;

        /**
         * @brief Whether a process is actually answering, which is not the same as having been
         * started.
         */
        [[nodiscard]]
        bool IsRunning() const;
    };

    /**
     * @brief Reads a transfer server.
     */
    [[nodiscard]]
    EUCLID_CDK_API TransferServer ToTransferServer(const boost::json::value &value);

}// namespace Euclid::CDK::ETS
