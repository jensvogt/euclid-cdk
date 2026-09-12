// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief Everything ESM does, in the order you would do it.
 *
 * @par
 * @code
 * esm-walkthrough https://euclid.example.com jens secret
 * @endcode
 *
 * @par
 * Works in a bucket of its own, named after the moment it started, and deletes it again at the end -
 * so it is safe to point at a running deployment, and a run that dies halfway leaves one obviously
 * disposable bucket behind rather than touching anything of yours.
 */

// C++ includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

// Euclid includes
#include <euclid/cdk/Euclid.h>

using namespace Euclid::CDK;

namespace {

    /**
     * @brief A scratch directory of our own, removed again whatever happens.
     */
    struct Scratch {

        Scratch() : path(std::filesystem::temp_directory_path() / ("euclid-cdk-walkthrough-" + std::to_string(::time(nullptr)))) {
            std::filesystem::create_directories(path);
        }

        ~Scratch() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }

        Scratch(const Scratch &) = delete;
        Scratch &operator=(const Scratch &) = delete;

        [[nodiscard]]
        std::string File(const std::string &name) const { return (path / name).string(); }

        std::filesystem::path path;
    };

    void write(const std::string &file, const std::string &content) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    [[nodiscard]]
    std::string read(const std::string &file) {
        std::ifstream in(file, std::ios::binary);
        return {std::istreambuf_iterator(in), std::istreambuf_iterator<char>()};
    }

    /**
     * @brief Everything between creating the bucket and deleting it.
     */
    void walk(const ESM::Esm &esm, const std::string &bucketErn) {

        esm.SetBucketTag(bucketErn, "purpose", "sdk-walkthrough");

        // Small enough for one request. The bytes go over the wire as bytes, not as base64 in a JSON
        // field, which is what keeps a large object the size it is.
        const auto stored = esm.PutObject(bucketErn, "notes/hello.txt", "written in one request\n",
                                          {.attributes = {{"author", std::string("euclid-cdk")}, {"revision", 1L}}});
        std::cout << "\nput   " << std::setw(24) << std::left << stored.key << std::setw(9) << std::right << stored.size
                  << " bytes  md5=" << stored.md5Sum << "\n";

        std::cout << "      attributes:";
        for (const auto &[name, value]: esm.ListObjectAttributes(stored.ern)) {
            std::cout << " " << name << "=" << value.ToString() << " (" << value.Type() << ")";
        }
        std::cout << "\n";

        const Scratch scratch;
        const auto source = scratch.File("large.bin");
        // ~10 MiB: three parts at the default part size.
        write(source, std::string(10 * 1024 * 1024 + 1, 'e'));

        const auto uploaded = esm.UploadFile(bucketErn, "data/large.bin", source, {.attributes = {{"origin", std::string("EsmWalkthrough")}}});
        std::cout << "put   " << std::setw(24) << std::left << uploaded.key << std::setw(9) << std::right << uploaded.size
                  << " bytes  (in parts, several at a time)\n";

        // Tried in one request first and fetched in parts when that comes back "too large", so the
        // caller does not have to know which of the two an object needs.
        const auto target = scratch.File("downloaded.bin");
        const auto written = esm.DownloadFile(bucketErn, "data/large.bin", target);
        std::cout << "got   " << std::setw(24) << std::left << "downloaded.bin" << std::setw(9) << std::right << written
                  << " bytes  identical=" << std::boolalpha << (read(target) == read(source)) << "\n";

        const auto objects = esm.ListObjects(bucketErn, {.pageSize = 10});
        std::cout << "\n" << objects.total << " object(s) in the bucket:\n";
        for (const auto &object: objects.items) {
            std::cout << "  " << std::setw(24) << std::left << object.key << std::setw(10) << std::right << object.size
                      << "  " << object.contentType << "\n";
        }

        const auto copied = esm.CopyObject(bucketErn, "notes/hello.txt", bucketErn, "notes/hello.copy.txt");
        std::cout << "\ncopied to " << copied.key << ", which is its own object with its own ERN\n";

        const auto deleted = esm.DeleteObjects(bucketErn, {"notes/hello.copy.txt", "never-existed.txt"});
        std::cout << "deleted " << deleted.objects << " of the " << deleted.asked
                  << " key(s) asked for - one named nothing, which is not an error\n";

        std::cout << "\nbucket holds " << esm.GetObjectCount(bucketErn) << " object(s), "
                  << esm.GetBucketSize(bucketErn) << " byte(s)\n";

        try {
            std::cout << "subscriptions: " << esm.ListSubscriptions(bucketErn).size() << "\n";
        } catch (const ServiceError &ex) {
            std::cout << "subscriptions: unavailable (" << ex.Reason() << ")\n";
        }
    }

}// namespace

int main(const int argc, char *argv[]) {

    if (argc < 4) {
        std::cerr << "usage: " << argv[0] << " <server-url> <user-id> <password> [namespace]\n"
                  << "   e.g. " << argv[0] << " https://euclid.example.com jens secret reports\n";
        return 2;
    }

    const std::string baseUrl = argv[1];
    const std::string nameSpace = argc > 4 ? argv[4] : "";
    const auto name = "cdk-walkthrough-" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                                  std::chrono::system_clock::now().time_since_epoch())
                                                                  .count());

    try {
        auto builder = EAM::Eam::ForServer(baseUrl).Credentials(argv[2], argv[3]);
        if (!nameSpace.empty()) builder.Namespace(nameSpace);

        const auto session = builder.Login();
        const ESM::Esm esm(session);

        std::cout << "logged in to " << session.BaseUrl() << " as " << session.UserId()
                  << " (" << session.AccountId() << ", " << session.Region() << ")\n";

        const auto bucket = esm.CreateBucket(name);
        std::cout << "created bucket " << bucket.name << "\n"
                  << "  ern: " << bucket.ern << "   (this, not the name, is what every other call takes)\n";

        try {
            walk(esm, bucket.ern);
        } catch (...) {
            // Emptied first: a bucket with objects in it cannot be deleted, which is the server
            // refusing to lose track of data rather than an inconvenience.
            std::ignore = esm.PurgeBucket(bucket.ern);
            esm.DeleteBucket(bucket.ern);
            throw;
        }

        std::ignore = esm.PurgeBucket(bucket.ern);
        esm.DeleteBucket(bucket.ern);
        std::cout << "\ndeleted bucket " << name << " again\n";

        std::cout << "\nSDK version " << Version << "\n";
        return 0;

    } catch (const AuthenticationError &ex) {
        std::cerr << "login refused: " << ex.what() << "\n";
        return 1;
    } catch (const ServiceError &ex) {
        std::cerr << ex.Target() << "/" << ex.Action() << " failed: " << ex.Reason() << "\n";
        return 1;
    } catch (const EuclidError &ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
