// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/dto/Page.h>

/**
 * @file
 * @brief The shapes EKM sends back, and the readers that parse them.
 *
 * @par
 * Nothing here carries key material: a key's bytes never leave the server, which is the point of
 * having a key module rather than a table of keys. What a caller gets is a handle - the key's name
 * to encrypt with, its ERN to administer - and the description it was given, which is what answers,
 * months later, whether the key can be deleted.
 */

namespace Euclid::CDK::EKM {

    /**
     * @brief An encryption key, described rather than disclosed.
     *
     * @par
     * "name" is the ID the server minted, and what EKM::Ekm::Encrypt() takes; "ern" is what the
     * administrative actions take. The two are not interchangeable, which is the one thing about EKM
     * worth remembering.
     */
    struct EUCLID_CDK_API Key {
        std::string name;
        std::string ern;
        std::string description;
        std::string algorithm;
        long length{};

        /**
         * @brief "AVAILABLE", "REVOKED" or "PENDING_DELETION". Only an available key encrypts; a
         * revoked one and one scheduled for deletion still decrypt what they wrote.
         */
        std::string status;
        std::map<std::string, std::string> tags;

        /**
         * @brief When a key scheduled for deletion goes for good. Empty for a key that is not.
         */
        std::string deletionDate;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A stored X.509 certificate.
     *
     * @par
     * "certificate" is the PEM, which is public and comes back. The private key does not: it goes in
     * once and no action returns it, so there is no field for it here.
     */
    struct EUCLID_CDK_API Certificate {
        std::string name;
        std::string ern;
        std::string description;

        /**
         * @brief The PEM-encoded certificate.
         */
        std::string certificate;
        std::string subject;
        std::string issuer;
        std::string serialNumber;
        std::string fingerprint;
        std::vector<std::string> subjectAltNames;

        /**
         * @brief Whether euclid generated and signed this itself, in which case nobody else has
         * vouched for it and a client still has to be told to trust it.
         */
        bool generated{};
        std::string notBefore;
        std::string notAfter;
        std::map<std::string, std::string> tags;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A newly created key. "name" is the ID the server minted - the only handle to it.
     */
    struct EUCLID_CDK_API CreateKeyResult {
        std::string name;
        std::string ern;
        std::string description;
        std::string algorithm;
        long length{};
        std::string status;
    };

    /**
     * @brief A key scheduled for deletion, and the date it goes for good.
     *
     * @par
     * Scheduled rather than deleted: everything the key encrypted becomes unreadable when that date
     * passes, and the window is the only chance anybody gets to notice.
     */
    struct EUCLID_CDK_API DeleteKeyResult {
        std::string name;
        std::string ern;
        std::string deletionDate;
        std::string status;
    };

    /**
     * @brief A revoked key: it encrypts nothing further, and still decrypts what it wrote.
     */
    struct EUCLID_CDK_API RevokeKeyResult {
        std::string name;
        std::string ern;
        std::string status;
    };

    /**
     * @brief A key as it now reads. Only the description changed - not the material, and not the
     * life.
     */
    struct EUCLID_CDK_API KeyDescriptionResult {
        std::string name;
        std::string ern;
        std::string description;
    };

    /**
     * @brief The name and ERN of a deleted certificate.
     */
    struct EUCLID_CDK_API DeleteCertificateResult {
        std::string name;
        std::string ern;
    };

    /**
     * @brief Reads a key.
     */
    [[nodiscard]]
    EUCLID_CDK_API Key ToKey(const boost::json::value &value);

    /**
     * @brief Reads a certificate.
     */
    [[nodiscard]]
    EUCLID_CDK_API Certificate ToCertificate(const boost::json::value &value);

    /**
     * @brief Reads a create-key response.
     */
    [[nodiscard]]
    EUCLID_CDK_API CreateKeyResult ToCreateKeyResult(const boost::json::value &value);

    /**
     * @brief Reads a delete-key response.
     */
    [[nodiscard]]
    EUCLID_CDK_API DeleteKeyResult ToDeleteKeyResult(const boost::json::value &value);

    /**
     * @brief Reads a revoke-key response.
     */
    [[nodiscard]]
    EUCLID_CDK_API RevokeKeyResult ToRevokeKeyResult(const boost::json::value &value);

    /**
     * @brief Reads a set-key-description response.
     */
    [[nodiscard]]
    EUCLID_CDK_API KeyDescriptionResult ToKeyDescriptionResult(const boost::json::value &value);

    /**
     * @brief Reads a delete-certificate response.
     */
    [[nodiscard]]
    EUCLID_CDK_API DeleteCertificateResult ToDeleteCertificateResult(const boost::json::value &value);

}// namespace Euclid::CDK::EKM
