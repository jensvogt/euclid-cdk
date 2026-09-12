// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>

namespace Euclid::CDK {

    /**
     * @brief Defensive readers for the JSON the server sends back.
     *
     * @par
     * Every one of these answers a usable value for a field that is absent, null or of the wrong
     * type, rather than throwing. A response is a wire format from another process and another
     * release: a server that starts sending null where it used to send a string should cost a
     * caller an empty string, not an exception from inside a parse of something it was not even
     * looking at. What a caller genuinely cannot proceed without is checked by the caller.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API Json {
    public:

        /**
         * @brief A value as an object, or an empty object when it is not one.
         *
         * @param value the value.
         * @return the object.
         */
        [[nodiscard]]
        static boost::json::object Object(const boost::json::value &value);

        /**
         * @brief A string field.
         *
         * @param value the containing value.
         * @param name  field name.
         * @return the string, or empty when the field is absent or not a string.
         */
        [[nodiscard]]
        static std::string Text(const boost::json::value &value, const std::string &name);

        /**
         * @brief A numeric field.
         *
         * @param value        the containing value.
         * @param name         field name.
         * @param defaultValue what to answer when the field is absent or not a number.
         * @return the number.
         */
        [[nodiscard]]
        static long Number(const boost::json::value &value, const std::string &name, long defaultValue = 0);

        /**
         * @brief A boolean field.
         *
         * @param value        the containing value.
         * @param name         field name.
         * @param defaultValue what to answer when the field is absent or not a boolean.
         * @return the flag.
         */
        [[nodiscard]]
        static bool Flag(const boost::json::value &value, const std::string &name, bool defaultValue = false);

        /**
         * @brief An array-of-strings field.
         *
         * @param value the containing value.
         * @param name  field name.
         * @return the strings, skipping any element that is not one; empty when the field is
         * absent or not an array.
         */
        [[nodiscard]]
        static std::vector<std::string> Strings(const boost::json::value &value, const std::string &name);

        /**
         * @brief An array-of-objects field.
         *
         * @param value the containing value.
         * @param name  field name.
         * @return the elements, skipping any that is not an object; empty when the field is absent
         * or not an array.
         */
        [[nodiscard]]
        static std::vector<boost::json::value> Documents(const boost::json::value &value, const std::string &name);

        /**
         * @brief A nested object field.
         *
         * @param value the containing value.
         * @param name  field name.
         * @return the nested value, or null when the field is absent.
         */
        [[nodiscard]]
        static boost::json::value Child(const boost::json::value &value, const std::string &name);

        /**
         * @brief Parses a response body.
         *
         * @param body the body.
         * @return the parsed value, or null when the body is empty or is not JSON - which is what a
         * proxy's error page is.
         */
        [[nodiscard]]
        static boost::json::value Parse(const std::string &body);
    };

}// namespace Euclid::CDK
