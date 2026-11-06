// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief ESM, end to end against a fake euclid server.
 *
 * @par
 * The JSON actions are checked the way EAM's are - what went on the wire, and what came back off it.
 * The transfers are checked against a storage stand-in that actually assembles what it is sent
 * (FakeStorage), because the part of a multipart upload a unit test cannot see is exactly the part
 * that corrupts an object: a part number off by one, a part size the two ends disagree about, or a
 * reassembly that depends on the order the parts happened to arrive in.
 */

// C++ includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <utility>

// Boost includes
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/esm/Esm.h>

#include "FakeGateway.h"
#include "FakeStorage.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;
using Euclid::CDK::Test::FakeStorage;

namespace {

    const std::string kBucket = FakeStorage::BucketErn("reports");

    /**
     * @brief A gateway with a storage module behind it, answering anything it does not implement
     * with one canned body.
     */
    FakeGateway::Handler storageHandler(FakeStorage &storage, const std::string &body = "{}") {
        return Test::Authenticated([&storage, body](const Request &request) -> RawResponse {
            if (auto answered = storage.Handle(request); answered.has_value()) return *answered;
            return FakeGateway::Json(200, body);
        });
    }

    /**
     * @brief A gateway, a logged-in session and an ESM client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EsmClient {

        explicit EsmClient(FakeGateway::Handler handler, const EAM::AuthMode auth = EAM::AuthMode::Auto)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Auth(auth).Login()), esm(session) {
            // Retries without the waiting. The delay is what makes a retry kind to a struggling
            // server, and what would make this suite take a minute to say the same thing.
            esm.SetRetryBaseDelay(std::chrono::milliseconds::zero());
        }

        FakeGateway gateway;
        EAM::Session session;
        ESM::Esm esm;
    };

    /**
     * @brief A file in the temporary directory, removed when the test ends.
     */
    struct ScratchFile {

        explicit ScratchFile(const std::string &name)
            : path((std::filesystem::temp_directory_path() / ("euclid-cdk-esm-" + name)).string()) {
            Remove();
        }

        ~ScratchFile() { Remove(); }

        ScratchFile(const ScratchFile &) = delete;
        ScratchFile &operator=(const ScratchFile &) = delete;

        void Write(const std::string &data) const {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }

        [[nodiscard]]
        std::string Read() const {
            std::ifstream in(path, std::ios::binary);
            return {std::istreambuf_iterator(in), std::istreambuf_iterator<char>()};
        }

        void Remove() const {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }

        std::string path;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief The request the gateway served for this action, of those it served at all.
     */
    Request requestFor(const FakeGateway &gateway, const std::string &action) {
        for (const auto &request: gateway.Received()) {
            if (std::string(request["x-euclid-action"]) == action) return request;
        }
        return {};
    }

    bool sawAction(const FakeGateway &gateway, const std::string &action) {
        return !std::string(requestFor(gateway, action)["x-euclid-action"]).empty();
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EsmBucketTest)

    BOOST_AUTO_TEST_CASE(CreatesAndListsBuckets) {
        const auto buckets = boost::json::serialize(boost::json::object{
                {"total", 2},
                {"buckets", boost::json::array{
                                    boost::json::object{
                                            {"name", "reports"},
                                            {"ern", kBucket},
                                            {"owner", "jens"},
                                            {"size", 2048},
                                            {"objects", 3},
                                            {"tags", boost::json::object{{"team", "finance"}}},
                                            {"encrypted", true},
                                            {"encryptionKeyErn", "ern:euclid:ekm:eu-central-1:000000000000:key/1"},
                                            {"created", "2026-01-01T00:00:00Z"}},
                                    boost::json::object{{"name", "euclid-artifacts"}, {"internal", true}}}}});

        const EsmClient client(Test::Answering(buckets));

        const auto page = client.esm.ListBuckets({.prefix = "rep", .pageSize = 25});
        BOOST_TEST(page.total == 2L);
        BOOST_REQUIRE(page.items.size() == 2U);
        BOOST_TEST(page.items[0].name == "reports");
        BOOST_TEST(page.items[0].ern == kBucket);
        BOOST_TEST(page.items[0].size == 2048L);
        BOOST_TEST(page.items[0].objects == 3L);
        BOOST_TEST(page.items[0].tags.at("team") == "finance");
        BOOST_TEST(page.items[0].encrypted);
        BOOST_TEST(!page.items[0].internal);
        BOOST_TEST(page.items[1].internal);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("prefix").as_string() == "rep");
        BOOST_TEST(body.at("pageSize").as_int64() == 25);
        BOOST_TEST(body.at("sortColumn").as_string() == "name");
        BOOST_TEST(body.at("includeInternal").as_bool() == false);
    }

