// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <array>
#include <charconv>
#include <string_view>

// Euclid includes
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/dto/Com.h>

namespace Euclid::CDK::COM {

    namespace {

        /**
         * @brief The tags the server writes, matched to this variant's alternatives by index.
         */
        constexpr std::string_view kInt = "int";
        constexpr std::string_view kLong = "long";
        constexpr std::string_view kDouble = "double";
        constexpr std::string_view kFloat = "float";
        constexpr std::string_view kBool = "bool";
        constexpr std::string_view kString = "string";
        constexpr std::string_view kBinary = "binary";

        /**
         * @brief A JSON number as a double, whichever of the three ways it arrived.
         */
        double numberOf(const boost::json::value &value) {
            if (value.is_double()) return value.as_double();
            if (value.is_int64()) return static_cast<double>(value.as_int64());
            if (value.is_uint64()) return static_cast<double>(value.as_uint64());
            return 0.0;
        }

        /**
         * @brief Bytes as a string_view, for the base64 encoder.
         */
        std::string_view bytesOf(const Binary &data) {
            return {reinterpret_cast<const char *>(data.data()), data.size()};
        }

        /**
         * @brief One alternative as the JSON value it travels as.
         *
         * @par
         * Spelled out per alternative rather than handed to boost::json::value's own conversions,
         * because a float is convertible to both double and bool and this is not a decision worth
         * leaving to overload resolution.
         */
        template<typename T>
        boost::json::value jsonValueOf(const T &held) {
            // boost::json::value takes its text as a string_view, and copies it as it is built - so
            // the encoded string below outlives the value that is made out of it.
            if constexpr (std::is_same_v<T, Binary>) return boost::json::value(boost::json::string_view(Crypto::Base64Encode(bytesOf(held))));
            else if constexpr (std::is_same_v<T, std::string>) return boost::json::value(boost::json::string_view(held));
            else if constexpr (std::is_same_v<T, bool>) return held;
            else if constexpr (std::is_same_v<T, int>) return static_cast<std::int64_t>(held);
            else if constexpr (std::is_same_v<T, long>) return static_cast<std::int64_t>(held);
            else return static_cast<double>(held);
        }

    }// namespace

    std::string_view Variant::Type() const {
        return std::visit([]<typename T>(const T &) -> std::string_view {
            if constexpr (std::is_same_v<T, int>) return kInt;
            else if constexpr (std::is_same_v<T, long>) return kLong;
            else if constexpr (std::is_same_v<T, double>) return kDouble;
            else if constexpr (std::is_same_v<T, float>) return kFloat;
            else if constexpr (std::is_same_v<T, bool>) return kBool;
            else if constexpr (std::is_same_v<T, std::string>) return kString;
            else return kBinary; }, value);
    }

    boost::json::object Variant::ToJson() const {
        return std::visit([this]<typename T>(const T &held) {
            return boost::json::object{{"type", std::string(Type())}, {"value", jsonValueOf(held)}}; }, value);
    }

    std::string Variant::ToString() const {
        return std::visit([]<typename T>(const T &held) -> std::string {
            if constexpr (std::is_same_v<T, std::string>) return held;
            else if constexpr (std::is_same_v<T, Binary>) return Crypto::Base64Encode(bytesOf(held));
            else if constexpr (std::is_same_v<T, bool>) return held ? "true" : "false";
            else if constexpr (std::is_integral_v<T>) return std::to_string(held);
            else {
                // std::to_chars rather than std::to_string: it writes the shortest text that reads
                // back as this exact double, where to_string would round it to six decimals.
                std::array<char, 32> buffer{};
                const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), held);
                return ec == std::errc{} ? std::string(buffer.data(), end) : std::string{};
            } }, value);
    }

    Variant ToVariant(const boost::json::value &value) {
        if (!value.is_object()) return {std::string{}};

        const auto type = Json::Text(value, "type");
        const auto held = Json::Child(value, "value");

        if (type == kInt) return {static_cast<int>(numberOf(held))};
        if (type == kLong) return {static_cast<long>(numberOf(held))};
        if (type == kDouble) return {numberOf(held)};
        if (type == kFloat) return {static_cast<float>(numberOf(held))};
        if (type == kBool) return {held.is_bool() && held.as_bool()};
        if (type == kString) return {held.is_string() ? std::string(held.as_string()) : std::string{}};
        if (type == kBinary) {
            const auto decoded = Crypto::Base64Decode(held.is_string() ? std::string(held.as_string()) : std::string{});
            return {Binary(decoded.begin(), decoded.end())};
        }

        // A tag this SDK does not know, or none at all. Answered as an empty string rather than
        // thrown, for the reason every reader here is defensive: a value from a later euclid should
        // cost the attribute it arrived in, not the object it hung off.
        return {std::string{}};
    }

    VariantMap ToVariantMap(const boost::json::value &value, const std::string &name) {
        VariantMap attributes;
        for (const auto child = Json::Child(value, name); const auto &[key, held]: Json::Object(child)) {
            attributes.emplace(key, ToVariant(held));
        }
        return attributes;
    }

    boost::json::object VariantMapToJson(const VariantMap &attributes) {
        boost::json::object json;
        for (const auto &[name, attribute]: attributes) {
            json[name] = attribute.ToJson();
        }
        return json;
    }

    Subscription ToSubscription(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .sourceErn = Json::Text(value, "sourceErn"),
                .type = Json::Text(value, "type"),
                .targetErn = Json::Text(value, "targetErn"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    SubscribeResult ToSubscribeResult(const boost::json::value &value) {
        return {
                .ern = Json::Text(value, "ern"),
                .sourceErn = Json::Text(value, "sourceErn"),
                .type = Json::Text(value, "type"),
                .targetErn = Json::Text(value, "targetErn"),
        };
    }

}// namespace Euclid::CDK::COM
