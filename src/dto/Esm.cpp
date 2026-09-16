// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Esm.h>

namespace Euclid::CDK::ESM {

    Bucket ToBucket(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .owner = Json::Text(value, "owner"),
                .size = Json::Number(value, "size"),
                .objects = Json::Number(value, "objects"),
                .tags = Json::StringMap(value, "tags"),
                .encrypted = Json::Flag(value, "encrypted"),
                .encryptionKeyErn = Json::Text(value, "encryptionKeyErn"),
                .internal = Json::Flag(value, "internal"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Object ToObject(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .bucketErn = Json::Text(value, "bucketErn"),
                .key = Json::Text(value, "key"),
                .size = Json::Number(value, "size"),
                .status = Json::Text(value, "status"),
                .contentType = Json::Text(value, "contentType"),
                .md5Sum = Json::Text(value, "md5Sum"),
                .encrypted = Json::Flag(value, "encrypted"),
                .attributes = COM::ToVariantMap(value, "attributes"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    BucketEvent ToBucketEvent(const boost::json::value &value) {
        return {
                .eventType = Json::Text(value, "eventType"),
                .bucketErn = Json::Text(value, "bucketErn"),
                .key = Json::Text(value, "key"),
                .ern = Json::Text(value, "ern"),
                .size = Json::Number(value, "size"),
                .contentType = Json::Text(value, "contentType"),
                .md5Sum = Json::Text(value, "md5Sum"),
                .attributes = COM::ToVariantMap(value, "attributes"),
                .systemAttributes = COM::ToVariantMap(value, "systemAttributes"),
        };
    }

    ObjectAttribute ToObjectAttribute(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .name = Json::Text(value, "name"),
                .value = COM::ToVariant(Json::Child(value, "value")),
        };
    }

    CreateBucketResult ToCreateBucketResult(const boost::json::value &value) {
        return {.name = Json::Text(value, "name"), .ern = Json::Text(value, "ern")};
    }

    RenameBucketResult ToRenameBucketResult(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                .objects = Json::Number(value, "objects"),
                .subscriptions = Json::Number(value, "subscriptions"),
        };
    }

    SetBucketInternalResult ToSetBucketInternalResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .name = Json::Text(value, "name"),
                .internal = Json::Flag(value, "internal"),
        };
    }

    DeleteBucketResult ToDeleteBucketResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .count = Json::Number(value, "objects"),
                .jobId = Json::Text(value, "jobId"),
                .background = Json::Flag(value, "async"),
        };
    }

    PurgeBucketResult ToPurgeBucketResult(const boost::json::value &value) {
        // "count" when the purge ran inline, "objects" when it was taken on: the same figure at two
        // points in the same work, and count reads it either way rather than a zero that only means
        // the other field name was used.
        const auto count = Json::Number(value, "count");
        return {
                .ern = Json::Text(value, "ern"),
                .count = count != 0 ? count : Json::Number(value, "objects"),
                .jobId = Json::Text(value, "jobId"),
                .background = Json::Flag(value, "async"),
        };
    }

    DeleteObjectsResult ToDeleteObjectsResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .asked = Json::Number(value, "asked"),
                .objects = Json::Number(value, "objects"),
                .background = Json::Flag(value, "async"),
        };
    }

    TouchObjectResult ToTouchObjectResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .bucketName = Json::Text(value, "bucketName"),
                .prefix = Json::Text(value, "prefix"),
                .objects = Json::Number(value, "objects"),
                .background = Json::Flag(value, "async"),
        };
    }

    EnableEncryptionResult ToEnableEncryptionResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .name = Json::Text(value, "name"),
                .keyErn = Json::Text(value, "keyErn"),
                .keyId = Json::Text(value, "keyId"),
                .algorithm = Json::Text(value, "algorithm"),
                .keyCreated = Json::Flag(value, "keyCreated"),
                .existingObjects = Json::Number(value, "existingObjects"),
        };
    }

    DisableEncryptionResult ToDisableEncryptionResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .name = Json::Text(value, "name"),
                .previousKeyErn = Json::Text(value, "previousKeyErn"),
                .previousKeyId = Json::Text(value, "previousKeyId"),
                .encryptedObjects = Json::Number(value, "encryptedObjects"),
        };
    }

    StoredObject ToStoredObject(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .bucketErn = Json::Text(value, "bucketErn"),
                .key = Json::Text(value, "key"),
                .size = Json::Number(value, "size"),
                .status = Json::Text(value, "status"),
                .contentType = Json::Text(value, "contentType"),
                .md5Sum = Json::Text(value, "md5Sum"),
        };
    }

    CreateUploadResult ToCreateUploadResult(const boost::json::value &value) {
        return {
                .uploadId = Json::Text(value, "uploadId"),
                .bucketErn = Json::Text(value, "bucketErn"),
                .key = Json::Text(value, "key"),
        };
    }

    CreateDownloadResult ToCreateDownloadResult(const boost::json::value &value) {
        return {
                .downloadId = Json::Text(value, "downloadId"),
                .bucketErn = Json::Text(value, "bucketErn"),
                .key = Json::Text(value, "key"),
                .ern = Json::Text(value, "ern"),
                .size = Json::Number(value, "size"),
                .contentType = Json::Text(value, "contentType"),
        };
    }

}// namespace Euclid::CDK::ESM
