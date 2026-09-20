// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <string>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/dto/Com.h>
#include <euclid/cdk/dto/Page.h>

/**
 * @file
 * @brief The shapes ESM sends back, and the readers that parse them.
 *
 * @par
 * Structs rather than raw boost::json::value, exactly as dto/Eam.h does and for the same reason: a
 * typo in a field name is a compile error here instead of a null that travels. Field names are the
 * server's own (dto/include/euclid/dto/esm in the euclid repository). One is spelled differently:
 * the server's "async" flag - "the server answered before it had finished" - is "background" here,
 * because async is a keyword in the languages the other SDKs are written in and a field spelled the
 * same way in all of them is worth more than one spelled like the wire.
 */

namespace Euclid::CDK::ESM {

    /**
     * @brief A bucket: a named container of objects, scoped to an account and a namespace.
     */
    struct EUCLID_CDK_API Bucket {
        std::string name;
        std::string ern;
        std::string owner;
        long size{};
        long objects{};
        std::map<std::string, std::string> tags;

        /**
         * @brief Whether objects written from now on are encrypted at rest. Says nothing about the
         * ones already stored - see ESM::Esm::EnableEncryption().
         */
        bool encrypted{};
        std::string encryptionKeyErn;

        /**
         * @brief One of euclid's own buckets rather than somebody's. Left out of a listing unless
         * asked for.
         */
        bool internal{};
        std::string created;
        std::string modified;
    };

    /**
     * @brief One stored object, as a listing describes it.
     *
     * @par
     * "key" is opaque: a bucket has directories only in the sense that keys share a prefix, which is
     * why listing them is something a caller asks for rather than something that happens.
     */
    struct EUCLID_CDK_API Object {
        std::string ern;
        std::string bucketErn;
        std::string key;
        long size{};

        /**
         * @brief Lifecycle status: CREATED, UPLOADING, UPLOADED or COMPLETED.
         */
        std::string status;
        std::string contentType;
        std::string md5Sum;
        bool encrypted{};
        COM::VariantMap attributes;
        std::string created;
        std::string modified;
    };

    /**
     * @brief What a subscription delivers: one object event, as the body of an ordinary message.
     *
     * @par
     * Carries both attribute maps the object was written with, because that is what the server puts
     * in the notification: a consumer deciding what to do with an object does it from this, without
     * a round trip back to ESM for the attributes it was already told about.
     */
    struct EUCLID_CDK_API BucketEvent {
        std::string eventType;
        std::string bucketErn;
        std::string key;
        std::string ern;
        long size{};
        std::string contentType;
        std::string md5Sum;
        COM::VariantMap attributes;
        COM::VariantMap systemAttributes;
    };

    /**
     * @brief One user-defined attribute of an object, as the server stored it.
     */
    struct EUCLID_CDK_API ObjectAttribute {
        std::string ern;
        std::string name;
        COM::Variant value;
    };

    /**
     * @brief A newly created bucket: its name, and the ERN everything else names it by.
     */
    struct EUCLID_CDK_API CreateBucketResult {
        std::string name;
        std::string ern;
    };

    /**
     * @brief A renamed bucket, and how much was repointed at it.
     *
     * @par
     * The ERN is new as well as the name, so this is the one later calls have to use - nothing
     * answers to the old one afterwards.
     */
    struct EUCLID_CDK_API RenameBucketResult {
        std::string name;
        std::string ern;
        long objects{};
        long subscriptions{};
    };

    /**
     * @brief A bucket and the flag it now carries.
     */
    struct EUCLID_CDK_API SetBucketInternalResult {
        std::string ern;
        std::string name;
        bool internal{};
    };

