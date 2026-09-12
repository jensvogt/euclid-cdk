// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Ess.h>

namespace Euclid::CDK::ESS {

    Secret ToSecret(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .description = Json::Text(value, "description"),
                .encryptionKeyErn = Json::Text(value, "encryptionKeyErn"),
                .version = Json::Number(value, "version"),
                .rotated = Json::Text(value, "rotated"),
                .tags = Json::StringMap(value, "tags"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    SecretValue ToSecretValue(const boost::json::value &value) {
        return {.value = Json::Text(value, "value"), .secret = ToSecret(Json::Child(value, "secret"))};
    }

    DeleteSecretResult ToDeleteSecretResult(const boost::json::value &value) {
        return {.name = Json::Text(value, "name"), .ern = Json::Text(value, "ern")};
    }

}// namespace Euclid::CDK::ESS
