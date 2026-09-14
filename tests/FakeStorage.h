// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Euclid includes
#include "FakeGateway.h"

namespace Euclid::CDK::Test {

    /**
     * @brief A euclid storage module small enough to read, for the ESM tests.
     *
     * @par
     * Buckets and objects in maps, and the multipart sequences implemented rather than stubbed: an
     * upload assembles its parts in part order and a download hands back the byte range that was
     * asked for, so a client that numbers its parts wrongly, sizes them inconsistently or reassembles
     * them out of order fails here rather than by writing a corrupt object to a real server.
     *
     * @par
     * Reached through FakeGateway, which is what authenticates the requests before any of this is.
     * Its handlers run under one lock, because a transfer sends its parts at once and a test that
     * failed on this class's own maps would say nothing about the client.
     *
     * @author jensvogt47\@gmail.com
     */
    class FakeStorage {
    public:

        /**
         * @brief The ERN a bucket of this name has here.
         */
        [[nodiscard]]
        static std::string BucketErn(const std::string &name);

        /**
         * @brief Answers the actions this stand-in implements.
         *
         * @param request the request.
         * @return the response, or nothing when the action is not one of these - which is what lets
         * a test answer the JSON actions itself.
         */
        [[nodiscard]]
        std::optional<RawResponse> Handle(const Request &request);

        /**
         * @brief Fails the next requests for an action.
         *
         * @par
         * A 500 is the storage node that is briefly unavailable, which a client is right to try
         * again; a 4xx is the request that is simply wrong, which it is not.
         *
         * @param action the action to fail.
         * @param times  how many times.
         * @param status the status to fail it with.
         */
        void FailNext(const std::string &action, int times = 1, int status = 500);

        /**
         * @brief Puts an object there without going through a request, for what a test starts from.
         */
        void Store(const std::string &bucketErn, const std::string &key, const std::string &data);

        /**
         * @brief An object's bytes, or nothing when there is no such object.
         */
        [[nodiscard]]
        std::optional<std::string> Object(const std::string &bucketErn, const std::string &key) const;

        /**
         * @brief The x-euclid-attributes header the write of this object carried, if any.
         */
        [[nodiscard]]
        std::string AttributeHeader(const std::string &bucketErn, const std::string &key) const;

        /**
         * @brief The x-euclid-system-attributes header the write of this object carried, if any.
         */
        [[nodiscard]]
        std::string SystemAttributeHeader(const std::string &bucketErn, const std::string &key) const;

        /**
         * @brief The x-euclid-expected-concurrency each create-upload and create-download declared.
         */
        [[nodiscard]]
        std::vector<std::string> DeclaredConcurrency() const;

        /**
         * @brief How many requests for an action were answered, failures included.
         */
        [[nodiscard]]
        int Attempts(const std::string &action) const;

    private:

        using ObjectKey = std::pair<std::string, std::string>;

        struct Upload {
            std::string bucketErn;
            std::string key;
            std::map<long, std::string> parts;
        };

        struct Failure {
            int times{};
            int status{};
        };

        [[nodiscard]]
        RawResponse Dispatch(const std::string &action, const Request &request);

        [[nodiscard]]
        RawResponse PutObject(const Request &request);

        [[nodiscard]]
        RawResponse GetObject(const Request &request);

        [[nodiscard]]
        RawResponse CreateUpload(const Request &request);

        [[nodiscard]]
        RawResponse UploadPart(const Request &request);

        [[nodiscard]]
        RawResponse CompleteUpload(const Request &request);

        [[nodiscard]]
        RawResponse CreateDownload(const Request &request);

        [[nodiscard]]
        RawResponse DownloadPart(const Request &request);

        [[nodiscard]]
        RawResponse CompleteDownload(const Request &request);

        [[nodiscard]]
        RawResponse ListObjects(const Request &request);

        /**
         * @brief Records the object and the attribute headers the write carried.
         */
        void StoreFrom(const ObjectKey &key, const Request &request);

        /**
         * @brief One object, in the shape put-object and complete-upload answer with.
         */
        [[nodiscard]]
        std::string Stored(const ObjectKey &key) const;

        mutable std::mutex _mutex;
        std::map<ObjectKey, std::string> _objects;
        std::map<ObjectKey, std::string> _attributeHeaders;
        std::map<ObjectKey, std::string> _systemAttributeHeaders;
        std::map<std::string, Upload> _uploads;
        std::map<std::string, ObjectKey> _downloads;
        std::vector<std::string> _declaredConcurrency;
        std::map<std::string, Failure> _failures;
        std::map<std::string, int> _attempts;
    };

}// namespace Euclid::CDK::Test
