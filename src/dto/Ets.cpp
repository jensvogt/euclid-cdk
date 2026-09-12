// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Ets.h>
#include <euclid/cdk/ets/Ets.h>

namespace Euclid::CDK::ETS {

    bool TransferServer::IsRunning() const { return state == StateRunning; }

    TransferServer ToTransferServer(const boost::json::value &value) {
        return {
                .serverId = Json::Text(value, "serverId"),
                .ern = Json::Text(value, "ern"),
                .accountId = Json::Text(value, "accountId"),
                .region = Json::Text(value, "region"),
                // "namespace" here, where every other module spells it "nameSpace".
                .nameSpace = Json::Text(value, "namespace"),
                .runtimeName = Json::Text(value, "runtimeName"),
                .protocol = Json::Text(value, "protocol"),
                .address = Json::Text(value, "address"),
                .port = Json::Number(value, "port"),
                .bucketName = Json::Text(value, "bucketName"),
                .bucketErn = Json::Text(value, "bucketErn"),
                .homeDirectory = Json::Text(value, "homeDirectory"),
                .userIds = Json::Strings(value, "userIds"),
                .userGroups = Json::Strings(value, "userGroups"),
                .directories = Json::Strings(value, "directories"),
                .desiredState = Json::Text(value, "desiredState"),
                .state = Json::Text(value, "state"),
                .hostKey = Json::Text(value, "hostKey"),
                .pasvMin = Json::Number(value, "pasvMin"),
                .pasvMax = Json::Number(value, "pasvMax"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

}// namespace Euclid::CDK::ETS
