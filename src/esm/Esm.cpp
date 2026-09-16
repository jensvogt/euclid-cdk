// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/esm/Esm.h>

namespace Euclid::CDK::ESM {

    namespace {

        /**
         * @brief The actions that carry raw bytes rather than JSON - see the class documentation.
         */
        const std::vector<std::string> kByteActions{"put-object", "get-object", "upload-part", "download-part"};

        /**
         * @brief Refused here rather than on arrival: a part size of zero would otherwise upload a
         * file as one empty part and call it stored, which is a corrupt object rather than an error.
         */
        void checkPartSize(const long partSize) {
            if (partSize < 1) throw EuclidError("partSize must be at least 1 byte");
        }

        /**
         * @brief A JSON array of strings, for the payload fields that take a list.
         */
        boost::json::array stringsOf(const std::vector<std::string> &values) {
            boost::json::array array;
            for (const auto &value: values) array.emplace_back(value);
            return array;
        }

        /**
         * @brief One part of a file, read at its own offset.
         *
         * @par
         * Its own handle rather than one shared between the parts: each reads its own
         * non-overlapping range, so sharing a file position would only mean contending for it.
         */
        std::string readPart(const std::string &file, const long offset, const long length) {

            std::ifstream in(file, std::ios::binary);
            if (!in.is_open()) throw EuclidError("could not open " + file + " to read");

            std::string part(static_cast<std::size_t>(length), '\0');
            if (length > 0) {
                in.seekg(offset);
                in.read(part.data(), static_cast<std::streamsize>(length));
                // A short read is what it is rather than an error: the file was measured before the
                // transfer started, and a part that came up short is caught by the server assembling
                // an object of the wrong size rather than silently here.
                part.resize(static_cast<std::size_t>(in.gcount()));
            }
            return part;
        }

        /**
         * @brief One part of a download, written at its own offset into a file already sized for it.
         */
        void writePart(const std::string &file, const long offset, const std::string &data) {

            std::fstream out(file, std::ios::binary | std::ios::in | std::ios::out);
            if (!out.is_open()) throw EuclidError("could not open " + file + " to write");

            out.seekp(offset);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!out) throw EuclidError("could not write " + std::to_string(data.size()) + " bytes to " + file);
        }

        /**
         * @brief Makes sure the file exists and is exactly this long, so that each part can be
         * written at its own offset whatever order the parts arrive in.
         */
        void createFileOfSize(const std::string &file, const long size) {

            if (const auto parent = std::filesystem::path(file).parent_path(); !parent.empty()) {
                std::error_code ec;
                std::filesystem::create_directories(parent, ec);
            }

            if (const std::ofstream create(file, std::ios::binary | std::ios::trunc); !create.is_open()) {
                throw EuclidError("could not create " + file);
            }

            std::error_code ec;
            std::filesystem::resize_file(file, static_cast<std::uintmax_t>(size), ec);
            if (ec) throw EuclidError("could not size " + file + " to " + std::to_string(size) + " bytes: " + ec.message());
        }

        /**
         * @brief Runs task for every index below count, no more than concurrency of them at a time.
         *
         * @par
         * Workers take the next index as they finish one rather than the work being handed out in
         * advance, so a part that takes longer than the rest does not leave a worker idle - and the
         * memory in flight is bounded by the concurrency rather than by the number of parts.
         *
         * @par
         * The first failure is what the caller sees, thrown once every worker has finished: a part
         * still in flight when another one failed is one the server is already writing, and
         * abandoning it would not stop that.
         */
        void runBounded(const long count, const int concurrency, const std::function<void(long)> &task) {

            if (count <= 0) return;

            std::atomic<long> next{0};
            std::atomic<bool> failed{false};
            std::mutex mutex;
            std::exception_ptr failure;

            const auto worker = [&] {
                while (!failed.load()) {
                    const long index = next.fetch_add(1);
                    if (index >= count) return;
                    try {
                        task(index);
                    } catch (...) {
                        const std::lock_guard lock(mutex);
                        if (!failure) failure = std::current_exception();
                        failed.store(true);
                        return;
                    }
                }
            };

            std::vector<std::thread> threads;
            const auto workers = static_cast<std::size_t>(std::min<long>(std::max(1, concurrency), count));
            threads.reserve(workers);
            for (std::size_t i = 0; i < workers; ++i) threads.emplace_back(worker);
            for (auto &thread: threads) thread.join();

            if (failure) std::rethrow_exception(failure);
        }

