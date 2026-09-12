// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Eqs.h>

namespace Euclid::CDK::EQS {

    Queue ToQueue(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .owner = Json::Text(value, "owner"),
                .ern = Json::Text(value, "ern"),
                .tags = Json::StringMap(value, "tags"),
                .size = Json::Number(value, "size"),
                .delay = Json::Number(value, "delay"),
                .available = Json::Number(value, "available"),
                .delayed = Json::Number(value, "delayed"),
                .invisible = Json::Number(value, "invisible"),
                .visibility = Json::Number(value, "visibility"),
                .maxMessageLength = Json::Number(value, "maxMessageLength"),
                .maxReceiveCount = Json::Number(value, "maxReceiveCount"),
                // The server has never renamed this one, so the wire name is the AWS spelling.
                .deadLetterQueueErn = Json::Text(value, "deadLetterQueueArn"),
                .priority = Json::Text(value, "priority"),
                .status = Json::Text(value, "status"),
                .internal = Json::Flag(value, "internal"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Message ToMessage(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .queueErn = Json::Text(value, "queueErn"),
                .messageId = Json::Text(value, "messageId"),
                .status = Json::Text(value, "status"),
                .priority = Json::Text(value, "priority"),
                .body = Json::Text(value, "body"),
                .receiptHandle = Json::Text(value, "receiptHandle"),
                .size = Json::Number(value, "size"),
                .receivedCount = Json::Number(value, "receivedCount"),
                .contentType = Json::Text(value, "contentType"),
                .attributes = COM::ToVariantMap(value, "attributes"),
                .systemAttributes = COM::ToVariantMap(value, "systemAttributes"),
                .lastReceived = Json::Text(value, "lastReceived"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    MessageAttribute ToMessageAttribute(const boost::json::value &value) {
        return {
                .messageId = Json::Text(value, "messageId"),
                .name = Json::Text(value, "name"),
                .value = COM::ToVariant(Json::Child(value, "value")),
        };
    }

    CreateQueueResult ToCreateQueueResult(const boost::json::value &value) {
        return {.name = Json::Text(value, "name"), .ern = Json::Text(value, "ern")};
    }

    QueueMetadata ToQueueMetadata(const boost::json::value &value) {
        return {
                .region = Json::Text(value, "region"),
                .accountId = Json::Text(value, "accountId"),
                .owner = Json::Text(value, "owner"),
                .nameSpace = Json::Text(value, "nameSpace"),
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .size = Json::Number(value, "size"),
                .messages = Json::Number(value, "messages"),
        };
    }

    MessageCount ToMessageCount(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .available = Json::Number(value, "available"),
                .delayed = Json::Number(value, "delayed"),
                .invisible = Json::Number(value, "invisible"),
                .total = Json::Number(value, "total"),
        };
    }

    MessageMetadata ToMessageMetadata(const boost::json::value &value) {
        return {
                .messageId = Json::Text(value, "messageId"),
                .queueErn = Json::Text(value, "queueErn"),
                .receiptHandle = Json::Text(value, "receiptHandle"),
                .status = Json::Text(value, "status"),
                .priority = Json::Text(value, "priority"),
                .size = Json::Number(value, "size"),
                .receivedCount = Json::Number(value, "receivedCount"),
                .visibilityTimeout = Json::Number(value, "visibilityTimeout"),
                .contentType = Json::Text(value, "contentType"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    QueueStatusResult ToQueueStatusResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .status = Json::Text(value, "status"),
                .available = Json::Number(value, "available"),
        };
    }

    MaxMessageLengthResult ToMaxMessageLengthResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .maxMessageLength = Json::Number(value, "maxMessageLength"),
                // Falls back to the stored figure, which is what it is whenever that is not zero.
                .effectiveMaxMessageLength = Json::Number(value, "effectiveMaxMessageLength", Json::Number(value, "maxMessageLength")),
        };
    }

    RedriveTarget ToRedriveTarget(const boost::json::value &value) {
        return {.queueErn = Json::Text(value, "queueErn"), .messages = Json::Number(value, "messages")};
    }

    RedriveDlqResult ToRedriveDlqResult(const boost::json::value &value) {

        RedriveDlqResult result{
                .ern = Json::Text(value, "ern"),
                .messages = Json::Number(value, "messages"),
                .remaining = Json::Number(value, "remaining"),
                .targets = {},
                .note = Json::Text(value, "note"),
        };
        for (const auto &document: Json::Documents(value, "targets")) {
            result.targets.push_back(ToRedriveTarget(document));
        }
        return result;
    }

}// namespace Euclid::CDK::EQS
