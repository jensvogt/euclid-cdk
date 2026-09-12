// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <string>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/dto/Page.h>

/**
 * @file
 * @brief The shapes ESS sends back, and the readers that parse them.
 *
 * @par
 * One thing is deliberately absent from Secret: the value. Every action but "get-secret" answers
 * with a secret's metadata only, so a listing, a rotation and a tag change can be logged, printed
 * and passed around without any of them being the thing that leaks it.
 */

namespace Euclid::CDK::ESS {

    /**
     * @brief A secret's metadata: everything about it except what it is.
     *
     * @par
     * "version" is how many times the value has been replaced, and "rotated" when that last happened
     * - which together are what an audit of "has this been rotated since the incident" actually
     * reads. Moving a secret to another key changes neither: that changes how the value is
     * protected, not what it is.
     */
    struct EUCLID_CDK_API Secret {
        std::string name;
        std::string ern;
        std::string description;

        /**
         * @brief The EKM key the value is encrypted under. Deleting that key there is what makes
         * this secret's value unrecoverable, whatever ESS still says about it.
         */
        std::string encryptionKeyErn;
        long version{};
        std::string rotated;
        std::map<std::string, std::string> tags;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A secret, decrypted: its value, and the metadata that goes with it.
     *
     * @par
     * The only shape in this SDK that carries a secret's value, and the only action that produces
     * one. Whatever a caller does with "value", this object is the point at which the value entered
     * the process - which is worth knowing when deciding what to log.
     */
    struct EUCLID_CDK_API SecretValue {
        std::string value;
        ESS::Secret secret;
    };

    /**
     * @brief The name and ERN of a deleted secret.
     */
    struct EUCLID_CDK_API DeleteSecretResult {
        std::string name;
        std::string ern;
    };

    /**
     * @brief Reads a secret's metadata.
     */
    [[nodiscard]]
    EUCLID_CDK_API Secret ToSecret(const boost::json::value &value);

    /**
     * @brief Reads a get-secret response, value included.
     */
    [[nodiscard]]
    EUCLID_CDK_API SecretValue ToSecretValue(const boost::json::value &value);

    /**
     * @brief Reads a delete-secret response.
     */
    [[nodiscard]]
    EUCLID_CDK_API DeleteSecretResult ToDeleteSecretResult(const boost::json::value &value);

}// namespace Euclid::CDK::ESS
