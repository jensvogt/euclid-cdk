// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/Json.h>

namespace Euclid::CDK {

    namespace {

        // The field, or nullptr when the containing value is not an object or does not carry it.
        const boost::json::value *field(const boost::json::value &value, const std::string &name) {
            if (!value.is_object()) return nullptr;
            return value.as_object().if_contains(name);
        }

    }// namespace

    boost::json::object Json::Object(const boost::json::value &value) {
        return value.is_object() ? value.as_object() : boost::json::object{};
    }

    std::string Json::Text(const boost::json::value &value, const std::string &name) {
        const auto *found = field(value, name);
        return found != nullptr && found->is_string() ? std::string(found->as_string()) : std::string{};
    }

    long Json::Number(const boost::json::value &value, const std::string &name, const long defaultValue) {
        const auto *found = field(value, name);
        if (found == nullptr) return defaultValue;
        if (found->is_int64()) return static_cast<long>(found->as_int64());
        if (found->is_uint64()) return static_cast<long>(found->as_uint64());
        if (found->is_double()) return static_cast<long>(found->as_double());
        return defaultValue;
    }

    bool Json::Flag(const boost::json::value &value, const std::string &name, const bool defaultValue) {
        const auto *found = field(value, name);
        return found != nullptr && found->is_bool() ? found->as_bool() : defaultValue;
    }

    std::vector<std::string> Json::Strings(const boost::json::value &value, const std::string &name) {
        std::vector<std::string> strings;
        const auto *found = field(value, name);
        if (found == nullptr || !found->is_array()) return strings;
        for (const auto &element: found->as_array()) {
            if (element.is_string()) strings.emplace_back(element.as_string());
        }
        return strings;
    }

    std::vector<boost::json::value> Json::Documents(const boost::json::value &value, const std::string &name) {
        std::vector<boost::json::value> documents;
        const auto *found = field(value, name);
        if (found == nullptr || !found->is_array()) return documents;
        for (const auto &element: found->as_array()) {
            if (element.is_object()) documents.push_back(element);
        }
        return documents;
    }

    boost::json::value Json::Child(const boost::json::value &value, const std::string &name) {
        const auto *found = field(value, name);
        return found == nullptr ? boost::json::value{} : *found;
    }

    boost::json::value Json::Parse(const std::string &body) {
        if (body.empty()) return {};
        boost::system::error_code ec;
        auto parsed = boost::json::parse(body, ec);
        return ec ? boost::json::value{} : parsed;
    }

}// namespace Euclid::CDK
