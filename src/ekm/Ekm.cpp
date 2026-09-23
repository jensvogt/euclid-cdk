// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>
#include <vector>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/ekm/Ekm.h>

namespace Euclid::CDK::EKM {

    namespace {

        /**
         * @brief The actions that carry raw bytes rather than JSON, and so present the session's
         * bearer token rather than a signature.
         */
        const std::vector<std::string> kByteActions{"encrypt", "decrypt"};

        /**
         * @brief A JSON array of strings, for the payload fields that take a list.
         */
        boost::json::array stringsOf(const std::vector<std::string> &values) {
            boost::json::array array;
            for (const auto &value: values) array.emplace_back(value);
            return array;
        }

    }// namespace

    Ekm::Ekm(const EAM::Session &session) : ModuleClient(session, std::string(EKM::Target), kByteActions) {}

    // -- keys ---------------------------------------------------------------------------------------

    CreateKeyResult Ekm::CreateKey(const CreateKeyOptions &options) const {
        const boost::json::object payload{
                {"algorithm", options.algorithm},
                {"length", options.length},
                {"description", options.description},
        };
        return ToCreateKeyResult(Call("create-key", payload));
    }

    Page<Key> Ekm::ListKeys(const ListOptions &options) const {
        return ToPage<Key>(Call("list-keys", ListPayload(options, "name")), "keys", ToKey);
    }

    Key Ekm::GetKey(const std::string &nameOrErn) const {
        // Sent as whichever of the two it is: the server resolves a name against the session's own
        // account and namespace, and a name put in the ERN field would simply not be found.
        const boost::json::object payload = nameOrErn.starts_with("ern:")
                                                    ? boost::json::object{{"ern", nameOrErn}}
                                                    : boost::json::object{{"name", nameOrErn}};
        return ToKey(Json::Child(Call("get-key", payload), "key"));
    }

    bool Ekm::ExistsKey(const std::string &nameOrErn) const {
        try {
            std::ignore = GetKey(nameOrErn);
        } catch (const ServiceError &error) {
            if (error.Status() == 404) return false;
            throw;
        }
        return true;
    }

    DeleteKeyResult Ekm::DeleteKey(const std::string &keyId, const long pendingWindowInDays) const {
        const boost::json::object payload{{"keyId", keyId}, {"pendingWindowInDays", pendingWindowInDays}};
        return ToDeleteKeyResult(Call("delete-key", payload));
    }

    RevokeKeyResult Ekm::RevokeKey(const std::string &ern) const {
        return ToRevokeKeyResult(Call("revoke-key", {{"ern", ern}}));
    }

    KeyDescriptionResult Ekm::SetKeyDescription(const std::string &ern, const std::string &description) const {
        return ToKeyDescriptionResult(Call("set-key-description", {{"ern", ern}, {"description", description}}));
    }

    void Ekm::AddKeyTag(const std::string &ern, const std::string &key, const std::string &value) const {
        std::ignore = Call("add-key-tag", {{"ern", ern}, {"key", key}, {"value", value}});
    }

    void Ekm::DeleteKeyTag(const std::string &ern, const std::string &key) const {
        std::ignore = Call("delete-key-tag", {{"ern", ern}, {"key", key}});
    }

    // -- using a key --------------------------------------------------------------------------------

    std::string Ekm::Encrypt(const std::string &keyId, const std::string_view plaintext) const {
        return Transform("encrypt", keyId, plaintext);
    }

    std::string Ekm::Decrypt(const std::string &keyId, const std::string_view ciphertext) const {
        return Transform("decrypt", keyId, ciphertext);
    }

    std::string Ekm::Transform(const std::string &action, const std::string &keyId, const std::string_view data) const {

        const auto response = PostBytes(action, std::string(data), {{"x-euclid-key-id", keyId}});
        if (!response.IsSuccess()) throw ServiceError(Target(), action, response.statusCode, response.body);

        return response.body;
    }

    // -- certificates -------------------------------------------------------------------------------

    Certificate Ekm::ImportCertificate(const std::string &name, const std::string &certificatePem, const std::string &privateKeyPem, const std::string &description) const {
        const boost::json::object payload{
                {"name", name},
                {"description", description},
                {"certificate", certificatePem},
                {"privateKey", privateKeyPem},
        };
        return CertificateOf("import-certificate", payload);
    }

    Certificate Ekm::CreateCertificate(const std::string &name, const CreateCertificateOptions &options) const {

        boost::json::object payload{
                {"name", name},
                {"description", options.description},
                {"commonName", options.commonName},
                {"subjectAltNames", stringsOf(options.subjectAltNames)},
        };
        // Left out rather than sent as zero, so the server's own defaults - 825 days, 2048 bits -
        // apply.
        if (options.validDays > 0) payload["validDays"] = options.validDays;
        if (options.keyBits > 0) payload["keyBits"] = options.keyBits;

        return CertificateOf("create-certificate", payload);
    }

    Certificate Ekm::GetCertificate(const std::string &name) const {
        return CertificateOf("get-certificate", {{"name", name}});
    }

    Page<Certificate> Ekm::ListCertificates(const ListOptions &options) const {
        return ToPage<Certificate>(Call("list-certificates", ListPayload(options, "name")), "certificates", ToCertificate);
    }

    DeleteCertificateResult Ekm::DeleteCertificate(const std::string &name) const {
        return ToDeleteCertificateResult(Call("delete-certificate", {{"name", name}}));
    }

    Certificate Ekm::CertificateOf(const std::string &action, const boost::json::object &payload) const {
        return ToCertificate(Json::Child(Call(action, payload), "certificate"));
    }

}// namespace Euclid::CDK::EKM
