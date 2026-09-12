// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <optional>
#include <string>
#include <string_view>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Ess.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::ESS {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "ess";

    /**
     * @brief What a secret is stored with besides its value.
     */
    struct EUCLID_CDK_API CreateSecretOptions {
        std::string description;

        /**
         * @brief The EKM key to encrypt it under. Left empty, the server picks the account's own;
         * naming one puts this secret's life in the hands of that key, which is the point when a set
         * of secrets should be revocable together.
         */
        std::string keyErn;
    };

    /**
     * @brief What an update changes - and only what it names.
     *
     * @par
     * The distinction the server draws is between a field being sent and not being sent rather than
     * between its values, so a description left unset leaves the stored one alone while one set to
     * an empty string clears it. That is what std::optional says here.
     *
     * @par
     * A value is the exception: the server refuses an empty one, on the ground that a secret nobody
     * can read is not a secret. So an unset value leaves the stored one alone, and an empty one is
     * refused before the round trip rather than after it.
     *
     * @par
     * Naming a keyErn re-encrypts the value under that key, which is how a secret is moved off a key
     * that is being retired. An empty one leaves it where it is - there is no such thing as moving a
     * secret onto the empty key - and doing so is not a rotation: the version and the rotation date
     * are what a new value changes.
     */
    struct EUCLID_CDK_API UpdateSecretOptions {
        std::optional<std::string> value;
        std::optional<std::string> description;
        std::string keyErn;
    };

    /**
     * @brief ESS - euclid's secret store: values kept encrypted under an EKM key, fetched one at a
     * time.
     *
     * @par
     * @code
     * const ESS::Ess ess(session);
     * ess.CreateSecret("db-password", "hunter2", {.description = "the reporting database"});
     *
     * const auto password = ess.GetSecret("db-password").value;
     * @endcode
     *
     * @par
     * The value is encrypted with EKM before it is stored, so a secret's life is tied to a key's:
     * deleting that key there is what makes the value unrecoverable, whatever ESS still says about
     * it.
     *
     * @par
     * Only GetSecret() answers with a value. Everything else - listing, rotating, tagging - answers
     * with metadata alone, so those calls can be logged and printed without being the thing that
     * leaks it.
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API Ess final : public ModuleClient {
    public:

        /**
         * @brief Builds ESS's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Ess(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Ess(EAM::Session &&) = delete;

        /**
         * @brief Stores a secret, and answers with its metadata - never the value it was just given.
         *
         * @param name    the secret's name, unique within the account and namespace.
         * @param value   the value to encrypt and store; the server refuses an empty one.
         * @param options a description, and the key to encrypt it under.
         * @throws EuclidError if the value is empty, which the server refuses anyway - this just
         * says so before the round trip.
         */
        [[nodiscard]]
        Secret CreateSecret(const std::string &name, const std::string &value, const CreateSecretOptions &options = {}) const;

        /**
         * @brief One secret, decrypted: "value" is the value, "secret" the metadata around it.
         *
         * @par
         * The only call in this SDK that answers with a secret's value, and so the point at which
         * the value enters the process.
         */
        [[nodiscard]]
        SecretValue GetSecret(const std::string &name) const;

        /**
         * @brief One page of secrets, and how many exist in total. Metadata only.
         */
        [[nodiscard]]
        Page<Secret> ListSecrets(const ListOptions &options = {}) const;

        /**
         * @brief Replaces a secret's value, which is what a rotation is and what bumps its version.
         */
        [[nodiscard]]
        Secret RotateSecret(const std::string &name, const std::string &value) const;

        /**
         * @brief Changes a secret that already exists: its value, its description, the key it is
         * under, or any combination. Only what options names changes.
         *
         * @throws EuclidError if none of the three was named, or if the value was named and empty -
         * both of which the server refuses anyway, said here so that neither costs a round trip.
         */
        [[nodiscard]]
        Secret UpdateSecret(const std::string &name, const UpdateSecretOptions &options) const;

        /**
         * @brief Deletes a secret, outright. The value is gone; the key it was under is left alone.
         */
        [[nodiscard]]
        DeleteSecretResult DeleteSecret(const std::string &name) const;

        /**
         * @brief Tags a secret, and answers with it as it now reads.
         *
         * @par
         * A tag already there has its value replaced - ESS has no separate set-secret-tag action to
         * distinguish the two.
         */
        [[nodiscard]]
        Secret AddSecretTag(const std::string &name, const std::string &key, const std::string &value) const;

        /**
         * @brief Removes a tag from a secret, and answers with it as it now reads.
         */
        [[nodiscard]]
        Secret DeleteSecretTag(const std::string &name, const std::string &key) const;

        /**
         * @brief ESS's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to ESS.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

    private:

        /**
         * @brief The actions that answer with one secret's metadata, wrapped in a "secret" field.
         */
        [[nodiscard]]
        Secret SecretOf(const std::string &action, const boost::json::object &payload) const;
    };

}// namespace Euclid::CDK::ESS
