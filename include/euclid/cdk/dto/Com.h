// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>

/**
 * @file
 * @brief The shapes more than one euclid module speaks.
 *
 * @par
 * Variant is here rather than in the module that needed it first for the same reason the server
 * keeps it in Euclid::Dto::COM: a queue message attribute, a topic message attribute and a storage
 * object attribute are the same typed value on the wire.
 *
 * @par
 * Subscription is here for a plainer reason - ESM and ENS answer with the same four fields, because
 * both are saying the same thing: what arrives at this source goes to that target from now on.
 */

namespace Euclid::CDK::COM {

    /**
     * @brief Raw bytes, as a "binary" variant holds them.
     */
    using Binary = std::vector<unsigned char>;

    /**
     * @brief What a message is delivered at, and the only three values euclid accepts.
     */
    inline constexpr std::string_view PriorityLow = "LOW";
    inline constexpr std::string_view PriorityMedium = "MEDIUM";
    inline constexpr std::string_view PriorityHigh = "HIGH";

    /**
     * @brief What a subscription delivers to: a queue...
     *
     * @par
     * Shared by ESM and ENS because the server's subscription is: both name a source, a target and
     * which of the two modules that target lives in.
     */
    inline constexpr std::string_view Queue = "SQS";

    /**
     * @brief ...or a topic. The two name different modules, and this is what says which.
     */
    inline constexpr std::string_view Topic = "SNS";

    /**
     * @brief A typed value: what it is, and what it holds.
     *
     * @par
     * A port of the server's Euclid::Dto::COM::Variant, with the same seven alternatives in the same
     * order. The type tag is what makes the round trip lossless: JSON has one number type and euclid
     * has several, so an attribute stored as a long would come back as a double - or as an int,
     * depending on the reader - if the type travelled only in the shape of the value.
     *
     * @par
     * The converting constructor is not explicit, which is what lets an attribute map be written as
     * @code {{"author", "euclid-cdk"}, {"revision", 1L}} @endcode - the C++ spelling of what
     * euclid-pdk calls Variant.of() and euclid-ndk variantOf(). It tags what it is given rather than
     * what the value could be widened to, so 1 is an int and 1L is a long; a value that has to carry
     * a particular tag is written with that alternative's own type.
     *
     * @par
     * "binary" travels as base64 and is held here as bytes, so a caller never sees the encoding.
     *
     * @author jensvogt47\@gmail.com
     */
    struct EUCLID_CDK_API Variant {

        std::variant<int, long, double, float, bool, std::string, Binary> value;

        Variant() = default;

        template<typename T>
            requires(!std::is_same_v<std::decay_t<T>, Variant> && std::is_constructible_v<decltype(value), T>)
        Variant(T &&v) : value(std::forward<T>(v)) {}// NOLINT(*-explicit-constructor) - see above

        /**
         * @brief Whether the value currently holds a T.
         */
        template<typename T>
        [[nodiscard]] bool Holds() const {
            return std::holds_alternative<T>(value);
        }

        /**
         * @brief The value as a T.
         *
         * @throws std::bad_variant_access if it does not hold a T - Holds() is the question that
         * does not throw.
         */
        template<typename T>
        [[nodiscard]] const T &Get() const {
            return std::get<T>(value);
        }

        /**
         * @brief The type tag this variant travels under: "int", "long", "double", "float", "bool",
         * "string" or "binary".
         */
        [[nodiscard]]
        std::string_view Type() const;

        /**
         * @brief This variant as the server reads it, with a "binary" value encoded as base64.
         */
        [[nodiscard]]
        boost::json::object ToJson() const;

        /**
         * @brief Whatever this holds, written out as text - for logging and for the one-line print.
         *
         * @par
         * Lossy on purpose: binary comes back as its base64, and a double as the shortest text that
         * reads back as itself. Anything that cares which type it has asks Holds().
         */
        [[nodiscard]]
        std::string ToString() const;

        friend bool operator==(const Variant &lhs, const Variant &rhs) = default;
    };

    /**
     * @brief Attributes keyed by name, which is how every module that has them carries them.
     */
    using VariantMap = std::map<std::string, Variant>;

    /**
     * @brief Reads one variant as the server sent it.
     *
     * @param value the "{type, value}" object.
     * @return the variant; an unreadable one reads as an empty string, rather than throwing out the
     * object the attribute hung off.
     */
    [[nodiscard]]
    EUCLID_CDK_API Variant ToVariant(const boost::json::value &value);

    /**
     * @brief Reads an object of variants keyed by name.
     *
     * @param value the containing value.
     * @param name  the field holding the map, e.g. "attributes".
     * @return the attributes, empty when the field is absent or is not an object.
     */
    [[nodiscard]]
    EUCLID_CDK_API VariantMap ToVariantMap(const boost::json::value &value, const std::string &name);

    /**
     * @brief An attribute map as the server reads it.
     */
    [[nodiscard]]
    EUCLID_CDK_API boost::json::object VariantMapToJson(const VariantMap &attributes);

    /**
     * @brief A standing instruction to deliver what arrives at a source onward to a target.
     *
     * @par
     * One shape for both modules that have them: a bucket's events going to a queue (ESM) and a
     * topic's messages going to a queue (ENS) differ in what is delivered, not in how the
     * instruction is described. "type" is Queue or Topic, and "ern" is the subscription's own -
     * which is what removing it takes.
     */
    struct EUCLID_CDK_API Subscription {
        std::string ern;
        std::string sourceErn;
        std::string type;
        std::string targetErn;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A new subscription, as the "subscribe" actions answer with it: the same, minus the
     * timestamps.
     */
    struct EUCLID_CDK_API SubscribeResult {
        std::string ern;
        std::string sourceErn;
        std::string type;
        std::string targetErn;
    };

    /**
     * @brief Reads a subscription.
     */
    [[nodiscard]]
    EUCLID_CDK_API Subscription ToSubscription(const boost::json::value &value);

    /**
     * @brief Reads a subscribe response.
     */
    [[nodiscard]]
    EUCLID_CDK_API SubscribeResult ToSubscribeResult(const boost::json::value &value);

}// namespace Euclid::CDK::COM
