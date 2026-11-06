// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Com.h>
#include <euclid/cdk/dto/Esm.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::ESM {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "esm";

    /**
     * @brief The object events a subscription can ask for. Asking for none asks for all of them.
     */
    inline constexpr std::string_view ObjectCreated = "esm.object.created";
    inline constexpr std::string_view ObjectUpdated = "esm.object.updated";
    inline constexpr std::string_view ObjectDeleted = "esm.object.deleted";

    /**
     * @brief How much of a file goes into one part.
     *
     * @par
     * Larger parts mean fewer round trips and more memory in flight; 5 MiB is what euclid-cli,
     * euclid-jdk, euclid-pdk and euclid-ndk use, which is what makes a file uploaded by one of them
     * arrive in the same pieces as one uploaded by another.
     */
    inline constexpr long DefaultPartSize = 5 * 1024 * 1024;

    /**
     * @brief How many parts travel at once.
     */
    inline constexpr int DefaultConcurrency = 4;

    /**
     * @brief How many attempts one step of a transfer gets. Transfers are long and made of many
     * steps, so a transient failure in any of them would otherwise throw away everything already
     * transferred.
     */
    inline constexpr int MaxPartAttempts = 4;

    /**
     * @brief The delay before retrying, multiplied by the attempt number.
     */
    inline constexpr std::chrono::milliseconds PartRetryBaseDelay{500};

    /**
     * @brief What "get-object" answers when the object is at or above the size the caller said it
     * would accept. Not an error in DownloadFile(): it is the server saying the object needs the
     * multipart path.
     */
    inline constexpr int PayloadTooLarge = 413;

    /**
     * @brief What an object's lifecycleState reads as.
     *
     * @par
     * LifecycleArchived means the bytes are in an archive rather than in a file of their own, so
     * the object is listed and described but cannot be downloaded until it is restored.
     */
    inline constexpr std::string_view LifecycleStandard = "STANDARD";
    inline constexpr std::string_view LifecycleArchived = "ARCHIVED";

    /**
     * @brief How many objects one VerifyObjects() pass looks at unless it says otherwise.
     */
    inline constexpr long DefaultVerifyLimit = 10000;

    /**
     * @brief The most it will look at in one pass.
     */
    inline constexpr long MaxVerifyLimit = 100000;

    /**
     * @brief The longest grace period RestoreObject() accepts, in days.
     */
    inline constexpr long MaxRestoreDays = 36500;

    /**
     * @brief How much of a bucket one verification pass covers, and where it starts.
     */
    struct EUCLID_CDK_API VerifyOptions {

        /**
         * @brief The key to start after, from the previous pass's VerifyResult::nextKey. Empty
         * starts at the beginning of the bucket.
         */
        std::string afterKey;

        /**
         * @brief At most this many objects, from 1 to MaxVerifyLimit.
         */
        long limit{DefaultVerifyLimit};
    };

    /**
     * @brief The two attribute maps a write carries, and they are not the same one.
     *
     * @par
     * "attributes" are the caller's own, listed back by ListObjectAttributes() and meaningless to
     * euclid. "systemAttributes" are euclid's envelope: they travel with the object across every hop
     * and are never mixed into the caller's. The one euclid acts on is "priority" - an object
     * written with systemAttributes {"priority", COM::PriorityLow} produces a notification carrying
     * it, which is how a producer's decision survives a hop through a bucket.
     */
    struct EUCLID_CDK_API AttributeOptions {
        COM::VariantMap attributes;
        COM::VariantMap systemAttributes;
    };

    /**
     * @brief How a file is cut up on the way out, and what the object it becomes carries.
     */
    struct EUCLID_CDK_API UploadOptions {
        long partSize{DefaultPartSize};
        int concurrency{DefaultConcurrency};
        COM::VariantMap attributes;
        COM::VariantMap systemAttributes;
    };

    /**
     * @brief How an object is fetched: in what parts, and how many at a time.
     */
    struct EUCLID_CDK_API DownloadOptions {
        long partSize{DefaultPartSize};
        int concurrency{DefaultConcurrency};
    };

    /**
     * @brief Which of a bucket's events a subscription asks for, and which keys they have to match.
     */
    struct EUCLID_CDK_API SubscribeOptions {

        /**
         * @brief ObjectCreated, ObjectUpdated, ObjectDeleted, or none of them for all of them.
         */
        std::vector<std::string> eventTypes;
        std::string prefix;

        /**
         * @brief Whether the zero-byte directory markers are delivered too.
         */
        bool directories{false};
    };

    /**
     * @brief Which of a bucket's objects are re-announced, and whether the caller waits for it.
     */
    struct EUCLID_CDK_API TouchObjectOptions {
        std::string prefix;
        bool background{false};
    };

    /**
     * @brief The notification a bucket subscription delivered, out of the message that carried it.
     *
     * @par
     * A subscription puts its notification into a queue or a topic as an ordinary message, so
     * nothing about receiving it is special - whatever reads that queue hands the message body to
     * this.
     *
     * @param messageBody the message body, as it was received.
     * @return the event; a body that is not JSON at all reads as an empty event rather than
     * throwing, which is what every reader in this SDK does with something it cannot parse.
     */
    [[nodiscard]]
    EUCLID_CDK_API BucketEvent ParseBucketEvent(const std::string &messageBody);

    /**
     * @brief ESM - euclid's storage module: buckets, objects, attributes, subscriptions and
     * transfers.
     *
     * @par
     * @code
     * const auto session = EAM::Eam::ForServer(url).Credentials("jens", "secret").Login();
     * const ESM::Esm esm(session);
     *
     * const auto bucket = esm.CreateBucket("reports");
     * esm.UploadFile(bucket.ern, "2026/q3.pdf", "q3.pdf");
     * @endcode
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time: an EAM::Session::ChangeNamespace() between two calls scopes the second one.
     * The session has to outlive the client.
     *
     * @par What is different about four of these
     * Most of what ESM does is the same JSON action every other module speaks. "put-object",
     * "get-object", "upload-part" and "download-part" are not: they carry the object's bytes
     * themselves, with the bucket, the key and the part number riding as headers instead of in a
     * body - which is what keeps a 5 MiB part 5 MiB on the wire rather than a third larger as base64
     * inside JSON. Those four also present the session's bearer token rather than a signature, which
     * is what euclid-cli and the other SDKs do for the same four actions, so every client writes
     * objects the same way. A session that asked for EAM::AuthMode::Signature signs them anyway.
     *
     * @par Threads
     * UploadFile() and DownloadFile() send their parts from several threads at once. That is safe
     * with one session because building and signing a request reads it without changing it, and
     * CDK::HttpClient makes a connection per request; what a caller has to keep in mind is that a
     * token provider set on the session is called from those threads too, and that changing the
     * session - ChangeNamespace(), SetScheme() - while a transfer is running changes it underneath
     * the parts still in flight. Anything else here is one request at a time, like the session.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Esm final : public ModuleClient {
    public:

        /**
         * @brief Builds ESM's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Esm(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Esm(EAM::Session &&) = delete;

        // -- how long a transfer may take -------------------------------------------------------

        /**
         * @brief How long the byte-carrying actions may take, or zero for the session's own timeout.
         *
         * @par
         * Worth raising: the session's default is sized for an action that answers from a database,
         * and a 5 MiB part on a slow link is not that.
         */
        void SetTransferTimeout(std::chrono::milliseconds timeout);

        /**
         * @brief The timeout the byte-carrying actions run on, or zero for the session's own.
         */
        [[nodiscard]]
        std::chrono::milliseconds TransferTimeout() const;

        /**
         * @brief The delay before one step of a transfer is tried again, multiplied by the attempt
         * number.
         *
         * @par
         * Settable because it is the one thing about a retry a caller may reasonably want to change:
         * nothing in a test suite wants to wait out a backoff that exists to be kind to a struggling
         * server.
         */
        void SetRetryBaseDelay(std::chrono::milliseconds delay);

        // -- buckets ----------------------------------------------------------------------------

        /**
         * @brief Creates a bucket, and answers with the ERN everything else names it by.
         *
         * @param name     the bucket's name, unique within the account and namespace.
         * @param internal marks it as euclid's own plumbing rather than somebody's bucket, which
         * leaves it out of an ordinary listing - see SetBucketInternal(), which is how a bucket that
         * already exists changes its mind about that.
         */
        [[nodiscard]]
        CreateBucketResult CreateBucket(const std::string &name, bool internal = false,
                                        const std::string &priority = {}) const;

        /**
         * @brief Deletes a bucket, and its objects with it.
         *
         * @par
         * Nothing else ever would: an object is only ever reached through its bucket, so a row left
         * behind would be unreachable for good and the file it names would be disk nothing accounts
         * for. PurgeBucket() is the one that empties a bucket and keeps it.
         *
         * @par
         * "background" is what a bucket of any size wants: emptying one can take minutes, and
         * holding a request open for all of it is a request that times out while the removal
         * carries on invisibly behind it. The server then answers as soon as it has written the
         * work down.
         *
         * @par
         * The bucket goes when the emptying finishes, so until then it stays listed - and still
         * deletable. A caller watching for it to disappear is watching the right thing.
         *
         * @par
         * Not [[nodiscard]], unlike most of these: deleting inline answers with nothing, since the
         * bucket is gone by then, and ignoring the result is the normal way to call it. It is only
         * worth reading when "background" is true.
         *
         * @param ern the bucket.
         * @param background whether the server answers before the bucket and its objects are gone.
         */
        DeleteBucketResult DeleteBucket(const std::string &ern, bool background = false) const;

        /**
         * @brief One page of buckets, and how many exist in total.
         *
         * @param options         paging, ordering and prefix.
         * @param includeInternal whether euclid's own buckets are in it. Left out by default, so a
         * listing shows what a person would recognise rather than the artifact bucket applications
         * are deployed from.
         */
        [[nodiscard]]
        Page<Bucket> ListBuckets(const ListOptions &options = {}, bool includeInternal = false) const;

        /**
         * @brief One bucket, by name or by ERN.
         *
         * @par
         * What comes back is exactly what ListBuckets() describes each of its own with - the ERN,
         * the account and namespace, the size and object count, encryption, tags and timestamps -
         * so this is the single-bucket form of a listing rather than another view of one.
         *
         * @par
         * A value starting with "ern:" is taken as an ERN and names one bucket in the
         * installation; anything else is a name and is resolved in the session's own account and
         * namespace, the way GetBucketErn() resolves one. A bucket that exists only in another
         * namespace is a 404 when asked for by name.
         *
         * @param nameOrErn the bucket's name, or its ERN.
         */
        [[nodiscard]]
        Bucket GetBucket(const std::string &nameOrErn) const;

        /**
         * @brief The ERN of the bucket of this name, in the session's account and namespace.
         */
        [[nodiscard]]
        std::string GetBucketErn(const std::string &name) const;

        /**
         * @brief Whether a bucket exists.
         *
         * @par
         * Three answers, not two. true and false are the ones a caller expects; the third is a
         * ServiceError, and it is the one that matters. An expired session, an unreachable gateway
         * or a refused permission is not the same as "not there", and returning false for them
         * would have callers deleting and recreating things over an outage. Only HTTP 404 - the
         * answer that actually says it is absent - becomes false; everything else is rethrown.
         *
         * @par
         * Asks about the bucket, not about anything in it: an empty bucket exists.
         *
         * @param name name of the bucket, resolved in the session's account and namespace.
         * @throws ServiceError if the question could not be answered.
         */
        [[nodiscard]]
        bool ExistsBucket(const std::string &name) const;

        /**
         * @brief How many bytes a bucket holds.
         */
        [[nodiscard]]
        long GetBucketSize(const std::string &ern) const;

        /**
         * @brief One object's size in bytes.
         *
         * @par
         * The figure the record carries, which is the object's own bytes - not the file behind it,
         * which is larger for an encrypted object. GetObject() and a listing both report the same
         * number.
         *
         * @throws ServiceError 404 if there is no such object, 409 if it has no final size yet
         * because it is still being uploaded.
         */
        [[nodiscard]]
        long GetObjectSize(const std::string &bucketErn, const std::string &key) const;

        /**
         * @brief Renames a bucket, and with it every object and subscription that named the old one.
         *
         * @par
         * The ERN changes too, and nothing answers to the old one afterwards, so the one in the
         * result is what later calls have to use. Refused rather than merged when a bucket of the
         * new name exists.
         */
        [[nodiscard]]
        RenameBucketResult RenameBucket(const std::string &ern, const std::string &newName) const;

        /**
         * @brief Marks a bucket as euclid's own plumbing, or stops doing so.
         *
         * @par
         * Separate from creating one because the bucket this exists for usually predates anybody
         * thinking about it, and reversible for the same reason: a flag that can only be set is one
         * nobody dares set.
         */
        [[nodiscard]]
        SetBucketInternalResult SetBucketInternal(const std::string &ern, bool internal = true) const;

        /**
         * @brief Sets the priority the notifications this bucket sends are given.
         *
         * @par
         * The bucket does nothing with it. A bucket is not consumed from and has no queue of its
         * own, so there is nothing here for a priority to mean - it exists to be handed on, to the
         * messages a subscription of this bucket turns an object event into. "Everything that lands
         * in this bucket is urgent" is the statement it makes, and the queue on the other side of
         * the subscription is where that statement finally has an effect.
         *
         * @par Which priority wins
         * Four statements can be in play about one message, least specific first: the target queue's
         * own default, this, the priority in the object's own system attributes, and a priority a
         * message already had when a topic passed it on. The object's beats the bucket's because it
         * is the narrower claim - which is what lets a bucket set a floor without taking away the
         * ability to say more about a particular object.
         *
         * @par Empty is not MEDIUM
         * An empty priority clears it, and is the only way back to letting the queue decide. A
         * bucket that says nothing leaves a queue created with LOW delivering at LOW, where a bucket
         * saying MEDIUM would override it.
         *
         * @param ern      the bucket, by name or by ERN.
         * @param priority "LOW", "MEDIUM" or "HIGH", or empty to clear it. Case is not significant;
         *                 anything else is refused rather than ignored.
         * @return the bucket and the priority it now carries.
         * @throws ServiceError if the bucket does not exist or the priority is not one of the three.
         */
        [[nodiscard]]
        SetBucketPriorityResult SetBucketPriority(const std::string &ern, const std::string &priority = {}) const;

        /**
         * @brief Whether the bucket keeps the previous object when a key is written again.
         *
         * @par
         * Off by default, because keeping every version costs disk for as long as the bucket exists.
         * Turning it on applies to what is written from here on; turning it off leaves what was
         * already kept, so a bucket switched off still has its versions and ListObjectVersions()
         * still reads them.
         *
         * @param ern     the bucket.
         * @param enabled whether to keep versions. Required rather than defaulted on the wire -
         * either answer is a change somebody may not have meant, and the two cost very different
         * amounts of disk.
         * @throws ServiceError 403 if the bucket is not the caller's account's, 404 if there is no
         * such bucket.
         */
        [[nodiscard]]
        BucketVersioningResult SetBucketVersioning(const std::string &ern, bool enabled) const;

        /**
         * @brief Every version of one key, the current object first.
         *
         * @par
         * A key whose current object was deleted still lists what was kept, which is what makes a
         * deletion recoverable - so an answer with no latest version is a deleted key rather than a
         * missing one.
         *
         * @param bucketErn the bucket, by ERN or bare name.
         * @param key       the object key.
         * @throws ServiceError 404 if the key has neither a current object nor any version.
         */
        [[nodiscard]]
        ObjectVersions ListObjectVersions(const std::string &bucketErn, const std::string &key) const;

        // -- lifecycle --------------------------------------------------------------------------

        /**
         * @brief Replaces a bucket's lifecycle rules.
         *
         * @par
         * Replaces rather than merges, and the whole set is validated before any of it is stored -
         * so a bucket never ends up with half of a set. Passing no rules is the same as
         * DeleteBucketLifecycle().
         *
         * @par
         * Rules decide what to archive, not what to bring back: objects already archived stay
         * archived when the rules change, and RestoreObject() is what reverses that.
         *
         * @param ern   the bucket.
         * @param rules the rules, each with an id unique within the set.
         * @throws ServiceError 400 if a rule is malformed or an id is given twice, 409 if the bucket
         * is one of euclid's own - those are read straight off the disk by the modules that use
         * them, which know nothing of an archive.
         */
        [[nodiscard]]
        BucketLifecycle SetBucketLifecycle(const std::string &ern, const std::vector<LifecycleRule> &rules) const;

        /**
         * @brief A bucket's lifecycle rules as they now stand; an empty set is a bucket with none.
         */
        [[nodiscard]]
        BucketLifecycle GetBucketLifecycle(const std::string &ern) const;

        /**
         * @brief Removes every lifecycle rule, leaving whatever is already archived archived.
         */
        [[nodiscard]]
        BucketLifecycle DeleteBucketLifecycle(const std::string &ern) const;

        /**
         * @brief Archives one object by hand, whatever the rules say.
         *
         * @par
         * What an operator does to something they know is not wanted online; it needs no rule to
         * cover it. The object stays listed and described, and its bytes move into an archive - so
         * a download is refused until it is restored.
         *
         * @par
         * Idempotent: an object already archived is answered as it is rather than refused.
         *
         * @param bucketErn the bucket.
         * @param key       the object key.
         * @throws ServiceError 409 if the object is not complete - only a finished object can be
         * archived - or if the bucket is one of euclid's own.
         */
        [[nodiscard]]
        ObjectLifecycle ArchiveObject(const std::string &bucketErn, const std::string &key) const;

        /**
         * @brief Brings an archived object back online for a while.
         *
         * @par
         * Temporary by default: ObjectLifecycle::restoredUntil is when it goes back, and a rule that
         * covers the key decides how long unless days says otherwise. That is worth reading rather
         * than assuming, because a restore that expires while something is still reading from it
         * looks like the object vanishing.
         *
         * @par
         * Idempotent, like archiving.
         *
         * @param bucketErn the bucket.
         * @param key       the object key.
         * @param days      how long to keep it online, up to MaxRestoreDays; 0 takes the covering
         * rule's own period.
         * @throws EuclidError if days is negative or beyond MaxRestoreDays.
         */
        [[nodiscard]]
        ObjectLifecycle RestoreObject(const std::string &bucketErn, const std::string &key, long days = 0) const;

        // -- bucket state -----------------------------------------------------------------------

        /**
         * @brief Holds the bucket's events, leaving its objects alone.
         *
         * @par
         * What stops is the notifications: every event published about the bucket from now on is
         * held, along with the deliveries to its subscribers that were still waiting. Reading and
         * writing objects carries on exactly as before - this is not a way to make a bucket
         * read-only.
         *
         * @par
         * Idempotent. BucketStateResult::held is how much is waiting.
         */
        [[nodiscard]]
        BucketStateResult StopBucket(const std::string &ern) const;

        /**
         * @brief Releases a stopped bucket's held events.
         *
         * @par
         * Everything held goes out, so a bucket stopped for an hour delivers an hour of events when
         * it starts - which is the point, but worth knowing before stopping a busy one.
         * BucketStateResult::released is how many went. Starting a bucket that was never stopped
         * releases nothing and is not an error.
         */
        [[nodiscard]]
        BucketStateResult StartBucket(const std::string &ern) const;

        // -- checking ---------------------------------------------------------------------------

        /**
         * @brief Checks a bucket's objects against the files behind them.
         *
         * @par
         * A stat per object, and a header read for an encrypted one - never the bytes, so this is
         * cheap enough to run on a large bucket. What it finds is a record whose file is missing or
         * whose size disagrees, which is the kind of damage nothing else notices until a download
         * fails.
         *
         * @par
         * One pass covers at most VerifyOptions::limit objects; VerifyResult::nextKey is where the
         * next one starts, and is empty at the end of the bucket. Objects still being uploaded,
         * archived ones and directory markers are counted as skipped rather than checked.
         *
         * @param bucketErn the bucket.
         * @param options   where to start, and how much to cover.
         * @throws EuclidError if the limit is outside 1..MaxVerifyLimit.
         */
        [[nodiscard]]
        VerifyResult VerifyObjects(const std::string &bucketErn, const VerifyOptions &options = {}) const;

        /**
         * @brief Deletes a bucket's objects, leaving the bucket itself in place.
         *
         * @par
         * "background" is what a bucket of any size wants: emptying one can take minutes, and
         * holding a request open for all of it is a request that times out while the removal
         * carries on invisibly behind it. The server then answers as soon as it has written the
         * work down, and the result's "count" is what the bucket held rather than what has gone.
         *
         * @par
         * Written down is the point: the job survives the instance that took it on being stopped -
         * which the autoscaler does to an instance it sees no requests on - and another picks it up
         * and carries on from where it got to.
         *
         * @param ern    the bucket.
         * @param prefix narrows it to the keys that start with this; an empty one purges everything.
         * @param background whether the server answers before it has finished removing the objects.
         */
        [[nodiscard]]
        PurgeBucketResult PurgeBucket(const std::string &ern, const std::string &prefix = {},
                                      bool background = false) const;

        /**
         * @brief Encrypts every object written to this bucket from now on, under an EKM key.
         *
         * @par
         * What it does not do is touch the objects already there: their bytes stay as they were
         * stored, each one records the key it is under, and the result says how many such objects
         * there are. Re-encrypting them is a decision for whoever owns the data.
         *
         * @param bucketErn the bucket.
         * @param keyId     an EKM key that exists and can encrypt, or empty for one created here as
         * AES-256. A created key belongs to EKM from that moment on - which means deleting it there
         * is what makes this bucket's objects unrecoverable.
         */
        [[nodiscard]]
        EnableEncryptionResult EnableEncryption(const std::string &bucketErn, const std::string &keyId = {}) const;

        /**
         * @brief Stops encrypting new objects written to a bucket.
         *
         * @par
         * The mirror image of EnableEncryption() in one respect and no other: it says what happens
         * to the next upload, and it is not an undo. Nothing already stored is decrypted or
         * rewritten, and the key is left alone rather than revoked - those objects are still under
         * it.
         */
        [[nodiscard]]
        DisableEncryptionResult DisableEncryption(const std::string &bucketErn) const;

        /**
         * @brief Tags a bucket. A key that is already tagged keeps its value - SetBucketTag()
         * overwrites.
         */
        void AddBucketTag(const std::string &bucketErn, const std::string &key, const std::string &value) const;

        /**
         * @brief Tags a bucket, overwriting any value the key already had.
         */
        void SetBucketTag(const std::string &bucketErn, const std::string &key, const std::string &value) const;

        /**
         * @brief Removes a tag from a bucket.
         */
        void DeleteBucketTag(const std::string &bucketErn, const std::string &key) const;

        // -- objects ----------------------------------------------------------------------------

        /**
         * @brief One page of a bucket's objects, and how many it holds in total.
         *
         * @param bucketErn          the bucket.
         * @param options            paging, ordering and prefix.
         * @param includeDirectories whether the directory markers are in it. Keys are opaque
         * strings, so a bucket only has "directories" in the sense that keys share a prefix.
         */
        [[nodiscard]]
        Page<Object> ListObjects(const std::string &bucketErn, const ListOptions &options = {}, bool includeDirectories = false) const;

        /**
         * @brief The bucket's stored object count, without counting.
         *
         * @par
         * That figure is a running total, moved as objects are written and removed rather than
         * counted on demand, so this costs one document read whatever the bucket holds. It is
         * always the whole bucket, and only as current as the last time euclid's monitoring module
         * recomputed it. Use CountObjects() when the answer has to be exact, or has to be about
         * part of a bucket.
         *
         * @par
         * This took a prefix until euclid 1.0.73 and the server ignored it, answering the whole
         * bucket's figure regardless. The parameter is gone rather than fixed, because the stored
         * total is a property of the bucket and there is no per-prefix one to read.
         */
        [[nodiscard]]
        long GetObjectCount(const std::string &bucketErn) const;

        /**
         * @brief Counts a bucket's objects, exactly, optionally under a prefix.
         *
         * @par
         * This runs a query, so the figure is right at the moment of asking and costs what counting
         * a bucket's objects costs - on a bucket of a million, not nothing. GetObjectCount() reads
         * the stored running total instead. Ask this one when the answer has to be right or has to
         * be about part of a bucket, and that one when it has to be cheap or is being polled.
         *
         * @param bucketErn          the bucket to count in.
         * @param prefix             only objects whose key starts with this; matched literally
         *                           rather than as a glob, since a bucket has "directories" only in
         *                           the sense that keys share a prefix. Empty counts the whole
         *                           bucket.
         * @param includeDirectories count the zero-byte markers that stand for directories too,
         *                           which a listing and the bucket's own stored figure both leave
         *                           out.
         */
        [[nodiscard]]
        long CountObjects(const std::string &bucketErn, const std::string &prefix = {},
                          bool includeDirectories = false) const;

        /**
         * @brief Deletes one object, by its own ERN.
         */
        void DeleteObject(const std::string &ern) const;

        /**
         * @brief Deletes several named objects from a bucket in one call.
         *
         * @par
         * A key that names no object is not an error, so the result reports both how many keys were
         * asked for and how many objects went. Deleting everything under a prefix is PurgeBucket()
         * rather than a variant of this - the server refuses keys and a prefix in the same request,
         * since answering both would delete more than either.
         *
         * @param bucketErn  the bucket.
         * @param keys       the keys to delete.
         * @param background has the server answer as soon as it has taken the work on rather than
         * when it has finished, in which case the count is what it took on.
         */
        [[nodiscard]]
        DeleteObjectsResult DeleteObjects(const std::string &bucketErn, const std::vector<std::string> &keys, bool background = false) const;

        /**
         * @brief Copies an object, leaving the source in place.
         *
         * @par
         * The copy gets its own bytes on disk and its own ERN, so the two are independent from here
         * on. Both ends are permission-checked, and an existing object at the target is refused with
         * HTTP 409 rather than silently replaced.
         */
        [[nodiscard]]
        Object CopyObject(const std::string &sourceBucketErn, const std::string &sourceKey, const std::string &targetBucketErn, const std::string &targetKey) const;

        /**
         * @brief Moves an object to another bucket or key, removing the source.
         *
         * @par
         * The bytes are not copied - the same file answers to a different key from now on - so this
         * costs the same whatever the object's size. Refuses an existing target exactly as
         * CopyObject() does.
         */
        [[nodiscard]]
        Object MoveObject(const std::string &sourceBucketErn, const std::string &sourceKey, const std::string &targetBucketErn, const std::string &targetKey) const;

        /**
         * @brief Renames an object within its bucket - a MoveObject() that cannot leave it, which is
         * the whole difference between the two.
         */
        [[nodiscard]]
        Object RenameObject(const std::string &bucketErn, const std::string &key, const std::string &newKey) const;

        /**
         * @brief Re-announces objects already in a bucket, so a listener that missed their creation
         * events hears about them now.
         *
         * @par
         * Nothing about the objects changes - not a byte, not their modified time. "Touch" here
         * means what it does to listeners, not what it does to storage: a timestamp is something
         * consumers compare against, and moving it would make this destructive in exactly the way it
         * is trying not to be.
         *
         * @par
         * options.background is what a bucket of any size wants: the announcement is per object, and
         * holding a request open for all of them is a request that times out.
         */
        [[nodiscard]]
        TouchObjectResult TouchObject(const std::string &bucketErn, const TouchObjectOptions &options = {}) const;

        // -- object attributes ------------------------------------------------------------------

        /**
         * @brief Adds a user-defined attribute to an object. One of that name already there keeps
         * its value - SetObjectAttribute() overwrites.
         */
        [[nodiscard]]
        ObjectAttribute AddObjectAttribute(const std::string &ern, const std::string &name, const COM::Variant &value) const;

        /**
         * @brief Sets a user-defined attribute on an object, overwriting any value it already had.
         */
        [[nodiscard]]
        ObjectAttribute SetObjectAttribute(const std::string &ern, const std::string &name, const COM::Variant &value) const;

        /**
         * @brief Every user-defined attribute of an object, keyed by name.
         */
        [[nodiscard]]
        COM::VariantMap ListObjectAttributes(const std::string &ern) const;

        /**
         * @brief Deletes one user-defined attribute from an object.
         */
        void DeleteObjectAttribute(const std::string &ern, const std::string &name) const;

        // -- subscriptions ----------------------------------------------------------------------

        /**
         * @brief Announces a bucket's object events to a queue or a topic from now on.
         *
         * @par
         * What lands there is a BucketEvent, carried as the body of an ordinary message - see
         * ParseBucketEvent(). The filters are applied by the server as it publishes, so a
         * subscription only ever delivers what it asked for rather than the target receiving
         * everything and discarding most of it.
         *
         * @par
         * Not idempotent: a second call registers a second subscription and the target then receives
         * every matching event twice, so a caller that may run twice checks ListSubscriptions()
         * first.
         *
         * @param bucketErn  the bucket whose events these are.
         * @param targetType COM::Queue or COM::Topic, which is also what decides how a bare target
         * name is resolved.
         * @param targetErn  the queue or topic to deliver to.
         * @param options    which events, which keys, and whether directory markers count.
         */
        [[nodiscard]]
        COM::SubscribeResult Subscribe(const std::string &bucketErn, const std::string &targetType, const std::string &targetErn, const SubscribeOptions &options = {}) const;

        /**
         * @brief Removes a subscription, by the ERN Subscribe() answered with - not the bucket's,
         * and not the target's.
         */
        void Unsubscribe(const std::string &ern) const;

        /**
         * @brief Every subscription currently registered on a bucket.
         */
        [[nodiscard]]
        std::vector<COM::Subscription> ListSubscriptions(const std::string &bucketErn) const;

        // -- objects, in bytes ------------------------------------------------------------------

        /**
         * @brief Uploads an object in a single request, skipping the multipart sequence entirely.
         *
         * @param bucketErn the bucket.
         * @param key       the key it is stored under.
         * @param data      the object's bytes, verbatim - what a caller passes is what is stored.
         * @param options   the two attribute maps.
         */
        [[nodiscard]]
        StoredObject PutObject(const std::string &bucketErn, const std::string &key, std::string_view data, const AttributeOptions &options = {}) const;

        /**
         * @brief Downloads an object's bytes in a single request.
         *
         * @par
         * The size limit is the server's to enforce rather than this client's: a download's size is
         * not known until the server is asked, unlike an upload's, so the caller declares how large
         * a response it is willing to take and an object at or above that comes back as HTTP 413.
         * DownloadFile() uses exactly that to decide whether an object needs the multipart path.
         *
         * @param bucketErn     the bucket.
         * @param key           the object's key.
         * @param maxInlineSize the largest response this caller will take.
         * @return the object's bytes.
         * @throws ServiceError if the server refused, HTTP 413 for an object too large included.
         */
        [[nodiscard]]
        std::string GetObject(const std::string &bucketErn, const std::string &key, long maxInlineSize = DefaultPartSize) const;

        /**
         * @brief Uploads a local file in parts, several at a time.
         *
         * @par
         * The file is read a part at a time rather than into memory, and no more than
         * options.concurrency parts are ever in flight, so the memory this costs is bounded by the
         * two together whatever the file's size. An empty file is one empty part, so that the object
         * exists.
         *
         * @par
         * Attributes belong on the upload rather than added afterwards: completing an upload is
         * finished off in the background, and the object row written at the end carries what this
         * call supplied - an attribute added between here and there is overwritten and silently
         * lost.
         *
         * @param bucketErn the bucket.
         * @param key       the key the object is stored under.
         * @param file      the local file to read.
         * @param options   part size, concurrency and the two attribute maps.
         * @return the object as it was stored.
         * @throws ServiceError if a part or one of the calls bracketing them failed for good.
         * @throws EuclidError if the file cannot be read, or if partSize is less than a byte.
         */
        [[nodiscard]]
        StoredObject UploadFile(const std::string &bucketErn, const std::string &key, const std::string &file, const UploadOptions &options = {}) const;

        /**
         * @brief Downloads an object to a local file, fetching its parts several at a time.
         *
         * @par
         * An object that fits in one part skips multipart entirely. Unlike an upload - whose source
         * this client has already measured - a download's size is not known before asking, so the
         * single-request path is tried first and HTTP 413 is what says the object was too large for
         * it.
         *
         * @param bucketErn the bucket.
         * @param key       the object's key.
         * @param file      the local file to write; missing parent directories are created.
         * @param options   part size and concurrency.
         * @return the number of bytes written.
         * @throws ServiceError if the object is not there, or a part failed for good.
         * @throws EuclidError if the file cannot be written, or if partSize is less than a byte.
         */
        long DownloadFile(const std::string &bucketErn, const std::string &key, const std::string &file, const DownloadOptions &options = {}) const;

        /**
         * @brief Uploads bytes already in memory in parts, several at a time - UploadFile() for a
         * caller that has no file.
         *
         * @par
         * Below one part the bytes go up in a single put-object, as UploadFile() does. Above it they
         * are cut into parts, which keeps every request under the body limits between here and ESM
         * however large the object is.
         *
         * @param bucketErn the bucket.
         * @param key       the key the object is stored under.
         * @param data      the object's bytes, verbatim.
         * @param options   part size, concurrency and the two attribute maps.
         * @return the object as it was stored.
         * @throws ServiceError if a part or one of the calls bracketing them failed for good.
         * @throws EuclidError if partSize is less than a byte.
         */
        [[nodiscard]]
        StoredObject UploadObject(const std::string &bucketErn, const std::string &key, std::string_view data, const UploadOptions &options = {}) const;

        /**
         * @brief Downloads an object into memory, fetching its parts several at a time -
         * DownloadFile() for a caller that wants the bytes rather than a file.
         *
         * @par
         * Unlike GetObject(), the size of the object is not bounded by what one response may carry:
         * an object larger than a part is fetched in parts. What bounds it is maxSize, checked
         * against the size create-download reports before a single part is fetched, so an object
         * too large to hold costs one round trip rather than the memory.
         *
         * @param bucketErn the bucket.
         * @param key       the object's key.
         * @param maxSize   the largest object accepted, in bytes; 0 for no limit.
         * @param options   part size and concurrency.
         * @return the object's bytes.
         * @throws ServiceError if the object is not there, a part failed for good, or - HTTP 413 -
         * the object is larger than maxSize.
         * @throws EuclidError if partSize is less than a byte.
         */
        [[nodiscard]]
        std::string DownloadObject(const std::string &bucketErn, const std::string &key, long maxSize = 0, const DownloadOptions &options = {}) const;

        /**
         * @brief Throws away a multipart upload that will not be finished.
         *
         * @par
         * Discards the parts staged under uploadId and - for a first upload - the object row that
         * was seeded for bytes which never arrived. A re-upload's object row is left exactly as it
         * is: that row is the previous version of the object, still published and still readable,
         * and not this upload's to delete. AbortUploadResult::objectRemoved says which happened.
         *
         * @par
         * UploadFile() does not need this - it completes or it fails within one call. What needs
         * it is an upload nothing is driving any more: one whose client was killed, or one the API
         * gateway abandoned. The id comes from a log or from whatever started the upload, which is
         * why this takes one rather than being folded into the multipart helpers.
         *
         * @param uploadId the upload to discard, as create-upload returned it.
         * @return what was discarded, and whether the object row went with it.
         * @throws ServiceError if there is no such upload - which is also what a completed one
         * answers, since completing it takes the staging with it.
         */
        [[nodiscard]]
        AbortUploadResult AbortUpload(const std::string &uploadId) const;

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief ESM's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to ESM.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

    private:

        /**
         * @brief copy-object and move-object take the same request and differ only in whether the
         * source survives.
         */
        [[nodiscard]]
        Object TransferObject(const std::string &action, const std::string &sourceBucketErn, const std::string &sourceKey, const std::string &targetBucketErn, const std::string &targetKey) const;

        /**
         * @brief add-object-attribute and set-object-attribute, which differ only in what they do to
         * an attribute that is already there.
         */
        [[nodiscard]]
        ObjectAttribute ObjectAttributeOf(const std::string &action, const std::string &ern, const std::string &name, const COM::Variant &value) const;

        // -- the multipart sequence -------------------------------------------------------------

        /**
         * @brief Opens a multipart upload, declaring the concurrency it is about to use so that the
         * gateway's autoscaler can ramp storage instances toward it rather than discover the load.
         *
         * @par
         * Retried on 5xx: the object row the server seeds is keyed on the bucket and key, so a
         * second attempt updates the same row, and the only cost of a repeat is the scratch
         * directory the abandoned upload ID left behind.
         */
        [[nodiscard]]
        CreateUploadResult CreateUpload(const std::string &bucketErn, const std::string &key, int concurrency) const;

        /**
         * @brief Sends one part of an upload.
         */
        void UploadPart(const std::string &uploadId, long number, const std::string &data) const;

        /**
         * @brief Assembles the parts into the object.
         *
         * @par
         * The attributes ride on this request because the background pass that finishes the upload
         * builds the object row from what this call was given. Retried on 5xx like the create:
         * failing here discards every part already uploaded, and an upload the server did accept
         * fails a retry with 404 rather than being assembled twice.
         */
        [[nodiscard]]
        StoredObject CompleteUpload(const std::string &uploadId, const UploadOptions &options) const;

        /**
         * @brief Opens a multipart download, which stages the object and says how large it is.
         *
         * @par
         * Retried on 5xx: the session it opens is scratch state keyed by a fresh download ID, so a
         * retried attempt starts a new one and the abandoned session is simply never used.
         */
        [[nodiscard]]
        CreateDownloadResult CreateDownload(const std::string &bucketErn, const std::string &key, int concurrency) const;

        /**
         * @brief Fetches one part of a download.
         */
        [[nodiscard]]
        std::string DownloadPart(const std::string &downloadId, long number, long partSize) const;

        /**
         * @brief Releases the download's server-side scratch state. Retried on 5xx for the same
         * reason completing an upload is: failing here throws away every part already fetched.
         */
        void CompleteDownload(const std::string &downloadId) const;

        /**
         * @brief The raw response, so a caller can tell an object that was too large from one that
         * failed.
         */
        [[nodiscard]]
        HttpResponse GetObjectResponse(const std::string &bucketErn, const std::string &key, long maxInlineSize) const;

        // -- transport ---------------------------------------------------------------------------

        /**
         * @brief One of the JSON actions bracketing a transfer, retried the way the parts between
         * them are.
         *
         * @par
         * They run once per transfer rather than once per part, but giving up on a transient failure
         * in one of them discards the whole file, which is what makes them worth the same treatment.
         */
        [[nodiscard]]
        boost::json::object CallWithRetry(const std::string &action, const boost::json::object &payload, const Headers &headers = {}) const;

        /**
         * @brief Sends one step of a transfer, retrying while it looks transient.
         *
         * @par
         * A 4xx means the request itself is wrong and a repeat would be answered identically, so
         * only a 5xx and a request that never got an answer are tried again. An exception here is
         * the transport failing rather than the server refusing - a refusal arrives as a status.
         */
        [[nodiscard]]
        HttpResponse WithRetry(const std::string &action, const std::function<HttpResponse()> &send) const;

        /**
         * @brief The two attribute maps, as the headers that carry them.
         *
         * @par
         * Headers rather than body fields because the actions that take them are the ones whose body
         * is either the object's bytes or nothing at all. An empty map is left out entirely, so a
         * request that has nothing to say about attributes says nothing.
         */
        [[nodiscard]]
        static Headers AttributeHeaders(const COM::VariantMap &attributes, const COM::VariantMap &systemAttributes);

        std::chrono::milliseconds _transferTimeout{std::chrono::milliseconds::zero()};
        std::chrono::milliseconds _retryBaseDelay{PartRetryBaseDelay};
    };

}// namespace Euclid::CDK::ESM