    /**
     * @brief What a background bucket deletion took on.
     *
     * @par
     * Only a deletion asked to run in the background answers with anything at all - a bucket
     * deleted inline is simply gone by the time the call returns. So "background" is true whenever
     * this is worth reading, "count" is how many objects the bucket held when the work was taken
     * on, and "jobId" names the job doing it, which outlives the instance that started it.
     *
     * @par
     * The bucket itself goes when the emptying finishes, so it stays listed - and still deletable -
     * until it is genuinely gone.
     */
    struct EUCLID_CDK_API DeleteBucketResult {
        std::string ern;

        /**
         * @brief How many objects the bucket held when the deletion was taken on.
         */
        long count{};

        /**
         * @brief The background job doing the work.
         */
        std::string jobId;

        /**
         * @brief Whether the server is still working through it.
         */
        bool background{};
    };

    /**
     * @brief A purged bucket, and how many objects went.
     *
     * @par
     * "background" says the server answered before doing any of it, in which case "count" is how
     * many objects the bucket held when the purge was taken on rather than how many have gone, and
     * "jobId" names the job doing it. That job outlives the instance that started it - one stopped
     * by the autoscaler, or lost to a crash, leaves a job another instance picks up and carries on.
     */
    /**
     * @brief What an abandoned upload was, and what became of the object it was writing.
     */
    struct EUCLID_CDK_API AbortUploadResult {

        std::string uploadId;
        std::string bucketErn;
        std::string key;

        /**
         * @brief How many staged parts were thrown away.
         */
        long parts{};

        /**
         * @brief Whether the object row at that key went with the upload.
         *
         * @par
         * True for a first upload, whose row described bytes that never arrived. False for a
         * re-upload, where the row is the previous version - still published, still readable, and
         * not this upload's to delete. Worth reading rather than assuming: "the upload is gone"
         * and "the object is gone" are different outcomes, and a caller cleaning up after a
         * failure needs to know which one they got.
         */
        bool objectRemoved{};
    };

    struct EUCLID_CDK_API PurgeBucketResult {
        std::string ern;
        long count{};

        /**
         * @brief The background job doing the work, empty unless "background".
         */
        std::string jobId;

        /**
         * @brief Whether the server is still working through the objects.
         */
        bool background{};
    };

    /**
     * @brief How many keys were asked for and how many objects went.
     *
     * @par
     * The two differ when a key named nothing, which is not an error - it simply was not there to
     * delete - so a caller that cares compares them. "background" says the server answered before it
     * had finished, in which case "objects" is what it took on rather than what it removed.
     */
    struct EUCLID_CDK_API DeleteObjectsResult {
        std::string ern;
        long asked{};
        long objects{};
        bool background{};
    };

    /**
     * @brief The bucket whose objects were re-announced, and how many of them there were.
     */
    struct EUCLID_CDK_API TouchObjectResult {
        std::string ern;
        std::string bucketName;
        std::string prefix;
        long objects{};
        bool background{};
    };

    /**
     * @brief The key a bucket now encrypts under, and how many objects predate the change.
     *
     * @par
     * Those objects are not re-encrypted and not touched: each one records the key it was written
     * under and is read back through it. "keyCreated" says the key is new and belongs to EKM from
     * that moment on - deleting it there is what makes this bucket's objects unrecoverable.
     */
    struct EUCLID_CDK_API EnableEncryptionResult {
        std::string ern;
        std::string name;
        std::string keyErn;
        std::string keyId;
        std::string algorithm;
        bool keyCreated{};
        long existingObjects{};
    };

    /**
     * @brief The key a bucket was encrypting under, and how many objects are still under it.
     */
    struct EUCLID_CDK_API DisableEncryptionResult {
        std::string ern;
        std::string name;
        std::string previousKeyErn;
        std::string previousKeyId;
        long encryptedObjects{};
    };

    /**
     * @brief An object as it was stored, which is what both ways of writing one answer with.
     *
     * @par
     * "put-object" and "complete-upload" return the same payload, so a caller that needs the
     * object's ERN does not have to take the multipart path to get one.
     */
    struct EUCLID_CDK_API StoredObject {
        std::string ern;
        std::string bucketErn;
        std::string key;
        long size{};
        std::string status;
        std::string contentType;
        std::string md5Sum;
    };

