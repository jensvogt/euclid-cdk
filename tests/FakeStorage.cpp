// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <ranges>
#include <set>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/Json.h>

#include "FakeStorage.h"

namespace Euclid::CDK::Test {

    namespace {

        /**
         * @brief What this stand-in answers. Anything else is left to the test that set it up.
         */
        const std::set<std::string> kHandled{
                "put-object", "get-object",
                "create-upload", "upload-part", "complete-upload",
                "create-download", "download-part", "complete-download",
                "list-objects", "delete-object"};

        std::string header(const Request &request, const std::string &name) {
            return std::string(request[name]);
        }

        long number(const Request &request, const std::string &name) {
            const auto value = header(request, name);
            return value.empty() ? 0 : std::stol(value);
        }

        boost::json::value body(const Request &request) {
            return Json::Parse(request.body());
        }

    }// namespace

    std::string FakeStorage::BucketErn(const std::string &name) {
        return "ern:euclid:esm:eu-central-1:000000000000:bucket/" + name;
    }

    std::optional<RawResponse> FakeStorage::Handle(const Request &request) {

        const auto action = std::string(request["x-euclid-action"]);
        if (!kHandled.contains(action)) return std::nullopt;

        const std::lock_guard lock(_mutex);
        ++_attempts[action];

        if (const auto failure = _failures.find(action); failure != _failures.end() && failure->second.times > 0) {
            --failure->second.times;
            return FakeGateway::Json(failure->second.status, R"({"error": "storage temporarily unavailable"})");
        }
        return Dispatch(action, request);
    }

    void FakeStorage::FailNext(const std::string &action, const int times, const int status) {
        const std::lock_guard lock(_mutex);
        _failures[action] = {.times = times, .status = status};
    }

    void FakeStorage::Store(const std::string &bucketErn, const std::string &key, const std::string &data) {
        const std::lock_guard lock(_mutex);
        _objects[{bucketErn, key}] = data;
    }

    std::optional<std::string> FakeStorage::Object(const std::string &bucketErn, const std::string &key) const {
        const std::lock_guard lock(_mutex);
        const auto found = _objects.find({bucketErn, key});
        return found == _objects.end() ? std::nullopt : std::optional(found->second);
    }

    std::string FakeStorage::AttributeHeader(const std::string &bucketErn, const std::string &key) const {
        const std::lock_guard lock(_mutex);
        const auto found = _attributeHeaders.find({bucketErn, key});
        return found == _attributeHeaders.end() ? std::string{} : found->second;
    }

    std::string FakeStorage::SystemAttributeHeader(const std::string &bucketErn, const std::string &key) const {
        const std::lock_guard lock(_mutex);
        const auto found = _systemAttributeHeaders.find({bucketErn, key});
        return found == _systemAttributeHeaders.end() ? std::string{} : found->second;
    }

    std::vector<std::string> FakeStorage::DeclaredConcurrency() const {
        const std::lock_guard lock(_mutex);
        return _declaredConcurrency;
    }

    int FakeStorage::Attempts(const std::string &action) const {
        const std::lock_guard lock(_mutex);
        const auto found = _attempts.find(action);
        return found == _attempts.end() ? 0 : found->second;
    }

    RawResponse FakeStorage::Dispatch(const std::string &action, const Request &request) {
        if (action == "put-object") return PutObject(request);
        if (action == "get-object") return GetObject(request);
        if (action == "create-upload") return CreateUpload(request);
        if (action == "upload-part") return UploadPart(request);
        if (action == "complete-upload") return CompleteUpload(request);
        if (action == "create-download") return CreateDownload(request);
        if (action == "download-part") return DownloadPart(request);
        if (action == "complete-download") return CompleteDownload(request);
        if (action == "list-objects") return ListObjects(request);
        return FakeGateway::Json(200, R"({"ern": ""})");// delete-object
    }

    // -- single-request transfers -------------------------------------------------------------------

    RawResponse FakeStorage::PutObject(const Request &request) {
        const ObjectKey key{header(request, "x-euclid-bucket-ern"), header(request, "x-euclid-key")};
        _objects[key] = request.body();
        StoreFrom(key, request);
        return FakeGateway::Json(200, Stored(key));
    }

    RawResponse FakeStorage::GetObject(const Request &request) {

        const ObjectKey key{header(request, "x-euclid-bucket-ern"), header(request, "x-euclid-key")};
        const auto found = _objects.find(key);
        if (found == _objects.end()) return FakeGateway::Json(404, R"({"error": "object not found"})");

        // At or above what the caller said it would take, not over it: the client declares its part
        // size, and an object exactly that long needs the multipart path.
        if (static_cast<long>(found->second.size()) >= number(request, "x-euclid-part-size")) {
            return FakeGateway::Json(413, R"({"error": "object too large for a single response"})");
        }
        return FakeGateway::Bytes(200, found->second);
    }

    // -- multipart upload ---------------------------------------------------------------------------

