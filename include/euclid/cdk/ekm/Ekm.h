// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Ekm.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EKM {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "ekm";

    /**
     * @brief The only algorithm the server generates so far; anything else is refused with HTTP 400.
     */
    inline constexpr std::string_view Aes = "AES";

    /**
     * @brief The key length this SDK asks for when the caller does not say.
     *
     * @par
     * 128 is the other one the server accepts, and what euclid-jdk's no-argument createKey() mints;
     * 256 is what euclid itself creates when a bucket asks to be encrypted, which is the better
     * default to inherit.
     */
    inline constexpr long DefaultKeyLength = 256;

    /**
     * @brief How long a key scheduled for deletion stays alive by default, in days.
     *
     * @par
     * The server's own default when the field is left out, restated here because it is the one
     * number in this module that decides whether a mistake can be caught.
     */
    inline constexpr long DefaultPendingWindowDays = 7;

    /**
     * @brief What a key's status reads as: usable for both directions...
     */
    inline constexpr std::string_view KeyAvailable = "AVAILABLE";

    /**
     * @brief ...revoked, and so decrypting only...
     */
    inline constexpr std::string_view KeyRevoked = "REVOKED";

    /**
     * @brief ...or on its way out, and decrypting only until its deletion date passes.
     */
    inline constexpr std::string_view KeyPendingDeletion = "PENDING_DELETION";

    /**
     * @brief What a key is created as, and what it says it is for.
     */
    struct EUCLID_CDK_API CreateKeyOptions {

        /**
         * @brief Aes; the server generates nothing else so far.
         */
        std::string algorithm = std::string(Aes);

        /**
         * @brief 128 or 256 bits.
         */
        long length{DefaultKeyLength};

        /**
         * @brief What the key is for. Free text, never interpreted - and worth supplying; see
         * EKM::Ekm::CreateKey().
         */
        std::string description;
    };

    /**
     * @brief What a generated certificate is valid for, and for how long.
     */
    struct EUCLID_CDK_API CreateCertificateOptions {

        /**
         * @brief The certificate's common name. Defaults to the name it is stored under: for a
         * listener certificate those are usually the same word, and a certificate with an empty
         * subject is refused by everything that reads it.
         */
        std::string commonName;

        /**
         * @brief The other names it should be valid for.
         */
        std::vector<std::string> subjectAltNames;

        /**
         * @brief How long it is valid, in days, or zero for the server's default of 825.
         */
        long validDays{0};

        /**
         * @brief The RSA key length, or zero for the server's default of 2048.
         */
        long keyBits{0};
        std::string description;
    };

    /**
     * @brief EKM - euclid's key management module: encryption keys, and the certificates a
     * deployment serves.
     *
     * @par
     * @code
     * const EKM::Ekm ekm(session);
     * const auto key = ekm.CreateKey({.description = "customer exports"});
     *
     * const auto sealed = ekm.Encrypt(key.name, "account 4711");
     * const auto plain = ekm.Decrypt(key.name, sealed);
     * @endcode
     *
     * @par
     * Key material never leaves the server: Encrypt() and Decrypt() send the bytes to the key rather
     * than fetching the key to the bytes. That is what makes a key deletable as a unit - and what
     * makes deleting one final, since nothing anywhere else has a copy.
     *
     * @par
     * A key is named two ways, and they are not interchangeable. "name" is the ID the server minted
     * and is what encrypts, decrypts and is deleted; the ERN is what revokes, describes and tags.
     * Both are on every Key a listing returns.
     *
     * @par
     * "encrypt" and "decrypt" carry raw bytes rather than JSON, and present the session's bearer
     * token for the same reason ESM's transfer actions do - see CDK::ModuleClient.
     *
     * @par
     * There is no Metrics() here, unlike every other module in this SDK: EKM answers "get-metrics"
     * with HTTP 404, because its server never implemented the action. Call() reaches it if a later
     * euclid does.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API Ekm final : public ModuleClient {
    public:

        /**
         * @brief Builds EKM's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Ekm(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Ekm(EAM::Session &&) = delete;

        // -- keys -------------------------------------------------------------------------------

        /**
         * @brief Creates a key, and answers with the ID the server minted for it.
         *
         * @par
         * The description is worth supplying. A key is identified by that generated ID, which says
         * nothing about what the key protects, and a key outlives the reason it was made - so months
         * later this is the only thing that answers whether it can be deleted, and deleting one is
         * not a mistake that can be undone.
         */
        [[nodiscard]]
        CreateKeyResult CreateKey(const CreateKeyOptions &options = {}) const;

        /**
         * @brief One page of keys, and how many exist in total. Never their material.
         */
        [[nodiscard]]
        Page<Key> ListKeys(const ListOptions &options = {}) const;

        /**
         * @brief Schedules a key for deletion, and answers with the date it goes for good.
         *
         * @par
         * Scheduled rather than immediate, because this is the one action here that cannot be undone
         * by any other: everything the key encrypted - a bucket's objects, a secret's value - becomes
         * unreadable when the date passes, and the window is the only chance anybody gets to notice.
         * A key inside its window still decrypts.
         *
         * @param keyId               the key's ID, as Encrypt() takes - not its ERN.
         * @param pendingWindowInDays how long it stays alive, at least one day.
         */
        [[nodiscard]]
        DeleteKeyResult DeleteKey(const std::string &keyId, long pendingWindowInDays = DefaultPendingWindowDays) const;

        /**
         * @brief Stops a key encrypting anything further, without touching what it already wrote.
         *
         * @par
         * The difference from DeleteKey() is that nothing becomes unreadable: a revoked key still
         * decrypts, so this is what to reach for when a key should no longer be used but the data
         * under it is still wanted.
         *
         * @param ern the key's ERN - not its ID.
         */
        [[nodiscard]]
        RevokeKeyResult RevokeKey(const std::string &ern) const;

        /**
         * @brief Changes what a key says it is for.
         *
         * @par
         * Only the description changes: the material, algorithm, length, status and any scheduled
         * deletion are untouched, so describing a key neither prolongs nor shortens its life. An
         * empty string clears the description rather than leaving it alone - otherwise there would
         * be no way to remove one.
         *
         * @param ern         the key's ERN - not its ID.
         * @param description what it is for, or empty to clear it.
         */
        [[nodiscard]]
        KeyDescriptionResult SetKeyDescription(const std::string &ern, const std::string &description) const;

        /**
         * @brief Tags a key.
         *
         * @par
         * The tag is upserted, so one already there has its value replaced - EKM has no separate
         * set-key-tag action to distinguish the two.
         *
         * @param ern   the key's ERN.
         * @param key   the tag's name.
         * @param value the tag's value.
         */
        void AddKeyTag(const std::string &ern, const std::string &key, const std::string &value) const;

        /**
         * @brief Removes a tag from a key.
         */
        void DeleteKeyTag(const std::string &ern, const std::string &key) const;

        // -- using a key ------------------------------------------------------------------------

        /**
         * @brief Encrypts bytes with a key the server holds, and answers with IV || ciphertext ||
         * tag.
         *
         * @par
         * Those are the exact bytes Decrypt() takes back; nothing here needs to be unpacked or
         * re-assembled. Only a key whose status is KeyAvailable encrypts - a revoked one, or one
         * scheduled for deletion, is refused with HTTP 403.
         *
         * @param keyId     the key's ID - the "name" CreateKey() answered with - not its ERN.
         * @param plaintext the bytes to encrypt.
         */
        [[nodiscard]]
        std::string Encrypt(const std::string &keyId, std::string_view plaintext) const;

        /**
         * @brief Decrypts what Encrypt() produced.
         *
         * @par
         * Works for a revoked key and for one scheduled for deletion, right up until its deletion
         * date passes - which is the whole difference between revoking a key and deleting it.
         */
        [[nodiscard]]
        std::string Decrypt(const std::string &keyId, std::string_view ciphertext) const;

        // -- certificates -----------------------------------------------------------------------

        /**
         * @brief Stores a certificate somebody else issued, together with the private key that
         * proves it.
         *
         * @par
         * Both halves are required and the server checks them against each other: a certificate
         * stored with a key that is not its own is accepted silently by every step after this one
         * and only shows itself as a handshake that fails for every caller. A mismatch is HTTP 400
         * here instead.
         *
         * @par
         * The private key stays with EKM. It goes in and is never handed back - no action returns
         * one.
         */
        [[nodiscard]]
        Certificate ImportCertificate(const std::string &name, const std::string &certificatePem, const std::string &privateKeyPem, const std::string &description = {}) const;

        /**
         * @brief Generates a self-signed certificate, for an installation that has to serve HTTPS
         * before anybody has bought it a real one.
         *
         * @par
         * Nobody has vouched for the result - Certificate::generated says so, and a client still has
         * to be told to trust it.
         */
        [[nodiscard]]
        Certificate CreateCertificate(const std::string &name, const CreateCertificateOptions &options = {}) const;

        /**
         * @brief One stored certificate, by name. The PEM comes back; the private key does not.
         */
        [[nodiscard]]
        Certificate GetCertificate(const std::string &name) const;

        /**
         * @brief One page of certificates, and how many exist in total.
         */
        [[nodiscard]]
        Page<Certificate> ListCertificates(const ListOptions &options = {}) const;

        /**
         * @brief Deletes a certificate, outright and with no grace period.
         *
         * @par
         * Unlike DeleteKey() this needs none: nothing becomes unreadable, because a certificate is
         * public. A listener already serving it keeps the copy it loaded until it is restarted,
         * which is what makes this recoverable - import a replacement under the same name.
         */
        [[nodiscard]]
        DeleteCertificateResult DeleteCertificate(const std::string &name) const;

    private:

        /**
         * @brief encrypt and decrypt differ only in direction: both send opaque bytes, name their
         * key in a header, and answer with opaque bytes.
         */
        [[nodiscard]]
        std::string Transform(const std::string &action, const std::string &keyId, std::string_view data) const;

        /**
         * @brief The three actions that answer with one certificate, wrapped in a "certificate"
         * field.
         */
        [[nodiscard]]
        Certificate CertificateOf(const std::string &action, const boost::json::object &payload) const;
    };

}// namespace Euclid::CDK::EKM