    /**
     * @brief An opened multipart upload: the ID every part of it carries.
     */
    struct EUCLID_CDK_API CreateUploadResult {
        std::string uploadId;
        std::string bucketErn;
        std::string key;
    };

    /**
     * @brief An opened multipart download, and how large the object turned out to be.
     *
     * @par
     * The size is what says how many parts there are to ask for: unlike an upload, whose source the
     * caller has already measured, a download's size is not known until the server is asked.
     */
    struct EUCLID_CDK_API CreateDownloadResult {
        std::string downloadId;
        std::string bucketErn;
        std::string key;
        std::string ern;
        long size{};
        std::string contentType;
    };

    /**
     * @brief Reads a bucket.
     */
    [[nodiscard]]
    EUCLID_CDK_API Bucket ToBucket(const boost::json::value &value);

    /**
     * @brief Reads a stored object.
     */
    [[nodiscard]]
    EUCLID_CDK_API Object ToObject(const boost::json::value &value);

    /**
     * @brief Reads a bucket event.
     */
    [[nodiscard]]
    EUCLID_CDK_API BucketEvent ToBucketEvent(const boost::json::value &value);

    /**
     * @brief Reads an object attribute.
     */
    [[nodiscard]]
    EUCLID_CDK_API ObjectAttribute ToObjectAttribute(const boost::json::value &value);

    /**
     * @brief Reads a create-bucket response.
     */
    [[nodiscard]]
    EUCLID_CDK_API CreateBucketResult ToCreateBucketResult(const boost::json::value &value);

    /**
     * @brief Reads a rename-bucket response.
     */
    [[nodiscard]]
    EUCLID_CDK_API RenameBucketResult ToRenameBucketResult(const boost::json::value &value);

    /**
     * @brief Reads a set-bucket-internal response.
     */
    [[nodiscard]]
    EUCLID_CDK_API SetBucketInternalResult ToSetBucketInternalResult(const boost::json::value &value);

    /**
     * @brief Reads a purge-bucket response.
     */
    [[nodiscard]]
    EUCLID_CDK_API DeleteBucketResult ToDeleteBucketResult(const boost::json::value &value);

    EUCLID_CDK_API AbortUploadResult ToAbortUploadResult(const boost::json::value &value);

    EUCLID_CDK_API PurgeBucketResult ToPurgeBucketResult(const boost::json::value &value);

    /**
     * @brief Reads a delete-objects response.
     */
    [[nodiscard]]
    EUCLID_CDK_API DeleteObjectsResult ToDeleteObjectsResult(const boost::json::value &value);

    /**
     * @brief Reads a touch-object response.
     */
    [[nodiscard]]
    EUCLID_CDK_API TouchObjectResult ToTouchObjectResult(const boost::json::value &value);

    /**
     * @brief Reads an enable-encryption response.
     */
    [[nodiscard]]
    EUCLID_CDK_API EnableEncryptionResult ToEnableEncryptionResult(const boost::json::value &value);

    /**
     * @brief Reads a disable-encryption response.
     */
    [[nodiscard]]
    EUCLID_CDK_API DisableEncryptionResult ToDisableEncryptionResult(const boost::json::value &value);

    /**
     * @brief Reads a put-object or complete-upload response.
     */
    [[nodiscard]]
    EUCLID_CDK_API StoredObject ToStoredObject(const boost::json::value &value);

    /**
     * @brief Reads a create-upload response.
     */
    [[nodiscard]]
    EUCLID_CDK_API CreateUploadResult ToCreateUploadResult(const boost::json::value &value);

    /**
     * @brief Reads a create-download response.
     */
    [[nodiscard]]
    EUCLID_CDK_API CreateDownloadResult ToCreateDownloadResult(const boost::json::value &value);

}// namespace Euclid::CDK::ESM
