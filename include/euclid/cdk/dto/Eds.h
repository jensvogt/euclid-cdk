// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>

/**
 * @file
 * @brief The shapes EDS sends back, and the readers that parse them.
 *
 * @par
 * This is the third module whose wire spells the namespace "namespace"; EAP, ETS, EAG and EAD are
 * the others.
 */

namespace Euclid::CDK::EDS {

    /**
     * @brief A zone this installation answers for.
     */
    struct EUCLID_CDK_API Zone {

        /**
         * @brief The zone's domain name, normalised - lower case and without a trailing dot.
         */
        std::string name;
        std::string ern;
        std::string nameSpace;

        /**
         * @brief The TTL a record of this zone takes when it names none of its own, in seconds.
         */
        long ttl{};
        std::string description;

        /**
         * @brief How many record sets the zone holds.
         */
        long recordCount{};

        /**
         * @brief The zone's serial, derived from when it was last modified.
         *
         * @par
         * What a secondary compares to decide whether it needs to transfer again, so it moves
         * whenever anything in the zone does.
         */
        long serial{};
        std::string created;
        std::string modified;
    };

    /**
     * @brief One record set: every value held at one name for one type.
     *
     * @par
     * A set rather than a record, which is what DNS actually stores - three addresses for one name
     * are one A set with three values, not three records. PutRecord() replaces the whole set, and
     * DeleteRecord() removes all of it.
     */
    struct EUCLID_CDK_API Record {

        /**
         * @brief The zone this belongs to.
         */
        std::string zone;

        /**
         * @brief The owner name, fully qualified.
         */
        std::string name;

        /**
         * @brief TypeA, TypeAaaa, TypeCname and the rest.
         */
        std::string type;
        long ttl{};

        /**
         * @brief The values held at this name, in their presentation form - an address for an A,
         * a target for a CNAME, "10 mail.example.com" for an MX.
         */
        std::vector<std::string> values;
        std::string created;
        std::string modified;
    };

    /**
     * @brief One record as a DNS answer carries it - a single value rather than a set.
     */
    struct EUCLID_CDK_API AnswerRecord {
        std::string name;
        std::string type;
        long ttl{};

        /**
         * @brief The value itself. An answer names one per record, where a stored set names all of
         * them together.
         */
        std::string value;
    };

    /**
     * @brief What the resolver would answer for a name, without asking it over DNS.
     *
     * @par
     * The same resolution the DNS port performs, through the API and with credentials - which is
     * what makes it answerable from a test, where a real query would need a resolver pointed at
     * the right port.
     */
    struct EUCLID_CDK_API Answer {
        std::string name;
        std::string type;

        /**
         * @brief Whether this installation is authoritative for the name.
         *
         * @par
         * False means it holds no zone covering it, and the answer is RcodeRefused with nothing in
         * it - this call does not recurse upstream the way the DNS port will.
         */
        bool authoritative{};

        /**
         * @brief The zone the answer came from, empty when there is none.
         */
        std::string zone;

        /**
         * @brief RcodeNoError, RcodeNxDomain or RcodeRefused.
         */
        std::string rcode;

        /**
         * @brief The records answering the question.
         */
        std::vector<AnswerRecord> answers;

        /**
         * @brief What the authority section carries - the zone's own records, which is where a
         * negative answer's SOA appears.
         */
        std::vector<AnswerRecord> authority;
    };

    /**
     * @brief Reads a zone.
     */
    [[nodiscard]]
    EUCLID_CDK_API Zone ToZone(const boost::json::value &value);

    /**
     * @brief Reads a record set.
     */
    [[nodiscard]]
    EUCLID_CDK_API Record ToRecord(const boost::json::value &value);

    /**
     * @brief Reads one record of an answer.
     */
    [[nodiscard]]
    EUCLID_CDK_API AnswerRecord ToAnswerRecord(const boost::json::value &value);

    /**
     * @brief Reads a get-answer response.
     */
    [[nodiscard]]
    EUCLID_CDK_API Answer ToAnswer(const boost::json::value &value);

}// namespace Euclid::CDK::EDS