    BOOST_AUTO_TEST_CASE(CreateBucketSaysWhetherItIsEuclidsOwn) {
        const EsmClient client(Test::Answering(R"({"name": "reports", "ern": "ern:bucket/reports"})"));

        const auto created = client.esm.CreateBucket("reports");
        BOOST_TEST(created.name == "reports");
        BOOST_TEST(created.ern == "ern:bucket/reports");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "reports");
        BOOST_TEST(body.at("internal").as_bool() == false);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-target"]) == "esm");
    }

    // One bucket is described exactly as a listing describes each of its own, so that a field
    // added to the one is in the other by construction rather than by somebody remembering.
    BOOST_AUTO_TEST_CASE(DescribesOneBucketTheWayAListingDescribesEach) {
        const EsmClient client(Test::Answering(R"({"bucket": {"name": "reports", "ern": "ern:bucket/reports",
                                                              "owner": "jens", "size": 2048, "objects": 7,
                                                              "tags": {"team": "media"}, "encrypted": true}})"));

        const auto bucket = client.esm.GetBucket("reports");

        BOOST_TEST(bucket.ern == "ern:bucket/reports");
        BOOST_TEST(bucket.size == 2048L);
        BOOST_TEST(bucket.objects == 7L);
        BOOST_TEST(bucket.encrypted);
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "reports");
    }

    // A name is resolved in the session's own namespace and an ERN is not, so which of the two was
    // given has to reach the server as the field it is - the caller should not have to say.
    BOOST_AUTO_TEST_CASE(AsksForABucketByErnAsWellAsByName) {
        const EsmClient client(Test::Answering(R"({"bucket": {"name": "reports", "ern": "ern:bucket/reports"}})"));

        std::ignore = client.esm.GetBucket(kBucket);

        BOOST_TEST(lastBody(client.gateway).at("ern").as_string() == kBucket);
    }

    // The actions whose whole answer is one value are answered as that value rather than as an
    // object a caller has to pick apart.
    BOOST_AUTO_TEST_CASE(AnswersTheSingleValueActionsAsValues) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "size": 4096, "count": 7})"));

        BOOST_TEST(client.esm.GetBucketErn("reports") == "ern:bucket/reports");
        BOOST_TEST(client.esm.GetBucketSize(kBucket) == 4096L);
        BOOST_TEST(client.esm.GetObjectCount(kBucket) == 7L);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-object-count");
    }

    // The two counts are different questions: get-object-count reads the bucket's stored running
    // total, count-objects runs a query. They were one call until euclid 1.0.73, which took a
    // prefix and ignored it - so a caller asking about part of a bucket was quietly given the
    // whole bucket's figure.
    BOOST_AUTO_TEST_CASE(CountsForRealAndSaysWhatItCounted) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "prefix": "2026/",
                                                   "includeDirectories": true, "count": 7})"));

        BOOST_TEST(client.esm.CountObjects(kBucket, "2026/", true) == 7L);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "count-objects");
        BOOST_TEST(lastBody(client.gateway).at("prefix").as_string() == "2026/");
        BOOST_TEST(lastBody(client.gateway).at("includeDirectories").as_bool());
    }

    BOOST_AUTO_TEST_CASE(CountsTheWholeBucketWithoutDirectoriesByDefault) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "count": 3})"));

        BOOST_TEST(client.esm.CountObjects(kBucket) == 3L);

        // Both defaults go on the wire rather than being left out: the server reads them the same
        // either way, but a request that says what it means is one a packet capture explains.
        BOOST_TEST(lastBody(client.gateway).at("prefix").as_string() == "");
        BOOST_TEST(!lastBody(client.gateway).at("includeDirectories").as_bool());
    }

    BOOST_AUTO_TEST_CASE(TagsRenamesPurgesAndFlagsABucket) {
        const EsmClient client(Test::Answering(R"({"name": "archive", "ern": "ern:bucket/archive", "objects": 12,
                                                   "subscriptions": 2, "internal": true, "count": 12})"));

        const auto renamed = client.esm.RenameBucket(kBucket, "archive");
        BOOST_TEST(renamed.ern == "ern:bucket/archive");
        BOOST_TEST(renamed.objects == 12L);
        BOOST_TEST(renamed.subscriptions == 2L);
        BOOST_TEST(lastBody(client.gateway).at("newName").as_string() == "archive");

        // Absent means true on the server too: the command exists to hide a bucket.
        const auto flagged = client.esm.SetBucketInternal(kBucket);
        BOOST_TEST(flagged.internal);
        BOOST_TEST(lastBody(client.gateway).at("internal").as_bool() == true);

        const auto purged = client.esm.PurgeBucket(kBucket, "2025/");
        BOOST_TEST(purged.count == 12L);
        BOOST_TEST(lastBody(client.gateway).at("prefix").as_string() == "2025/");
        BOOST_TEST(lastBody(client.gateway).at("async").as_bool() == false);

        client.esm.AddBucketTag(kBucket, "team", "finance");
        BOOST_TEST(lastBody(client.gateway).at("value").as_string() == "finance");
        client.esm.SetBucketTag(kBucket, "team", "treasury");
        BOOST_TEST(lastBody(client.gateway).at("value").as_string() == "treasury");
        client.esm.DeleteBucketTag(kBucket, "team");
        BOOST_TEST(lastBody(client.gateway).at("key").as_string() == "team");
    }

    // Encryption says what happens to the next upload, and reports what it did not touch.
    BOOST_AUTO_TEST_CASE(EncryptionReportsWhatItDidNotTouch) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "name": "reports",
                                                   "keyErn": "ern:key/1", "keyId": "reports-key", "algorithm": "AES-256",
                                                   "keyCreated": true, "existingObjects": 9,
                                                   "previousKeyErn": "ern:key/1", "previousKeyId": "reports-key",
                                                   "encryptedObjects": 9})"));

        const auto enabled = client.esm.EnableEncryption(kBucket);
        BOOST_TEST(enabled.keyCreated);
        BOOST_TEST(enabled.algorithm == "AES-256");
        BOOST_TEST(enabled.existingObjects == 9L);
        BOOST_TEST(lastBody(client.gateway).at("keyId").as_string() == "");

        const auto disabled = client.esm.DisableEncryption(kBucket);
        BOOST_TEST(disabled.previousKeyId == "reports-key");
        BOOST_TEST(disabled.encryptedObjects == 9L);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EsmObjectTest)

    BOOST_AUTO_TEST_CASE(NamesBothEndsWhenCopyingMovingAndRenaming) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:object/2", "bucketErn": "ern:bucket/archive", "key": "2026/copy.pdf", "size": 12})"));

        const auto copied = client.esm.CopyObject(kBucket, "2026/q3.pdf", "ern:bucket/archive", "2026/copy.pdf");
        BOOST_TEST(copied.key == "2026/copy.pdf");
        auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("sourceBucketErn").as_string() == kBucket);
        BOOST_TEST(body.at("sourceKey").as_string() == "2026/q3.pdf");
        BOOST_TEST(body.at("targetKey").as_string() == "2026/copy.pdf");

        std::ignore = client.esm.MoveObject(kBucket, "2026/q3.pdf", "ern:bucket/archive", "2026/copy.pdf");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "move-object");

        // A rename cannot leave the bucket, which is the whole difference between it and a move.
        std::ignore = client.esm.RenameObject(kBucket, "2026/q3.pdf", "2026/q3-final.pdf");
        body = lastBody(client.gateway);
        BOOST_TEST(body.at("bucketErn").as_string() == kBucket);
        BOOST_TEST(body.at("newKey").as_string() == "2026/q3-final.pdf");
    }

    // A key that named nothing is not an error, so the two counts are reported separately.
    BOOST_AUTO_TEST_CASE(ReportsHowManyOfTheKeysAskedForNamedAnObject) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "asked": 2, "objects": 1, "async": false})"));

        const auto deleted = client.esm.DeleteObjects(kBucket, {"there.txt", "never-existed.txt"});
        BOOST_TEST(deleted.asked == 2L);
        BOOST_TEST(deleted.objects == 1L);
        BOOST_TEST(!deleted.background);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("keys").as_array().size() == 2U);
        BOOST_TEST(body.at("async").as_bool() == false);
    }

    BOOST_AUTO_TEST_CASE(TouchesABucketInTheBackground) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "bucketName": "reports",
                                                   "prefix": "2026/", "objects": 40, "async": true})"));

        const auto touched = client.esm.TouchObject(kBucket, {.prefix = "2026/", .background = true});
        BOOST_TEST(touched.bucketName == "reports");
        BOOST_TEST(touched.objects == 40L);
        BOOST_TEST(touched.background);
        BOOST_TEST(lastBody(client.gateway).at("async").as_bool() == true);
    }

    BOOST_AUTO_TEST_CASE(ListsObjectsAsAPageWithTheirAttributes) {
        const auto objects = boost::json::serialize(boost::json::object{
                {"total", 1},
                {"objects", boost::json::array{boost::json::object{
                                    {"ern", "ern:object/1"},
                                    {"bucketErn", kBucket},
                                    {"key", "2026/q3.pdf"},
                                    {"size", 17},
                                    {"status", "COMPLETED"},
                                    {"contentType", "application/pdf"},
                                    {"md5Sum", "d41d8cd98f00b204e9800998ecf8427e"},
                                    {"encrypted", true},
                                    {"attributes", boost::json::object{
                                                           {"revision", boost::json::object{{"type", "long"}, {"value", 7}}},
                                                           {"author", boost::json::object{{"type", "string"}, {"value", "jens"}}}}}}}}});

        const EsmClient client(Test::Answering(objects));

        const auto page = client.esm.ListObjects(kBucket, {.prefix = "2026/"}, true);
        BOOST_REQUIRE(page.items.size() == 1U);
        BOOST_TEST(page.items[0].key == "2026/q3.pdf");
        BOOST_TEST(page.items[0].encrypted);
        BOOST_TEST(page.items[0].attributes.at("revision").Get<long>() == 7L);
        BOOST_TEST(page.items[0].attributes.at("author").Get<std::string>() == "jens");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("bucketErn").as_string() == kBucket);
        BOOST_TEST(body.at("includeDirectories").as_bool() == true);
        BOOST_TEST(body.at("sortColumn").as_string() == "name");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EsmObjectAttributeTest)

    // The type tag is what makes the round trip lossless, so it is what these check.
    BOOST_AUTO_TEST_CASE(TypesAttributesOnTheWayOutAndBack) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:object/1", "name": "revision",
                                                   "value": {"type": "long", "value": 7}})"));

        const auto attribute = client.esm.AddObjectAttribute("ern:object/1", "revision", 7L);
        BOOST_TEST(attribute.name == "revision");
        BOOST_TEST(attribute.value.Get<long>() == 7L);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("value").at("type").as_string() == "long");
        BOOST_TEST(body.at("value").at("value").as_int64() == 7);
    }

    BOOST_AUTO_TEST_CASE(KeepsTheTagAnExplicitVariantWasGiven) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:object/1", "name": "revision"})"));

        // An int and a long are the same 7 in JSON and two different columns in euclid.
        std::ignore = client.esm.SetObjectAttribute("ern:object/1", "revision", 7);
        BOOST_TEST(lastBody(client.gateway).at("value").at("type").as_string() == "int");

        std::ignore = client.esm.SetObjectAttribute("ern:object/1", "note", std::string("q3"));
        BOOST_TEST(lastBody(client.gateway).at("value").at("type").as_string() == "string");

        std::ignore = client.esm.SetObjectAttribute("ern:object/1", "draft", true);
        BOOST_TEST(lastBody(client.gateway).at("value").at("type").as_string() == "bool");
    }

    BOOST_AUTO_TEST_CASE(CarriesBinaryAttributesAsBase64) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:object/1", "name": "thumbnail",
                                                   "value": {"type": "binary", "value": "AAEC"}})"));

        const COM::Binary bytes{0x00, 0x01, 0x02};
        const auto attribute = client.esm.SetObjectAttribute("ern:object/1", "thumbnail", bytes);

        BOOST_TEST(lastBody(client.gateway).at("value").at("value").as_string() == "AAEC");
        BOOST_TEST(attribute.value.Holds<COM::Binary>());
        BOOST_TEST(attribute.value.Get<COM::Binary>() == bytes);
    }

    BOOST_AUTO_TEST_CASE(ReadsEveryAttributeOfAnObject) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:object/1", "total": 2, "attributes": {
                                                   "revision": {"type": "int", "value": 3},
                                                   "ratio": {"type": "double", "value": 0.5}}})"));

        const auto attributes = client.esm.ListObjectAttributes("ern:object/1");
        BOOST_TEST(attributes.size() == 2U);
        BOOST_TEST(attributes.at("revision").Get<int>() == 3);
        BOOST_TEST(attributes.at("ratio").Get<double>() == 0.5);
        BOOST_TEST(attributes.at("ratio").ToString() == "0.5");

        client.esm.DeleteObjectAttribute("ern:object/1", "revision");
        BOOST_TEST(lastBody(client.gateway).at("name").as_string() == "revision");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EsmSubscriptionTest)

    BOOST_AUTO_TEST_CASE(SubscribesAQueueToABucketsEvents) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:subscription/1", "sourceErn": "ern:bucket/reports",
                                                   "type": "SQS", "targetErn": "ern:queue/inbox"})"));

        const auto result = client.esm.Subscribe(kBucket, std::string(COM::Queue), "ern:queue/inbox",
                                                 {.eventTypes = {std::string(ESM::ObjectCreated)}, .prefix = "2026/"});
        BOOST_TEST(result.ern == "ern:subscription/1");
        BOOST_TEST(result.type == "SQS");

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("sourceErn").as_string() == kBucket);
        BOOST_TEST(body.at("type").as_string() == "SQS");
        BOOST_TEST(body.at("targetErn").as_string() == "ern:queue/inbox");
        BOOST_REQUIRE(body.at("eventTypes").as_array().size() == 1U);
        BOOST_TEST(body.at("eventTypes").as_array()[0].as_string() == "esm.object.created");
        BOOST_TEST(body.at("prefix").as_string() == "2026/");
        BOOST_TEST(body.at("directories").as_bool() == false);
    }

    BOOST_AUTO_TEST_CASE(ListsAndRemovesSubscriptions) {
        const EsmClient client(Test::Answering(R"({"total": 1, "subscriptions": [
                                                   {"ern": "ern:subscription/1", "sourceErn": "ern:bucket/reports",
                                                    "type": "SNS", "targetErn": "ern:topic/news",
                                                    "created": "2026-09-01T08:00:00Z"}]})"));

        const auto subscriptions = client.esm.ListSubscriptions(kBucket);
        BOOST_REQUIRE(subscriptions.size() == 1U);
        BOOST_TEST(subscriptions[0].targetErn == "ern:topic/news");
        BOOST_TEST(subscriptions[0].type == "SNS");

        // By the subscription's own ERN - not the bucket's, and not the target's.
        client.esm.Unsubscribe("ern:subscription/1");
        BOOST_TEST(lastBody(client.gateway).at("ern").as_string() == "ern:subscription/1");
    }

    // Nothing about receiving one is special: it arrives as the body of an ordinary message.
    BOOST_AUTO_TEST_CASE(ADeliveredNotificationReadsAsABucketEvent) {
        const auto event = ESM::ParseBucketEvent(R"({"eventType": "esm.object.created",
                                                     "bucketErn": "ern:bucket/reports", "key": "2026/q3.pdf",
                                                     "ern": "ern:object/1", "size": 17, "contentType": "application/pdf",
                                                     "md5Sum": "d41d8cd98f00b204e9800998ecf8427e",
                                                     "attributes": {"author": {"type": "string", "value": "jens"}},
                                                     "systemAttributes": {"priority": {"type": "string", "value": "LOW"}}})");

        BOOST_TEST(event.eventType == std::string(ESM::ObjectCreated));
        BOOST_TEST(event.key == "2026/q3.pdf");
        BOOST_TEST(event.size == 17L);
        BOOST_TEST(event.attributes.at("author").Get<std::string>() == "jens");
        // The producer's decision, carried through the bucket to whatever reads the queue.
        BOOST_TEST(event.systemAttributes.at("priority").Get<std::string>() == std::string(COM::PriorityLow));
    }

    BOOST_AUTO_TEST_CASE(AMessageThatIsNotAnEventReadsAsAnEmptyOne) {
        const auto event = ESM::ParseBucketEvent("<html>not an event</html>");
        BOOST_TEST(event.eventType.empty());
        BOOST_TEST(event.key.empty());
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EsmTransferTest)

    BOOST_AUTO_TEST_CASE(PutsAndGetsAnObjectInASingleRequest) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const auto stored = client.esm.PutObject(kBucket, "notes/hello.txt", "written in one request\n");
        BOOST_TEST(stored.key == "notes/hello.txt");
        BOOST_TEST(stored.size == 23L);
        BOOST_TEST(storage.Object(kBucket, "notes/hello.txt").value() == "written in one request\n");

        // The bytes go as bytes, described by headers rather than wrapped in a JSON field.
        const auto request = requestFor(client.gateway, "put-object");
        BOOST_TEST(std::string(request["x-euclid-bucket-ern"]) == kBucket);
        BOOST_TEST(std::string(request["x-euclid-key"]) == "notes/hello.txt");
        BOOST_TEST(std::string(request[boost::beast::http::field::content_type]) == "application/octet-stream");

        BOOST_TEST(client.esm.GetObject(kBucket, "notes/hello.txt") == "written in one request\n");
    }

    BOOST_AUTO_TEST_CASE(CarriesBothAttributeMapsOnAWrite) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        std::ignore = client.esm.PutObject(kBucket, "notes/hello.txt", "hi",
                                           {.attributes = {{"author", std::string("jens")}},
                                            .systemAttributes = {{"priority", std::string(COM::PriorityLow)}}});

        const auto attributes = boost::json::parse(storage.AttributeHeader(kBucket, "notes/hello.txt"));
        BOOST_TEST(attributes.at("author").at("type").as_string() == "string");
        BOOST_TEST(attributes.at("author").at("value").as_string() == "jens");

        const auto systemAttributes = boost::json::parse(storage.SystemAttributeHeader(kBucket, "notes/hello.txt"));
        BOOST_TEST(systemAttributes.at("priority").at("value").as_string() == "LOW");
    }

    // A request with nothing to say about attributes says nothing.
    BOOST_AUTO_TEST_CASE(SendsNoAttributeHeadersWhenThereAreNone) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        std::ignore = client.esm.PutObject(kBucket, "notes/hello.txt", "hi");
        BOOST_TEST(storage.AttributeHeader(kBucket, "notes/hello.txt").empty());
        BOOST_TEST(storage.SystemAttributeHeader(kBucket, "notes/hello.txt").empty());
    }

    BOOST_AUTO_TEST_CASE(SaysSoWhenAnObjectIsTooLargeForOneResponse) {
        FakeStorage storage;
        storage.Store(kBucket, "data/large.bin", std::string(64, 'e'));
        const EsmClient client(storageHandler(storage));

        try {
            std::ignore = client.esm.GetObject(kBucket, "data/large.bin", 32);
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Status() == ESM::PayloadTooLarge);
            BOOST_TEST(ex.Target() == "esm");
            BOOST_TEST(ex.Action() == "get-object");
        }
    }

    BOOST_AUTO_TEST_CASE(UploadsAndDownloadsAFileInParts) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        // Three parts, the last one short - which is where an off-by-one in the part numbering or a
        // reassembly in arrival order shows up.
        const std::string content = std::string(4, 'a') + std::string(4, 'b') + "c";
        const ScratchFile source("upload.bin");
        source.Write(content);

        const auto uploaded = client.esm.UploadFile(kBucket, "data/large.bin", source.path, {.partSize = 4, .concurrency = 3});
        BOOST_TEST(uploaded.key == "data/large.bin");
        BOOST_TEST(uploaded.size == 9L);
        BOOST_TEST(storage.Object(kBucket, "data/large.bin").value() == content);
        BOOST_TEST(storage.Attempts("upload-part") == 3);

        // Declared up front, so the gateway's autoscaler can ramp toward the load rather than
        // discover it.
        BOOST_REQUIRE(!storage.DeclaredConcurrency().empty());
        BOOST_TEST(storage.DeclaredConcurrency()[0] == "3");

        const ScratchFile target("download.bin");
        const auto written = client.esm.DownloadFile(kBucket, "data/large.bin", target.path, {.partSize = 4, .concurrency = 3});
        BOOST_TEST(written == 9L);
        BOOST_TEST(target.Read() == content);
        BOOST_TEST(storage.Attempts("download-part") == 3);
        BOOST_TEST(storage.Attempts("complete-download") == 1);
    }

    // An object that fits in one part never opens a multipart download at all.
    BOOST_AUTO_TEST_CASE(DownloadsASmallObjectInOneRequest) {
        FakeStorage storage;
        storage.Store(kBucket, "notes/hello.txt", "small enough");
        const EsmClient client(storageHandler(storage));

        const ScratchFile target("small.txt");
        BOOST_TEST(client.esm.DownloadFile(kBucket, "notes/hello.txt", target.path) == 12L);
        BOOST_TEST(target.Read() == "small enough");
        BOOST_TEST(storage.Attempts("create-download") == 0);
    }

    // One part is not a multipart upload.
    //
    // create-upload, upload-part and complete-upload are three round trips, and on the server an
    // upload directory, a part file, an assembly pass and a separate MD5 - none of which buys
    // anything when there is only ever going to be one part. Measured on a development installation
    // before this existed: 0.94 parts per upload, so essentially every one was single-part, and
    // 793,614 objects written in an hour with every one of them under a kilobyte.
    BOOST_AUTO_TEST_CASE(SendsOnePutObjectForAFileBelowThePartSize) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("small.bin");
        source.Write("some bytes");

        const auto uploaded = client.esm.UploadFile(kBucket, "data/small.bin", source.path, {.partSize = 5000});

        BOOST_TEST(uploaded.size == 10L);
        BOOST_TEST(storage.Attempts("put-object") == 1);
        BOOST_TEST(storage.Attempts("create-upload") == 0);
        BOOST_TEST(storage.Attempts("upload-part") == 0);
    }

    // The boundary, stated rather than left to the reader: strictly below goes whole, equal does not.
    BOOST_AUTO_TEST_CASE(AFileExactlyOnePartLongStillUsesMultipart) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("exact.bin");
        source.Write("some bytes");

        std::ignore = client.esm.UploadFile(kBucket, "data/exact.bin", source.path, {.partSize = 10});

        BOOST_TEST(storage.Attempts("create-upload") == 1);
        BOOST_TEST(storage.Attempts("put-object") == 0);
    }

    // put-object carries attributes on its own headers, so a file that changed route could lose
    // the metadata whatever put it there knows about it.
    BOOST_AUTO_TEST_CASE(ASmallUploadStillCarriesItsAttributes) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("attributed-small.bin");
        source.Write("some bytes");

        std::ignore = client.esm.UploadFile(kBucket, "data/attributed-small.bin", source.path,
                                            {.partSize = 5000, .attributes = {{"origin", std::string("EsmTest")}}});

        BOOST_TEST(storage.Attempts("put-object") == 1);
        const auto attributes = boost::json::parse(storage.AttributeHeader(kBucket, "data/attributed-small.bin"));
        BOOST_TEST(attributes.at("origin").at("value").as_string() == "EsmTest");
    }

    BOOST_AUTO_TEST_CASE(StillMakesAnObjectOutOfAnEmptyFile) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("empty.bin");
        source.Write({});

        const auto uploaded = client.esm.UploadFile(kBucket, "data/empty.bin", source.path);

        // Zero bytes is below any part size, so it goes the way every other small file goes. It
        // used to create an upload, send one empty part and complete it - three calls to store
        // nothing, with the part manufactured because an empty file yields none. The object that
        // results is the same either way.
        BOOST_TEST(uploaded.size == 0L);
        BOOST_TEST(storage.Attempts("put-object") == 1);
        BOOST_TEST(storage.Attempts("create-upload") == 0);
        BOOST_TEST(storage.Object(kBucket, "data/empty.bin").has_value());
    }

    // The object row is built from what completes the upload, so that is where the attributes ride.
    BOOST_AUTO_TEST_CASE(RidesTheUploadsAttributesOnTheCallThatCompletesIt) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("attributed.bin");
        source.Write("some bytes");
        std::ignore = client.esm.UploadFile(kBucket, "data/attributed.bin", source.path,
                                            {.attributes = {{"origin", std::string("EsmTest")}}});

        BOOST_TEST(std::string(requestFor(client.gateway, "upload-part")["x-euclid-attributes"]).empty());
        const auto attributes = boost::json::parse(storage.AttributeHeader(kBucket, "data/attributed.bin"));
        BOOST_TEST(attributes.at("origin").at("value").as_string() == "EsmTest");
    }

    BOOST_AUTO_TEST_CASE(RefusesAPartSizeOfNothingBeforeAnythingIsSent) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("refused.bin");
        source.Write("some bytes");

        BOOST_CHECK_THROW(std::ignore = client.esm.UploadFile(kBucket, "data/refused.bin", source.path, {.partSize = 0}), EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.esm.DownloadFile(kBucket, "data/refused.bin", source.path, {.partSize = 0}), EuclidError);
        BOOST_TEST(storage.Attempts("create-upload") == 0);
    }

    BOOST_AUTO_TEST_CASE(AFileThatIsNotThereCostsNoRoundTrip) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        BOOST_CHECK_THROW(std::ignore = client.esm.UploadFile(kBucket, "data/gone.bin", "/no/such/file"), EuclidError);
        BOOST_TEST(storage.Attempts("create-upload") == 0);
    }

    BOOST_AUTO_TEST_CASE(OpensNoSessionWhenDownloadingAnObjectThatIsNotThere) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const ScratchFile target("missing.bin");
        BOOST_CHECK_THROW(std::ignore = client.esm.DownloadFile(kBucket, "data/missing.bin", target.path), ServiceError);
        BOOST_TEST(storage.Attempts("create-download") == 0);
    }

    // Beast reads a response into a message with a body limit of 8MB unless told otherwise, and
    // fails anything above it as "body limit exceeded" - however much the caller said it would take.
    BOOST_AUTO_TEST_CASE(TakesAResponseAboveBeastsDefaultBodyLimit) {
        FakeStorage storage;
        const std::string content(9 * 1024 * 1024, 'g');
        storage.Store(kBucket, "data/nine-megabytes.bin", content);
        const EsmClient client(storageHandler(storage));

        BOOST_TEST(client.esm.GetObject(kBucket, "data/nine-megabytes.bin", 16L * 1024 * 1024) == content);
    }

    BOOST_AUTO_TEST_CASE(UploadsAndDownloadsBytesInParts) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        // Three parts, the last one short, as for a file.
        const std::string content = std::string(4, 'a') + std::string(4, 'b') + "c";

        const auto uploaded = client.esm.UploadObject(kBucket, "data/large.bin", content, {.partSize = 4, .concurrency = 3});
        BOOST_TEST(uploaded.key == "data/large.bin");
        BOOST_TEST(uploaded.size == 9L);
        BOOST_TEST(storage.Object(kBucket, "data/large.bin").value() == content);
        BOOST_TEST(storage.Attempts("upload-part") == 3);
        BOOST_TEST(storage.Attempts("put-object") == 0);

        BOOST_TEST(client.esm.DownloadObject(kBucket, "data/large.bin", 0, {.partSize = 4, .concurrency = 3}) == content);
        BOOST_TEST(storage.Attempts("download-part") == 3);
        BOOST_TEST(storage.Attempts("complete-download") == 1);
    }

    BOOST_AUTO_TEST_CASE(SendsBytesBelowThePartSizeInOneRequest) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const auto uploaded = client.esm.UploadObject(kBucket, "data/small.bin", "some bytes",
                                                      {.partSize = 5000, .attributes = {{"origin", std::string("EsmTest")}}});

        BOOST_TEST(uploaded.size == 10L);
        BOOST_TEST(storage.Attempts("put-object") == 1);
        BOOST_TEST(storage.Attempts("create-upload") == 0);
        const auto attributes = boost::json::parse(storage.AttributeHeader(kBucket, "data/small.bin"));
        BOOST_TEST(attributes.at("origin").at("value").as_string() == "EsmTest");

        BOOST_TEST(client.esm.DownloadObject(kBucket, "data/small.bin") == "some bytes");
        BOOST_TEST(storage.Attempts("create-download") == 0);
    }

    BOOST_AUTO_TEST_CASE(StillMakesAnObjectOutOfNoBytes) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        const auto uploaded = client.esm.UploadObject(kBucket, "data/empty.bin", {});

        BOOST_TEST(uploaded.size == 0L);
        BOOST_TEST(storage.Attempts("put-object") == 1);
        BOOST_TEST(storage.Object(kBucket, "data/empty.bin").has_value());
        BOOST_TEST(client.esm.DownloadObject(kBucket, "data/empty.bin").empty());
    }

    // Refused on the size create-download reports, before a part is fetched - the whole point of a
    // limit on a download into memory being that the memory is never spent.
    BOOST_AUTO_TEST_CASE(RefusesAnObjectLargerThanTheLimitBeforeFetchingAnyPart) {
        FakeStorage storage;
        storage.Store(kBucket, "data/large.bin", std::string(64, 'e'));
        const EsmClient client(storageHandler(storage));

        try {
            std::ignore = client.esm.DownloadObject(kBucket, "data/large.bin", 32, {.partSize = 16});
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Status() == ESM::PayloadTooLarge);
            BOOST_TEST(ex.Action() == "get-object");
            BOOST_TEST(ex.Reason() == "the object is 64 bytes, larger than the 32 accepted");
        }
        BOOST_TEST(storage.Attempts("download-part") == 0);
        BOOST_TEST(storage.Attempts("complete-download") == 1);
    }

    // The same limit when the object fits in the one request tried first.
    BOOST_AUTO_TEST_CASE(RefusesASmallObjectLargerThanTheLimitToo) {
        FakeStorage storage;
        storage.Store(kBucket, "data/small.bin", std::string(10, 'e'));
        const EsmClient client(storageHandler(storage));

        try {
            std::ignore = client.esm.DownloadObject(kBucket, "data/small.bin", 5);
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Status() == ESM::PayloadTooLarge);
        }
        BOOST_TEST(storage.Attempts("create-download") == 0);
    }

    BOOST_AUTO_TEST_CASE(DownloadsAnObjectExactlyAtTheLimit) {
        FakeStorage storage;
        storage.Store(kBucket, "data/exact.bin", std::string(32, 'e'));
        const EsmClient client(storageHandler(storage));

        BOOST_TEST(client.esm.DownloadObject(kBucket, "data/exact.bin", 32, {.partSize = 16}) == std::string(32, 'e'));
    }

    BOOST_AUTO_TEST_CASE(RefusesAPartSizeOfNothingForBytesToo) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        BOOST_CHECK_THROW(std::ignore = client.esm.UploadObject(kBucket, "data/refused.bin", "some bytes", {.partSize = 0}), EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.esm.DownloadObject(kBucket, "data/refused.bin", 0, {.partSize = 0}), EuclidError);
        BOOST_TEST(storage.Attempts("create-upload") == 0);
        BOOST_TEST(storage.Attempts("put-object") == 0);
    }

    BOOST_AUTO_TEST_CASE(OpensNoSessionWhenDownloadingBytesThatAreNotThere) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        try {
            std::ignore = client.esm.DownloadObject(kBucket, "data/missing.bin");
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Status() == 404);
        }
        BOOST_TEST(storage.Attempts("create-download") == 0);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EsmRetryTest)

    BOOST_AUTO_TEST_CASE(SendsAPartThatFailedTransientlyAgain) {
        FakeStorage storage;
        storage.FailNext("upload-part", 1, 503);
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("retried.bin");
        source.Write("some bytes");

        const auto uploaded = client.esm.UploadFile(kBucket, "data/retried.bin", source.path, {.partSize = 10});
        BOOST_TEST(uploaded.size == 10L);
        BOOST_TEST(storage.Attempts("upload-part") == 2);
    }

    // The calls bracketing a transfer run once each, but giving up on one discards the whole file.
    BOOST_AUTO_TEST_CASE(RetriesTheCallsBracketingATransferToo) {
        FakeStorage storage;
        storage.FailNext("create-upload", 1, 500);
        storage.FailNext("complete-upload", 1, 500);
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("bracketed.bin");
        source.Write("some bytes");

        std::ignore = client.esm.UploadFile(kBucket, "data/bracketed.bin", source.path, {.partSize = 10});
        BOOST_TEST(storage.Attempts("create-upload") == 2);
        BOOST_TEST(storage.Attempts("complete-upload") == 2);
    }

    BOOST_AUTO_TEST_CASE(GivesUpWithTheServersReasonWhenAPartKeepsFailing) {
        FakeStorage storage;
        storage.FailNext("upload-part", 100, 500);
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("doomed.bin");
        source.Write("some bytes");

        try {
            std::ignore = client.esm.UploadFile(kBucket, "data/doomed.bin", source.path, {.partSize = 10});
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Action() == "upload-part");
            BOOST_TEST(ex.Status() == 500);
            BOOST_TEST(ex.Reason() == "storage temporarily unavailable");
        }
        BOOST_TEST(storage.Attempts("upload-part") == ESM::MaxPartAttempts);
    }

    // A 4xx means the request itself is wrong, and a repeat would be answered identically.
    BOOST_AUTO_TEST_CASE(DoesNotRetryARejectedPart) {
        FakeStorage storage;
        storage.FailNext("upload-part", 100, 400);
        const EsmClient client(storageHandler(storage));

        const ScratchFile source("rejected.bin");
        source.Write("some bytes");

        BOOST_CHECK_THROW(std::ignore = client.esm.UploadFile(kBucket, "data/rejected.bin", source.path, {.partSize = 10}), ServiceError);
        BOOST_TEST(storage.Attempts("upload-part") == 1);
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EsmAuthenticationTest)

    // What every euclid client does: sign the JSON actions, present the token for the four that
    // carry bytes.
    BOOST_AUTO_TEST_CASE(SignsTheJsonActionsAndPresentsTheTokenForTheByteActions) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage));

        std::ignore = client.esm.PutObject(kBucket, "notes/hello.txt", "hi");
        const auto bytes = requestFor(client.gateway, "put-object");
        BOOST_TEST(SigningScheme::Of(bytes) == nullptr);
        BOOST_TEST(std::string(bytes[boost::beast::http::field::authorization]) == "Bearer " + client.session.Token());

        std::ignore = client.esm.GetBucketSize(kBucket);
        const auto json = client.gateway.LastRequest();
        BOOST_REQUIRE(SigningScheme::Of(json) != nullptr);
        BOOST_TEST(SigningScheme::Of(json)->Name() == "rfc9421");
    }

    // A session that asked not to be handed a token silently is not handed one here either.
    BOOST_AUTO_TEST_CASE(SignsTheBytesTooWhenTheSessionAskedForSignatures) {
        FakeStorage storage;
        const EsmClient client(storageHandler(storage), EAM::AuthMode::Signature);

        std::ignore = client.esm.PutObject(kBucket, "notes/hello.txt", "hi");

        const auto request = requestFor(client.gateway, "put-object");
        BOOST_REQUIRE(SigningScheme::Of(request) != nullptr);
        BOOST_TEST(std::string(request[boost::beast::http::field::authorization]).empty());
    }

    // The client holds the session rather than a copy of what it knew at the time.
    BOOST_AUTO_TEST_CASE(FollowsTheSessionItCameFrom) {
        FakeStorage storage;
        EsmClient client(storageHandler(storage));

        client.session.ChangeNamespace("reports");
        std::ignore = client.esm.GetBucketSize(kBucket);

        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-namespace"]) == "reports");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-user-id"]) == "jens");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EsmLifecycleTest)

    BOOST_AUTO_TEST_CASE(KeepsVersionsWhenAskedTo) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "name": "reports", "versioning": true})"));

        BOOST_TEST(client.esm.SetBucketVersioning(kBucket, true).versioning);

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("enabled").as_bool() == true);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "set-bucket-versioning");
    }

    // A key whose current object was deleted still lists what was kept, which is what makes a
    // deletion recoverable - so an answer with no latest version is a deleted key, not a missing one.
    BOOST_AUTO_TEST_CASE(ListsVersionsIncludingForADeletedKey) {
        const EsmClient client(Test::Answering(R"({"bucketErn": "ern:bucket/reports", "key": "q3.pdf",
                                                   "ern": "ern:object/reports/q3.pdf", "versioning": true,
                                                   "versions": [{"versionId": "v2", "latest": false, "size": 2048},
                                                                {"versionId": "v1", "latest": false, "size": 1024}],
                                                   "total": 2})"));

        const auto versions = client.esm.ListObjectVersions(kBucket, "q3.pdf");

        BOOST_TEST(versions.total == 2L);
        BOOST_REQUIRE(versions.versions.size() == 2U);
        BOOST_TEST(!versions.versions[0].latest);
        BOOST_TEST(versions.versions[0].versionId == "v2");
    }

    BOOST_AUTO_TEST_CASE(ReplacesTheLifecycleRulesWholesale) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "name": "reports",
                                                   "rules": [{"id": "cold", "prefix": "archive/",
                                                              "archiveAfterDays": 90, "restoreDays": 7,
                                                              "enabled": true}]})"));

        const auto set = client.esm.SetBucketLifecycle(kBucket, {{.id = "cold", .prefix = "archive/",
                                                                 .archiveAfterDays = 90, .restoreDays = 7}});

        BOOST_REQUIRE(set.rules.size() == 1U);
        BOOST_TEST(set.rules[0].id == "cold");
        BOOST_TEST(set.rules[0].archiveAfterDays == 90L);
        BOOST_TEST(set.rules[0].enabled);

        const auto body = lastBody(client.gateway);
        BOOST_REQUIRE(body.at("rules").as_array().size() == 1U);
        BOOST_TEST(body.at("rules").as_array()[0].at("prefix").as_string() == "archive/");
    }

    // A rule written without the field is one that applies, so a reader defaulting it to false
    // would quietly disable every rule a server did not spell out.
    BOOST_AUTO_TEST_CASE(ARuleThatSaysNothingAboutBeingEnabledIsOne) {
        const EsmClient client(Test::Answering(R"({"rules": [{"id": "cold", "archiveAfterDays": 30}]})"));

        BOOST_TEST(client.esm.GetBucketLifecycle(kBucket).rules[0].enabled);
    }

    BOOST_AUTO_TEST_CASE(ArchivesAndRestoresOneObject) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:object/reports/q3.pdf",
                                                   "bucketErn": "ern:bucket/reports", "key": "q3.pdf",
                                                   "lifecycleState": "ARCHIVED",
                                                   "archivedAt": "2026-11-01T08:00:00Z",
                                                   "archiveFile": "2026-11.zip"})"));

        const auto archived = client.esm.ArchiveObject(kBucket, "q3.pdf");

        BOOST_TEST(archived.lifecycleState == std::string(ESM::LifecycleArchived));
        BOOST_TEST(archived.archiveFile == "2026-11.zip");
        // Not restored, so the field that says when a restore expires is empty.
        BOOST_TEST(archived.restoredUntil.empty());
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "archive-object");
    }

    // Zero is not a mistake here - it is what asks for the covering rule's own grace period - so
    // only the range beyond it is refused.
    BOOST_AUTO_TEST_CASE(RestoreTakesZeroDaysToMeanTheRulesOwnPeriod) {
        const EsmClient client(Test::Answering(R"({"lifecycleState": "STANDARD",
                                                   "restoredUntil": "2026-11-08T08:00:00Z"})"));

        const auto restored = client.esm.RestoreObject(kBucket, "q3.pdf");
        BOOST_TEST(restored.restoredUntil == "2026-11-08T08:00:00Z");
        BOOST_TEST(lastBody(client.gateway).at("days").as_int64() == 0);

        std::ignore = client.esm.RestoreObject(kBucket, "q3.pdf", 14);
        BOOST_TEST(lastBody(client.gateway).at("days").as_int64() == 14);

        BOOST_CHECK_THROW(std::ignore = client.esm.RestoreObject(kBucket, "q3.pdf", -1), EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.esm.RestoreObject(kBucket, "q3.pdf", ESM::MaxRestoreDays + 1), EuclidError);
    }

    // Stopping holds the notifications and leaves the objects alone - this is not a way to make a
    // bucket read-only.
    BOOST_AUTO_TEST_CASE(StopsAndStartsABucketsEvents) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/reports", "name": "reports",
                                                   "stopped": true, "held": 42})"));

        const auto stopped = client.esm.StopBucket(kBucket);
        BOOST_TEST(stopped.stopped);
        BOOST_TEST(stopped.held == 42L);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "stop-bucket");

        std::ignore = client.esm.StartBucket(kBucket);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "start-bucket");
    }

    BOOST_AUTO_TEST_CASE(ReadsVersioningAndStoppedOffABucket) {
        const EsmClient client(Test::Answering(R"({"bucket": {"name": "reports", "versioning": true, "stopped": true}})"));

        const auto bucket = client.esm.GetBucket("reports");
        BOOST_TEST(bucket.versioning);
        BOOST_TEST(bucket.stopped);
    }

    BOOST_AUTO_TEST_CASE(VerifiesObjectsAndSaysWhereToCarryOn) {
        const EsmClient client(Test::Answering(R"({"bucketErn": "ern:bucket/reports", "afterKey": "",
                                                   "scanned": 500, "checked": 480, "skipped": 20,
                                                   "problems": [{"key": "q3.pdf", "problem": "size-mismatch",
                                                                 "recordedSize": 2048, "fileSize": 1024,
                                                                 "expectedFileSize": 2048}],
                                                   "nextKey": "q4.pdf"})"));

        const auto result = client.esm.VerifyObjects(kBucket, {.limit = 500});

        BOOST_TEST(result.scanned == 500L);
        BOOST_TEST(result.skipped == 20L);
        BOOST_REQUIRE(result.problems.size() == 1U);
        BOOST_TEST(result.problems[0].problem == "size-mismatch");
        BOOST_TEST(result.problems[0].fileSize == 1024L);
        // Where the next pass starts; empty would be the end of the bucket.
        BOOST_TEST(result.nextKey == "q4.pdf");
        BOOST_TEST(lastBody(client.gateway).at("limit").as_int64() == 500);
    }

    BOOST_AUTO_TEST_CASE(RefusesAVerifyLimitTheServerWouldRefuse) {
        const EsmClient client(Test::Answering("{}"));

        BOOST_CHECK_THROW(std::ignore = client.esm.VerifyObjects(kBucket, {.limit = 0}), EuclidError);
        BOOST_CHECK_THROW(std::ignore = client.esm.VerifyObjects(kBucket, {.limit = ESM::MaxVerifyLimit + 1}), EuclidError);
    }

    BOOST_AUTO_TEST_CASE(ReadsOneObjectsSize) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:object/reports/q3.pdf", "size": 2048})"));

        BOOST_TEST(client.esm.GetObjectSize(kBucket, "q3.pdf") == 2048L);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-object-size");
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EsmCallTest)

    // Reaching an action this SDK does not wrap should not need a release.
    BOOST_AUTO_TEST_CASE(DeletesABucketInTheBackground) {
        // A bucket goes with its objects, and a large one is emptied in the background first - the
        // bucket itself going only when that finishes.
        const EsmClient client(Test::Answering(
                R"({"ern": "ern:bucket/reports", "async": true, "jobId": "job-7", "objects": 40000})"));

        const auto deleted = client.esm.DeleteBucket(kBucket, true);
        BOOST_TEST(deleted.count == 40000L);
        BOOST_TEST(deleted.background);
        BOOST_TEST(deleted.jobId == "job-7");
        BOOST_TEST(lastBody(client.gateway).at("async").as_bool() == true);
    }

    BOOST_AUTO_TEST_CASE(AbortsAnUploadAndSaysWhatWasDiscarded) {
        const EsmClient client(Test::Answering(
                R"({"uploadId": "upload-1", "bucketErn": "ern:bucket/reports", "key": "onix/big.xml",
                    "parts": 12, "objectRemoved": true})"));

        const auto aborted = client.esm.AbortUpload("upload-1");

        BOOST_TEST(aborted.key == "onix/big.xml");
        BOOST_TEST(aborted.parts == 12L);
        BOOST_TEST(aborted.objectRemoved);
        BOOST_TEST(lastBody(client.gateway).at("uploadId").as_string() == "upload-1");
    }

    // The row a re-upload was writing over is the previous version - still published, still
    // readable - so it is not removed, and the difference is what a caller cleaning up after a
    // failure has to be able to see.
    BOOST_AUTO_TEST_CASE(AnAbandonedReUploadLeavesThePublishedObject) {
        const EsmClient client(Test::Answering(R"({"uploadId": "upload-1", "parts": 3, "objectRemoved": false})"));

        BOOST_TEST(!client.esm.AbortUpload("upload-1").objectRemoved);
    }

    BOOST_AUTO_TEST_CASE(PurgesABucketInTheBackground) {
        // Emptying a bucket can take minutes, so the server writes the work down as a job and
        // answers at once. The job is what makes the instance being stopped a pause: another picks
        // it up and carries on.
        const EsmClient client(Test::Answering(
                R"({"ern": "ern:bucket/reports", "async": true, "jobId": "job-42", "objects": 120000})"));

        const auto purged = client.esm.PurgeBucket(kBucket, "", true);

        // "objects" here, "count" when it runs inline: the same figure at two points in the same
        // work, and count reads it either way.
        BOOST_TEST(purged.count == 120000L);
        BOOST_TEST(purged.background);
        BOOST_TEST(purged.jobId == "job-42");
        BOOST_TEST(lastBody(client.gateway).at("async").as_bool() == true);
    }

    BOOST_AUTO_TEST_CASE(CallReachesAnyEsmAction) {
        const EsmClient client(Test::Answering(R"({"whatever": 42})"));

        const auto answer = client.esm.Call("some-future-action", {{"argument", "value"}});
        BOOST_TEST(answer.at("whatever").as_int64() == 42);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-target"]) == "esm");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "some-future-action");
    }

    BOOST_AUTO_TEST_CASE(MetricsComeBackUnparsed) {
        const EsmClient client(Test::Answering(R"({"esm_object_count": 12})"));

        BOOST_TEST(client.esm.Metrics().at("esm_object_count").as_int64() == 12);
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "get-metrics");
    }

    BOOST_AUTO_TEST_CASE(ARefusedActionThrowsWithTheTargetAndAction) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(403, R"({"error": "not your bucket"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const ESM::Esm esm(session);

        try {
            std::ignore = esm.ListBuckets();
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "esm");
            BOOST_TEST(ex.Action() == "list-buckets");
            BOOST_TEST(ex.Status() == 403);
            BOOST_TEST(ex.Reason() == "not your bucket");
        }
        BOOST_TEST(sawAction(gateway, "list-buckets"));
    }

    // A bucket's priority is for the notifications it sends, not for the bucket - so what matters is
    // that it reaches the wire and that "no priority" is expressible.
    BOOST_AUTO_TEST_CASE(SetsThePriorityItsNotificationsAreSentWith) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/inbox", "name": "inbox", "priority": "HIGH"})"));

        const auto result = client.esm.SetBucketPriority(kBucket, "HIGH");
        BOOST_TEST(result.priority == "HIGH");
        BOOST_TEST(lastBody(client.gateway).at("priority").as_string() == "HIGH");
    }

    BOOST_AUTO_TEST_CASE(ClearingThePriorityStillSendsTheField) {
        const EsmClient client(Test::Answering(R"({"ern": "ern:bucket/inbox", "name": "inbox", "priority": ""})"));

        // Empty is an instruction here, not an omission: it is the only way back to letting the target
        // queue's own default decide. Leaving the field out would ask the server to change nothing.
        std::ignore = client.esm.SetBucketPriority(kBucket, "");
        BOOST_TEST(lastBody(client.gateway).at("priority").as_string() == "");
    }

    BOOST_AUTO_TEST_CASE(CreateBucketOnlyCarriesAPriorityWhenThereIsOne) {
        const EsmClient client(Test::Answering(R"({"name": "inbox", "ern": "ern:bucket/inbox"})"));

        std::ignore = client.esm.CreateBucket("inbox");
        // Absent rather than empty, so an older installation is not handed a field it has no meaning
        // for - and so "no priority" is what a create has always said.
        BOOST_TEST(!lastBody(client.gateway).as_object().contains("priority"));

        std::ignore = client.esm.CreateBucket("inbox", false, "HIGH");
        BOOST_TEST(lastBody(client.gateway).at("priority").as_string() == "HIGH");
    }

BOOST_AUTO_TEST_SUITE_END()
