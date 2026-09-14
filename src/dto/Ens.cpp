// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Ens.h>

namespace Euclid::CDK::ENS {

    Topic ToTopic(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .owner = Json::Text(value, "owner"),
                .ern = Json::Text(value, "ern"),
                .tags = Json::StringMap(value, "tags"),
                .size = Json::Number(value, "size"),
                .messages = Json::Number(value, "messages"),
                .maxMessageLength = Json::Number(value, "maxMessageLength"),
                .status = Json::Text(value, "status"),
                .retentionPeriod = Json::Number(value, "retentionPeriod"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Message ToMessage(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .topicErn = Json::Text(value, "topicErn"),
                .messageId = Json::Text(value, "messageId"),
                .status = Json::Text(value, "status"),
                .body = Json::Text(value, "body"),
                .contentType = Json::Text(value, "contentType"),
                .attributes = COM::ToVariantMap(value, "attributes"),
                .lastReceived = Json::Text(value, "lastReceived"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    MessageAttribute ToMessageAttribute(const boost::json::value &value) {
        return {
                .messageId = Json::Text(value, "messageId"),
                // "key" here and "name" in EQS, which is the server's own asymmetry.
                .key = Json::Text(value, "key"),
                .value = COM::ToVariant(Json::Child(value, "value")),
        };
    }

    CreateTopicResult ToCreateTopicResult(const boost::json::value &value) {
        return {.name = Json::Text(value, "name"), .ern = Json::Text(value, "ern")};
    }

    TopicMetadata ToTopicMetadata(const boost::json::value &value) {
        return {
                .region = Json::Text(value, "region"),
                .accountId = Json::Text(value, "accountId"),
                .owner = Json::Text(value, "owner"),
                .nameSpace = Json::Text(value, "nameSpace"),
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .size = Json::Number(value, "size"),
                .messages = Json::Number(value, "messages"),
                .status = Json::Text(value, "status"),
                .retentionPeriod = Json::Number(value, "retentionPeriod"),
                .held = Json::Number(value, "held"),
        };
    }

    TopicStateResult ToTopicStateResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .status = Json::Text(value, "status"),
                .released = Json::Number(value, "released"),
        };
    }

    ResendResult ToResendResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .resent = Json::Number(value, "resent"),
                .held = Json::Number(value, "held"),
        };
    }

    TopicRetentionResult ToTopicRetentionResult(const boost::json::value &value) {
        return {.ern = Json::Text(value, "ern"), .retentionPeriod = Json::Number(value, "retentionPeriod")};
    }

    MessageCount ToMessageCount(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .available = Json::Number(value, "available"),
                .send = Json::Number(value, "send"),
                .resend = Json::Number(value, "resend"),
        };
    }

}// namespace Euclid::CDK::ENS
