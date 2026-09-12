// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Ekm.h>

namespace Euclid::CDK::EKM {

    Key ToKey(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .description = Json::Text(value, "description"),
                .algorithm = Json::Text(value, "algorithm"),
                .length = Json::Number(value, "length"),
                .status = Json::Text(value, "status"),
                .tags = Json::StringMap(value, "tags"),
                .deletionDate = Json::Text(value, "deletionDate"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Certificate ToCertificate(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .description = Json::Text(value, "description"),
                .certificate = Json::Text(value, "certificate"),
                .subject = Json::Text(value, "subject"),
                .issuer = Json::Text(value, "issuer"),
                .serialNumber = Json::Text(value, "serialNumber"),
                .fingerprint = Json::Text(value, "fingerprint"),
                .subjectAltNames = Json::Strings(value, "subjectAltNames"),
                .generated = Json::Flag(value, "generated"),
                .notBefore = Json::Text(value, "notBefore"),
                .notAfter = Json::Text(value, "notAfter"),
                .tags = Json::StringMap(value, "tags"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    CreateKeyResult ToCreateKeyResult(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .description = Json::Text(value, "description"),
                .algorithm = Json::Text(value, "algorithm"),
                .length = Json::Number(value, "length"),
                .status = Json::Text(value, "status"),
        };
    }

    DeleteKeyResult ToDeleteKeyResult(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .deletionDate = Json::Text(value, "deletionDate"),
                .status = Json::Text(value, "status"),
        };
    }

    RevokeKeyResult ToRevokeKeyResult(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .status = Json::Text(value, "status"),
        };
    }

    KeyDescriptionResult ToKeyDescriptionResult(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .description = Json::Text(value, "description"),
        };
    }

    DeleteCertificateResult ToDeleteCertificateResult(const boost::json::value &value) {
        return {.name = Json::Text(value, "name"), .ern = Json::Text(value, "ern")};
    }

}// namespace Euclid::CDK::EKM
