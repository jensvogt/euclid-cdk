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

        /**
         * @brief Priority the notifications this bucket sends are given, empty when it sets none.
         *
         * @par
         * Nothing about the bucket depends on it: a bucket is not consumed from and has no queue of
         * its own. It is handed to the messages a subscription of this bucket turns an object event
         * into - see ESM::Esm::SetBucketPriority().
         */
        std::string priority;

        /**
         * @brief Whether this bucket keeps the previous object when a key is written again.
         *
         * @par
         * Off by default, because keeping every version costs disk for as long as the bucket
         * exists. ESM::Esm::ListObjectVersions() is what reads them back.
         */
        bool versioning{};

        /**
         * @brief Whether the bucket's events are being held.
         *
         * @par
         * A stopped bucket still stores and serves objects - what stops is the notifications about
         * them. See ESM::Esm::StopBucket().
         */
        bool stopped{};
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
     * @brief What set-bucket-priority answers with: the bucket, and the priority it now carries.
     */
    struct EUCLID_CDK_API SetBucketPriorityResult {
        std::string ern;
        std::string name;

        /**
         * @brief The priority as stored - upper case, or empty when it was cleared.
         */
        std::string priority;
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

    /**
     * @brief A purged bucket, and how many objects went.
     *
     * @par
     * "background" says the server answered before doing any of it, in which case "count" is how
     * many objects the bucket held when the purge was taken on rather than how many have gone, and
     * "jobId" names the job doing it. That job outlives the instance that started it - one stopped
     * by the autoscaler, or lost to a crash, leaves a job another instance picks up and carries on.
     */
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
     * @brief Whether a bucket keeps previous versions, after a change.
     */
    struct EUCLID_CDK_API BucketVersioningResult {
        std::string ern;
        std::string name;
        bool versioning{};
    };

    /**
     * @brief One stored version of one key.
     */
    struct EUCLID_CDK_API ObjectVersion {

        /**
         * @brief What names this version in the calls that take one.
         */
        std::string versionId;

        /**
         * @brief Whether this is the object a plain read of the key returns.
         */
        bool latest{};
        long size{};
        std::string md5Sum;
        std::string contentType;
        std::string status;

        /**
         * @brief LifecycleStandard or LifecycleArchived.
         */
        std::string lifecycleState;
        std::string owner;
        std::string modified;
    };

    /**
     * @brief Every version of one key, newest first.
     *
     * @par
     * The current object comes first with ObjectVersion::latest true, and the stored versions
     * follow. A key whose current object was deleted still lists its versions, which is what makes
     * a deletion recoverable.
     */
    struct EUCLID_CDK_API ObjectVersions {
        std::string bucketErn;
        std::string key;

        /**
         * @brief The key's own ERN, which is what versions are kept under.
         */
        std::string ern;

        /**
         * @brief Whether the bucket is keeping versions now - which is not whether these exist: a
         * bucket switched off still holds what it kept while it was on.
         */
        bool versioning{};
        std::vector<ObjectVersion> versions;
        long total{};
    };

    /**
     * @brief One rule saying what a bucket archives, and when.
     */
    struct EUCLID_CDK_API LifecycleRule {

        /**
         * @brief What this rule is called, unique within the bucket. Two rules of one name are
         * refused rather than the second replacing the first.
         */
        std::string id;

        /**
         * @brief The key prefix the rule covers, or empty for the whole bucket.
         */
        std::string prefix;

        /**
         * @brief How old an object has to be before it is archived, in days.
         */
        long archiveAfterDays{};

        /**
         * @brief How long a restore of an object this rule covers stays online before it is
         * archived again, in days.
         */
        long restoreDays{};

        /**
         * @brief Whether the rule is acted on. A disabled rule is kept rather than deleted, so it
         * can be turned off without being rewritten from memory.
         */
        bool enabled{true};
    };

    /**
     * @brief A bucket's lifecycle rules, as they now stand.
     */
    struct EUCLID_CDK_API BucketLifecycle {
        std::string ern;
        std::string name;
        std::vector<LifecycleRule> rules;
    };

    /**
     * @brief Where one object stands between online and archived.
     *
     * @par
     * The dates are empty unless they apply: an object that was never archived has no archivedAt,
     * and one that is archived now has no restoredUntil.
     */
    struct EUCLID_CDK_API ObjectLifecycle {
        std::string ern;
        std::string bucketErn;
        std::string key;

        /**
         * @brief LifecycleStandard or LifecycleArchived.
         */
        std::string lifecycleState;
        std::string archivedAt;
        std::string restoredAt;

        /**
         * @brief When a restored object goes back to being archived. Empty on one that is not
         * restored, and it is what says a restore is temporary rather than permanent.
         */
        std::string restoredUntil;

        /**
         * @brief The archive the bytes are in while the object is archived.
         */
        std::string archiveFile;
    };

    /**
     * @brief What stopping or starting a bucket came to.
     */
    struct EUCLID_CDK_API BucketStateResult {
        std::string ern;
        std::string name;
        bool stopped{};

        /**
         * @brief How many deliveries are being held, after a stop.
         */
        long held{};

        /**
         * @brief How many were let go, after a start.
         */
        long released{};
    };

    /**
     * @brief One object whose file does not match its record.
     */
    struct EUCLID_CDK_API VerifyProblem {
        std::string key;
        std::string ern;

        /**
         * @brief What is wrong - a missing file, a size that disagrees, and so on.
         */
        std::string problem;

        /**
         * @brief What the record says the object is.
         */
        long recordedSize{};

        /**
         * @brief What the file on disk actually is.
         */
        long fileSize{};

        /**
         * @brief What the file should be, which is not recordedSize for an encrypted object - its
         * file carries a header.
         */
        long expectedFileSize{};
        std::string internalName;
        std::string internalDir;
        std::string created;
    };

    /**
     * @brief What one pass of a verification found.
     *
     * @par
     * A stat per object, and a header read for an encrypted one - never the bytes. Objects still
     * being uploaded, archived ones (whose bytes are in an archive rather than a file) and
     * directory markers are counted as skipped rather than checked.
     */
    struct EUCLID_CDK_API VerifyResult {
        std::string bucketErn;

        /**
         * @brief The key this pass started after.
         */
        std::string afterKey;
        long scanned{};
        long checked{};
        long skipped{};
        std::vector<VerifyProblem> problems;

        /**
         * @brief Where the next pass should start, or empty at the end of the bucket.
         */
        std::string nextKey;
    };

    /**
     * @brief Reads a bucket.
     */
    [[nodiscard]]
    EUCLID_CDK_API Bucket ToBucket(const boost::json::value &value);

    /**
     * @brief Reads a set-bucket-versioning response.
     */
    [[nodiscard]]
    EUCLID_CDK_API BucketVersioningResult ToBucketVersioningResult(const boost::json::value &value);

    /**
     * @brief Reads one version of an object.
     */
    [[nodiscard]]
    EUCLID_CDK_API ObjectVersion ToObjectVersion(const boost::json::value &value);

    /**
     * @brief Reads a list-object-versions response.
     */
    [[nodiscard]]
    EUCLID_CDK_API ObjectVersions ToObjectVersions(const boost::json::value &value);

    /**
     * @brief Reads one lifecycle rule.
     */
    [[nodiscard]]
    EUCLID_CDK_API LifecycleRule ToLifecycleRule(const boost::json::value &value);

    /**
     * @brief Reads a bucket's lifecycle rules.
     */
    [[nodiscard]]
    EUCLID_CDK_API BucketLifecycle ToBucketLifecycle(const boost::json::value &value);

    /**
     * @brief Reads an archive-object or restore-object response.
     */
    [[nodiscard]]
    EUCLID_CDK_API ObjectLifecycle ToObjectLifecycle(const boost::json::value &value);

    /**
     * @brief Reads a start-bucket or stop-bucket response.
     */
    [[nodiscard]]
    EUCLID_CDK_API BucketStateResult ToBucketStateResult(const boost::json::value &value);

    /**
     * @brief Reads one verification problem.
     */
    [[nodiscard]]
    EUCLID_CDK_API VerifyProblem ToVerifyProblem(const boost::json::value &value);

    /**
     * @brief Reads a verify-objects response.
     */
    [[nodiscard]]
    EUCLID_CDK_API VerifyResult ToVerifyResult(const boost::json::value &value);

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
     * @brief Reads what set-bucket-priority answered with.
     */
    [[nodiscard]]
    EUCLID_CDK_API SetBucketPriorityResult ToSetBucketPriorityResult(const boost::json::value &value);

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
