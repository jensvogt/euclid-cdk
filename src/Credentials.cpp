// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Credentials.h>
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>

namespace Euclid::CDK {

    namespace {

        std::string environmentValue(const char *name) {
            const char *value = std::getenv(name);
            return value == nullptr ? std::string{} : std::string(value);
        }

        std::string homeDirectory() {
            if (const auto home = environmentValue("HOME"); !home.empty()) return home;
            // Windows has no HOME; the pair below is what a profile is assembled from there.
            const auto drive = environmentValue("HOMEDRIVE");
            if (const auto path = environmentValue("HOMEPATH"); !drive.empty() && !path.empty()) return drive + path;
            return environmentValue("USERPROFILE");
        }

    }// namespace

    std::string Credentials::FilePath() {
        if (const auto configured = environmentValue("EUCLID_CREDENTIALS_FILE"); !configured.empty()) return configured;
        return (std::filesystem::path(homeDirectory()) / ".euclid" / "credentials").string();
    }

    std::optional<Credentials::Entry> Credentials::Load() {

        std::ifstream in(FilePath());
        if (!in) return std::nullopt;

        std::ostringstream buffer;
        buffer << in.rdbuf();

        const auto document = Json::Parse(buffer.str());
        // No token means there is nothing to present, which is the same to a caller as there being
        // no file at all: log in.
        if (!document.is_object() || Json::Text(document, "token").empty()) return std::nullopt;

        Entry entry;
        entry.token = Json::Text(document, "token");
        entry.userId = Json::Text(document, "userId");
        entry.accountId = Json::Text(document, "accountId");
        entry.region = Json::Text(document, "region");
        entry.accessKeyId = Json::Text(document, "accessKeyId");
        entry.secretAccessKey = Json::Text(document, "secretAccessKey");
        entry.isAdmin = Json::Flag(document, "isAdmin");
        entry.nameSpace = Json::Text(document, "namespace");
        entry.baseUrl = Json::Text(document, "baseUrl");

        // "endpoint" is the same field under the name the manager writes it as. A euclid-managed
        // application is handed its credentials through EUCLID_CREDENTIALS_FILE - see FilePath() -
        // and the file the manager writes there calls the server "endpoint", where a file this SDK
        // wrote calls it "baseUrl". Reading only one of the two left an application with a valid
        // token and no idea where to send it, which is the one field it cannot do without.
        if (entry.baseUrl.empty()) entry.baseUrl = Json::Text(document, "endpoint");
        return entry;
    }

    void Credentials::Save(const Entry &entry) {

        const std::filesystem::path path(FilePath());

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::filesystem::permissions(path.parent_path(), std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, ec);

        const boost::json::object body{
                {"token", entry.token},
                {"userId", entry.userId},
                {"accountId", entry.accountId},
                {"region", entry.region},
                {"accessKeyId", entry.accessKeyId},
                {"secretAccessKey", entry.secretAccessKey},
                {"isAdmin", entry.isAdmin},
                {"namespace", entry.nameSpace},
                {"baseUrl", entry.baseUrl},
        };

        std::ofstream out(path, std::ios::trunc);
        if (!out) throw EuclidError("failed to open " + path.string() + " for writing");
        out << boost::json::serialize(body);
        out.close();

        // Best effort: not every filesystem has POSIX permissions, and a credentials file that
        // exists is better than a login that failed over its mode.
        std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, ec);
    }

    void Credentials::UpdateNamespace(const std::string &baseUrl, const std::string &nameSpace) {
        const auto cached = Load();
        if (!cached.has_value() || cached->baseUrl != baseUrl) return;
        Entry entry = *cached;
        entry.nameSpace = nameSpace;
        Save(entry);
    }

    void Credentials::Clear() {
        std::error_code ec;
        std::filesystem::remove(FilePath(), ec);
    }

    bool Credentials::IsTokenValid(const std::string &token) {

        const auto firstDot = token.find('.');
        if (firstDot == std::string::npos) return false;
        const auto secondDot = token.find('.', firstDot + 1);
        const auto payload = token.substr(firstDot + 1, secondDot == std::string::npos ? std::string::npos : secondDot - firstDot - 1);

        const auto decoded = Crypto::Base64Decode(payload);
        if (decoded.empty()) return false;

        const auto claims = Json::Parse(decoded);
        if (!claims.is_object()) return false;

        const auto expiry = Json::Number(claims, "exp", 0);
        if (expiry == 0) return false;

        const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return now < expiry;
    }

}// namespace Euclid::CDK