        /**
         * @brief How many parts a body of this size is cut into.
         */
        long partCount(const long size, const long partSize) {
            return (size + partSize - 1) / partSize;
        }

    }// namespace

    BucketEvent ParseBucketEvent(const std::string &messageBody) {
        return ToBucketEvent(Json::Parse(messageBody));
    }

    Esm::Esm(const EAM::Session &session) : ModuleClient(session, std::string(ESM::Target), kByteActions) {}

    void Esm::SetTransferTimeout(const std::chrono::milliseconds timeout) { _transferTimeout = timeout; }

    std::chrono::milliseconds Esm::TransferTimeout() const { return _transferTimeout; }

    void Esm::SetRetryBaseDelay(const std::chrono::milliseconds delay) { _retryBaseDelay = delay; }

    // -- buckets ------------------------------------------------------------------------------------

    CreateBucketResult Esm::CreateBucket(const std::string &name, const bool internal) const {
        return ToCreateBucketResult(Call("create-bucket", {{"name", name}, {"internal", internal}}));
    }

    DeleteBucketResult Esm::DeleteBucket(const std::string &ern, const bool background) const {
        return ToDeleteBucketResult(Call("delete-bucket", {{"ern", ern}, {"async", background}}));
    }

    Page<Bucket> Esm::ListBuckets(const ListOptions &options, const bool includeInternal) const {
        auto payload = ListPayload(options, "name");
        payload["includeInternal"] = includeInternal;
        return ToPage<Bucket>(Call("list-buckets", payload), "buckets", ToBucket);
    }

    std::string Esm::GetBucketErn(const std::string &name) const {
        return TextOf("get-bucket-ern", {{"name", name}}, "ern");
    }

    long Esm::GetBucketSize(const std::string &ern) const {
        return NumberOf("get-bucket-size", {{"ern", ern}}, "size");
    }

    RenameBucketResult Esm::RenameBucket(const std::string &ern, const std::string &newName) const {
        return ToRenameBucketResult(Call("rename-bucket", {{"ern", ern}, {"newName", newName}}));
    }

    SetBucketInternalResult Esm::SetBucketInternal(const std::string &ern, const bool internal) const {
        return ToSetBucketInternalResult(Call("set-bucket-internal", {{"ern", ern}, {"internal", internal}}));
    }

    PurgeBucketResult Esm::PurgeBucket(const std::string &ern, const std::string &prefix, const bool background) const {
        return ToPurgeBucketResult(Call("purge-bucket", {{"ern", ern}, {"prefix", prefix}, {"async", background}}));
    }

    EnableEncryptionResult Esm::EnableEncryption(const std::string &bucketErn, const std::string &keyId) const {
        return ToEnableEncryptionResult(Call("enable-encryption", {{"bucketErn", bucketErn}, {"keyId", keyId}}));
    }

    DisableEncryptionResult Esm::DisableEncryption(const std::string &bucketErn) const {
        return ToDisableEncryptionResult(Call("disable-encryption", {{"bucketErn", bucketErn}}));
    }

    void Esm::AddBucketTag(const std::string &bucketErn, const std::string &key, const std::string &value) const {
        std::ignore = Call("add-bucket-tag", {{"ern", bucketErn}, {"key", key}, {"value", value}});
    }

    void Esm::SetBucketTag(const std::string &bucketErn, const std::string &key, const std::string &value) const {
        std::ignore = Call("set-bucket-tag", {{"ern", bucketErn}, {"key", key}, {"value", value}});
    }

    void Esm::DeleteBucketTag(const std::string &bucketErn, const std::string &key) const {
        std::ignore = Call("delete-bucket-tag", {{"ern", bucketErn}, {"key", key}});
    }

    // -- objects ------------------------------------------------------------------------------------

    Page<Object> Esm::ListObjects(const std::string &bucketErn, const ListOptions &options, const bool includeDirectories) const {
        auto payload = ListPayload(options, "name");
        payload["bucketErn"] = bucketErn;
        payload["includeDirectories"] = includeDirectories;
        return ToPage<Object>(Call("list-objects", payload), "objects", ToObject);
    }

    long Esm::GetObjectCount(const std::string &bucketErn) const {
        return NumberOf("get-object-count", {{"ern", bucketErn}}, "count");
    }

    long Esm::CountObjects(const std::string &bucketErn, const std::string &prefix, const bool includeDirectories) const {
        return NumberOf("count-objects",
                        {{"ern", bucketErn}, {"prefix", prefix}, {"includeDirectories", includeDirectories}},
                        "count");
    }

    void Esm::DeleteObject(const std::string &ern) const {
        std::ignore = Call("delete-object", {{"ern", ern}});
    }

    DeleteObjectsResult Esm::DeleteObjects(const std::string &bucketErn, const std::vector<std::string> &keys, const bool background) const {
        const boost::json::object payload{{"ern", bucketErn}, {"keys", stringsOf(keys)}, {"async", background}};
        return ToDeleteObjectsResult(Call("delete-objects", payload));
    }

    Object Esm::CopyObject(const std::string &sourceBucketErn, const std::string &sourceKey, const std::string &targetBucketErn, const std::string &targetKey) const {
        return TransferObject("copy-object", sourceBucketErn, sourceKey, targetBucketErn, targetKey);
    }

    Object Esm::MoveObject(const std::string &sourceBucketErn, const std::string &sourceKey, const std::string &targetBucketErn, const std::string &targetKey) const {
        return TransferObject("move-object", sourceBucketErn, sourceKey, targetBucketErn, targetKey);
    }

    Object Esm::RenameObject(const std::string &bucketErn, const std::string &key, const std::string &newKey) const {
        return ToObject(Call("rename-object", {{"bucketErn", bucketErn}, {"key", key}, {"newKey", newKey}}));
    }

    TouchObjectResult Esm::TouchObject(const std::string &bucketErn, const TouchObjectOptions &options) const {
        const boost::json::object payload{{"ern", bucketErn}, {"prefix", options.prefix}, {"async", options.background}};
        return ToTouchObjectResult(Call("touch-object", payload));
    }

    Object Esm::TransferObject(const std::string &action, const std::string &sourceBucketErn, const std::string &sourceKey, const std::string &targetBucketErn, const std::string &targetKey) const {
        const boost::json::object payload{
                {"sourceBucketErn", sourceBucketErn},
                {"sourceKey", sourceKey},
                {"targetBucketErn", targetBucketErn},
                {"targetKey", targetKey},
        };
        return ToObject(Call(action, payload));
    }

    // -- object attributes --------------------------------------------------------------------------

    ObjectAttribute Esm::AddObjectAttribute(const std::string &ern, const std::string &name, const COM::Variant &value) const {
        return ObjectAttributeOf("add-object-attribute", ern, name, value);
    }

    ObjectAttribute Esm::SetObjectAttribute(const std::string &ern, const std::string &name, const COM::Variant &value) const {
        return ObjectAttributeOf("set-object-attribute", ern, name, value);
    }

    COM::VariantMap Esm::ListObjectAttributes(const std::string &ern) const {
        return COM::ToVariantMap(Call("list-object-attributes", {{"ern", ern}}), "attributes");
    }

    void Esm::DeleteObjectAttribute(const std::string &ern, const std::string &name) const {
        std::ignore = Call("delete-object-attribute", {{"ern", ern}, {"name", name}});
    }

    ObjectAttribute Esm::ObjectAttributeOf(const std::string &action, const std::string &ern, const std::string &name, const COM::Variant &value) const {
        const boost::json::object payload{{"ern", ern}, {"name", name}, {"value", value.ToJson()}};
        return ToObjectAttribute(Call(action, payload));
    }

    // -- subscriptions ------------------------------------------------------------------------------

    COM::SubscribeResult Esm::Subscribe(const std::string &bucketErn, const std::string &targetType, const std::string &targetErn, const SubscribeOptions &options) const {
        const boost::json::object payload{
                {"sourceErn", bucketErn},
                {"type", targetType},
                {"targetErn", targetErn},
                {"eventTypes", stringsOf(options.eventTypes)},
                {"prefix", options.prefix},
                {"directories", options.directories},
        };
        return COM::ToSubscribeResult(Call("subscribe", payload));
    }

    void Esm::Unsubscribe(const std::string &ern) const {
        std::ignore = Call("unsubscribe", {{"ern", ern}});
    }

    std::vector<COM::Subscription> Esm::ListSubscriptions(const std::string &bucketErn) const {
        std::vector<COM::Subscription> subscriptions;
        for (const auto &document: Json::Documents(Call("list-subscriptions", {{"bucketErn", bucketErn}}), "subscriptions")) {
            subscriptions.push_back(COM::ToSubscription(document));
        }
        return subscriptions;
    }

    // -- objects, in bytes --------------------------------------------------------------------------

    StoredObject Esm::PutObject(const std::string &bucketErn, const std::string &key, const std::string_view data, const AttributeOptions &options) const {

        Headers headers{{"x-euclid-bucket-ern", bucketErn}, {"x-euclid-key", key}};
        headers.merge(AttributeHeaders(options.attributes, options.systemAttributes));

        // Answers with JSON even though the request carried bytes: the same payload complete-upload
        // answers with, so a caller that needs the ERN does not have to take the multipart path.
        const auto response = PostBytes("put-object", std::string(data), headers, _transferTimeout);
        return ToStoredObject(Result("put-object", response));
    }

    std::string Esm::GetObject(const std::string &bucketErn, const std::string &key, const long maxInlineSize) const {

        const auto response = GetObjectResponse(bucketErn, key, maxInlineSize);
        if (!response.IsSuccess()) throw ServiceError(Target(), "get-object", response.statusCode, response.body);
        return response.body;
    }

    StoredObject Esm::UploadFile(const std::string &bucketErn, const std::string &key, const std::string &file, const UploadOptions &options) const {

        checkPartSize(options.partSize);

        // Measured before anything is sent, so a path that is not there costs no round trip at all.
        std::error_code ec;
        const auto size = static_cast<long>(std::filesystem::file_size(file, ec));
        if (ec) throw EuclidError("could not read " + file + ": " + ec.message());

        // Normalised once: what the parts are actually sent with is also what create-upload declares
        // to the autoscaler, rather than the two disagreeing about a nonsensical value.
        const auto concurrency = std::max(1, options.concurrency);
        const auto upload = CreateUpload(bucketErn, key, concurrency);

        // An empty file is one empty part rather than none, so that the object exists afterwards.
        const auto parts = std::max(1L, partCount(size, options.partSize));
        runBounded(parts, concurrency, [&](const long index) {
            const auto offset = index * options.partSize;
            UploadPart(upload.uploadId, index + 1, readPart(file, offset, std::min(options.partSize, std::max(0L, size - offset))));
        });

        return CompleteUpload(upload.uploadId, options);
    }

    long Esm::DownloadFile(const std::string &bucketErn, const std::string &key, const std::string &file, const DownloadOptions &options) const {

        checkPartSize(options.partSize);

        // Tried in one request first: an object that fits in one part skips multipart entirely, and
        // "too large" is the server's way of saying this one does not.
        if (const auto single = GetObjectResponse(bucketErn, key, options.partSize); single.statusCode != PayloadTooLarge) {
            if (!single.IsSuccess()) throw ServiceError(Target(), "get-object", single.statusCode, single.body);
            const auto size = static_cast<long>(single.body.size());
            createFileOfSize(file, size);
            writePart(file, 0, single.body);
            return size;
        }

        const auto concurrency = std::max(1, options.concurrency);
        const auto download = CreateDownload(bucketErn, key, concurrency);
        createFileOfSize(file, download.size);

        runBounded(partCount(download.size, options.partSize), concurrency, [&](const long index) {
            if (const auto part = DownloadPart(download.downloadId, index + 1, options.partSize); !part.empty()) {
                writePart(file, index * options.partSize, part);
            }
        });

        CompleteDownload(download.downloadId);
        return download.size;
    }

    // -- monitoring ---------------------------------------------------------------------------------

    boost::json::object Esm::Metrics() const {
        return Call("get-metrics");
    }

    // -- the multipart sequence ---------------------------------------------------------------------

    CreateUploadResult Esm::CreateUpload(const std::string &bucketErn, const std::string &key, const int concurrency) const {
        const Headers headers{{"x-euclid-expected-concurrency", std::to_string(concurrency)}};
        return ToCreateUploadResult(CallWithRetry("create-upload", {{"bucketErn", bucketErn}, {"key", key}}, headers));
    }

    void Esm::UploadPart(const std::string &uploadId, const long number, const std::string &data) const {
        const Headers headers{{"x-euclid-upload-id", uploadId}, {"x-euclid-part-number", std::to_string(number)}};
        std::ignore = WithRetry("upload-part", [&] { return PostBytes("upload-part", data, headers, _transferTimeout); });
    }

    StoredObject Esm::CompleteUpload(const std::string &uploadId, const UploadOptions &options) const {
        const auto headers = AttributeHeaders(options.attributes, options.systemAttributes);
        return ToStoredObject(CallWithRetry("complete-upload", {{"uploadId", uploadId}}, headers));
    }

    CreateDownloadResult Esm::CreateDownload(const std::string &bucketErn, const std::string &key, const int concurrency) const {
        const Headers headers{{"x-euclid-expected-concurrency", std::to_string(concurrency)}};
        return ToCreateDownloadResult(CallWithRetry("create-download", {{"bucketErn", bucketErn}, {"key", key}}, headers));
    }

    std::string Esm::DownloadPart(const std::string &downloadId, const long number, const long partSize) const {
        const Headers headers{
                {"x-euclid-download-id", downloadId},
                {"x-euclid-part-number", std::to_string(number)},
                {"x-euclid-part-size", std::to_string(partSize)},
        };
        return WithRetry("download-part", [&] { return PostBytes("download-part", {}, headers, _transferTimeout); }).body;
    }

    void Esm::CompleteDownload(const std::string &downloadId) const {
        std::ignore = CallWithRetry("complete-download", {{"downloadId", downloadId}});
    }

    HttpResponse Esm::GetObjectResponse(const std::string &bucketErn, const std::string &key, const long maxInlineSize) const {
        const Headers headers{
                {"x-euclid-bucket-ern", bucketErn},
                {"x-euclid-key", key},
                {"x-euclid-part-size", std::to_string(maxInlineSize)},
        };
        return PostBytes("get-object", {}, headers, _transferTimeout);
    }

    // -- transport ----------------------------------------------------------------------------------

    boost::json::object Esm::CallWithRetry(const std::string &action, const boost::json::object &payload, const Headers &headers) const {
        return Result(action, WithRetry(action, [&] { return Post(action, payload, headers); }));
    }

    HttpResponse Esm::WithRetry(const std::string &action, const std::function<HttpResponse()> &send) const {

        for (int attempt = 1;; ++attempt) {

            const bool last = attempt == MaxPartAttempts;
            std::optional<HttpResponse> response;

            try {
                response = send();
            } catch (const EuclidError &) {
                if (last) throw;
            }

            if (response.has_value() && (response->statusCode < 500 || last)) {
                if (!response->IsSuccess()) throw ServiceError(Target(), action, response->statusCode, response->body);
                return *response;
            }

            std::this_thread::sleep_for(_retryBaseDelay * attempt);
        }
    }

    ModuleClient::Headers Esm::AttributeHeaders(const COM::VariantMap &attributes, const COM::VariantMap &systemAttributes) {
        Headers headers;
        if (!attributes.empty()) headers["x-euclid-attributes"] = boost::json::serialize(COM::VariantMapToJson(attributes));
        if (!systemAttributes.empty()) headers["x-euclid-system-attributes"] = boost::json::serialize(COM::VariantMapToJson(systemAttributes));
        return headers;
    }

}// namespace Euclid::CDK::ESM