    RawResponse FakeStorage::CreateUpload(const Request &request) {

        const auto payload = body(request);
        _declaredConcurrency.push_back(header(request, "x-euclid-expected-concurrency"));

        const auto uploadId = "upload-" + std::to_string(_uploads.size() + 1);
        _uploads[uploadId] = {.bucketErn = Json::Text(payload, "bucketErn"), .key = Json::Text(payload, "key"), .parts = {}};

        return FakeGateway::Json(200, boost::json::serialize(boost::json::object{
                                              {"uploadId", uploadId},
                                              {"bucketErn", _uploads[uploadId].bucketErn},
                                              {"key", _uploads[uploadId].key}}));
    }

    RawResponse FakeStorage::UploadPart(const Request &request) {

        const auto upload = _uploads.find(header(request, "x-euclid-upload-id"));
        if (upload == _uploads.end()) return FakeGateway::Json(404, R"({"error": "upload not found"})");

        const auto partNumber = number(request, "x-euclid-part-number");
        upload->second.parts[partNumber] = request.body();

        return FakeGateway::Json(200, boost::json::serialize(boost::json::object{
                                              {"uploadId", upload->first},
                                              {"partNumber", partNumber},
                                              {"size", static_cast<long>(request.body().size())}}));
    }

    RawResponse FakeStorage::CompleteUpload(const Request &request) {

        const auto upload = _uploads.find(Json::Text(body(request), "uploadId"));
        if (upload == _uploads.end()) return FakeGateway::Json(404, R"({"error": "upload not found"})");

        // Assembled in part order rather than in arrival order, which is the whole point of this
        // class: parts arrive from several threads and a client that numbered them wrongly writes an
        // object whose bytes are shuffled.
        const ObjectKey key{upload->second.bucketErn, upload->second.key};
        std::string assembled;
        for (const auto &part: upload->second.parts | std::views::values) assembled += part;

        _objects[key] = assembled;
        StoreFrom(key, request);
        _uploads.erase(upload);

        return FakeGateway::Json(200, Stored(key));
    }

    // -- multipart download -------------------------------------------------------------------------

    RawResponse FakeStorage::CreateDownload(const Request &request) {

        const auto payload = body(request);
        _declaredConcurrency.push_back(header(request, "x-euclid-expected-concurrency"));

        const ObjectKey key{Json::Text(payload, "bucketErn"), Json::Text(payload, "key")};
        const auto found = _objects.find(key);
        if (found == _objects.end()) return FakeGateway::Json(404, R"({"error": "object not found"})");

        const auto downloadId = "download-" + std::to_string(_downloads.size() + 1);
        _downloads[downloadId] = key;

        return FakeGateway::Json(200, boost::json::serialize(boost::json::object{
                                              {"downloadId", downloadId},
                                              {"bucketErn", key.first},
                                              {"key", key.second},
                                              {"ern", key.first + "/" + key.second},
                                              {"size", static_cast<long>(found->second.size())},
                                              {"contentType", "application/octet-stream"}}));
    }

    RawResponse FakeStorage::DownloadPart(const Request &request) {

        const auto download = _downloads.find(header(request, "x-euclid-download-id"));
        if (download == _downloads.end()) return FakeGateway::Json(404, R"({"error": "download not found"})");

        const auto &data = _objects[download->second];
        const auto partNumber = number(request, "x-euclid-part-number");
        const auto partSize = number(request, "x-euclid-part-size");
        const auto offset = std::min(static_cast<std::size_t>((partNumber - 1) * partSize), data.size());

        return FakeGateway::Bytes(200, data.substr(offset, static_cast<std::size_t>(partSize)));
    }

    RawResponse FakeStorage::CompleteDownload(const Request &request) {
        if (_downloads.erase(Json::Text(body(request), "downloadId")) == 0) {
            return FakeGateway::Json(404, R"({"error": "download not found"})");
        }
        return FakeGateway::Json(200, "{}");
    }

    // -- listings -----------------------------------------------------------------------------------

    RawResponse FakeStorage::ListObjects(const Request &request) {

        const auto payload = body(request);
        const auto bucketErn = Json::Text(payload, "bucketErn");
        const auto prefix = Json::Text(payload, "prefix");

        boost::json::array objects;
        for (const auto &key: _objects | std::views::keys) {
            if (key.first == bucketErn && key.second.starts_with(prefix)) {
                objects.push_back(boost::json::parse(Stored(key)));
            }
        }

        return FakeGateway::Json(200, boost::json::serialize(boost::json::object{
                                              {"total", static_cast<long>(objects.size())},
                                              {"objects", objects}}));
    }

    // -- internals ----------------------------------------------------------------------------------

    void FakeStorage::StoreFrom(const ObjectKey &key, const Request &request) {
        _attributeHeaders[key] = header(request, "x-euclid-attributes");
        _systemAttributeHeaders[key] = header(request, "x-euclid-system-attributes");
    }

    std::string FakeStorage::Stored(const ObjectKey &key) const {
        const auto &data = _objects.at(key);
        return boost::json::serialize(boost::json::object{
                {"ern", key.first + "/" + key.second},
                {"bucketErn", key.first},
                {"key", key.second},
                {"size", static_cast<long>(data.size())},
                {"status", "COMPLETED"},
                {"contentType", "application/octet-stream"},
                // A digest of the bytes, which is all this is used for here - what matters is that
                // it changes when the assembled object does.
                {"md5Sum", Crypto::Sha256Hex(data).substr(0, 32)}});
    }

}// namespace Euclid::CDK::Test
