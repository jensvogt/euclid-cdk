// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Eam.h>

namespace Euclid::CDK::EAM {

    Metadata ToMetadata(const boost::json::value &value) {
        return {
                .region = Json::Text(value, "region"),
                .accountId = Json::Text(value, "accountId"),
                .user = Json::Text(value, "user"),
        };
    }

    AccessKey ToAccessKey(const boost::json::value &value) {
        return {
                .accessKeyId = Json::Text(value, "accessKeyId"),
                // A key the server sent but did not describe is assumed usable: the field was added
                // after the shape was, and an older server's keys are all active ones.
                .active = Json::Flag(value, "active", true),
                .createdAt = Json::Text(value, "createdAt"),
        };
    }

    AccountGrant ToAccountGrant(const boost::json::value &value) {
        return {
                .accountId = Json::Text(value, "accountId"),
                .namespaces = Json::Strings(value, "namespaces"),
                .isAdmin = Json::Flag(value, "isAdmin"),
                .granted = Json::Text(value, "granted"),
        };
    }

    User ToUser(const boost::json::value &value) {
        User user{
                .userId = Json::Text(value, "userId"),
                .ern = Json::Text(value, "ern"),
                .password = Json::Text(value, "password"),
                .email = Json::Text(value, "email"),
                .accountId = Json::Text(value, "accountId"),
                .region = Json::Text(value, "region"),
                .accountGrants = {},
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
        for (const auto &grant: Json::Documents(value, "accountGrants")) {
            user.accountGrants.push_back(ToAccountGrant(grant));
        }
        return user;
    }

    UserGroup ToUserGroup(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .accountId = Json::Text(value, "accountId"),
                .region = Json::Text(value, "region"),
                .description = Json::Text(value, "description"),
                .userIds = Json::Strings(value, "userIds"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Account ToAccount(const boost::json::value &value) {
        return {
                .accountId = Json::Text(value, "accountId"),
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .description = Json::Text(value, "description"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Namespace ToNamespace(const boost::json::value &value) {
        return {
                .accountId = Json::Text(value, "accountId"),
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .description = Json::Text(value, "description"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    LoginResult ToLoginResult(const boost::json::value &value) {
        return {
                .token = Json::Text(value, "token"),
                .accessKeyId = Json::Text(value, "accessKeyId"),
                .secretAccessKey = Json::Text(value, "secretAccessKey"),
                .createdAt = Json::Text(value, "createdAt"),
                .isAdmin = Json::Flag(value, "isAdmin"),
                .metadata = ToMetadata(Json::Child(value, "metadata")),
                .raw = Json::Object(value),
        };
    }

    CreateAccessKeyResult ToCreateAccessKeyResult(const boost::json::value &value) {
        return {
                .accessKeyId = Json::Text(value, "accessKeyId"),
                .secretAccessKey = Json::Text(value, "secretAccessKey"),
                .createdAt = Json::Text(value, "createdAt"),
                .metadata = ToMetadata(Json::Child(value, "metadata")),
        };
    }

}// namespace Euclid::CDK::EAM
