// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <string>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/dto/Com.h>
#include <euclid/cdk/dto/Page.h>

/**
 * @file
 * @brief The shapes ENS sends back, and the readers that parse them.
 *
 * @par
 * Where a type here looks like one in dto/Eqs.h, it is not the same type: a topic's message has no
 * receipt handle and no visibility, because nothing leases it - it is delivered to the topic's
 * subscribers and kept as a record of that. The server keeps them apart for the same reason, so this
 * does too.
 *
 * @par
 * A subscription is the exception and lives in dto/Com.h: ENS and ESM describe one the same way.
 */

namespace Euclid::CDK::ENS {

    /**
     * @brief A topic: what a publisher publishes to, and what subscriptions hang off.
     */
    struct EUCLID_CDK_API Topic {
        std::string name;
        std::string owner;
        std::string ern;
        std::map<std::string, std::string> tags;
        long size{};
        long messages{};
        long maxMessageLength{};

        /**
         * @brief "RUNNING" or "STOPPED" - see ENS::Ens::StopTopic(). A stopped topic still accepts
         * what is published to it; it holds it rather than fanning it out.
         */
        std::string status;

        /**
         * @brief How long a published message is kept, in seconds. Zero means this topic has never
         * been told what it wants and follows the installation's default as that changes.
         */
        long retentionPeriod{};
        std::string created;
        std::string modified;
    };

    /**
     * @brief One message published to a topic.
     */
    struct EUCLID_CDK_API Message {
        std::string ern;
        std::string topicErn;
        std::string messageId;
        std::string status;
        std::string body;
        std::string contentType;
        COM::VariantMap attributes;
        std::string lastReceived;
        std::string created;
        std::string modified;
    };

    /**
     * @brief One attribute of one published message.
     *
     * @par
     * The wire field is "key" here and "name" in EQS - the same thing under two names, which this
     * SDK reproduces rather than papers over, so that a request built from this documentation
     * matches what the server and euclid-cli exchange.
     */
    struct EUCLID_CDK_API MessageAttribute {
        std::string messageId;
        std::string key;
        COM::Variant value;
    };

    /**
     * @brief A newly created topic: its name, and the ERN everything else names it by.
     */
    struct EUCLID_CDK_API CreateTopicResult {
        std::string name;
        std::string ern;
    };

    /**
     * @brief Where a topic lives, how much has been published to it, and whether it is delivering.
     */
    struct EUCLID_CDK_API TopicMetadata {
        std::string region;
        std::string accountId;
        std::string owner;
        std::string nameSpace;
        std::string name;
        std::string ern;
        long size{};
        long messages{};

        /**
         * @brief "RUNNING" or "STOPPED".
         */
        std::string status;

        /**
         * @brief How long a published message is kept, in seconds; zero follows the installation's
         * default.
         */
        long retentionPeriod{};

        /**
         * @brief How many messages are waiting for this topic to be started again. Nothing but a
         * stopped topic has any, and the server only counts them for a topic that is stopped.
         */
        long held{};
    };

    /**
     * @brief A topic after being started or stopped, and what starting it let go.
     *
     * @par
     * "released" is how many held messages were delivered to the topic's subscriptions on the way -
     * zero for a stop, and zero for a start of a topic that was never stopped. It is delivery rather
     * than a promise of it: the messages went to the subscriptions as they went out.
     */
    struct EUCLID_CDK_API TopicStateResult {
        std::string ern;

        /**
         * @brief "RUNNING" or "STOPPED", as it now stands.
         */
        std::string status;
        long released{};
    };

    /**
     * @brief What a resend handed over, and what it passed by.
     *
     * @par
     * "held" is the number that says a resend was not the right command: those messages were
     * published while the topic was stopped and have never been delivered at all, so StartTopic()
     * is what releases them. A resend leaves them alone, because delivering one from here would
     * hand it over without marking it delivered and the next start would deliver it a second time.
     */
    struct EUCLID_CDK_API ResendResult {
        std::string ern;

        /**
         * @brief How many messages went to the topic's subscriptions again.
         */
        long resent{};

        /**
         * @brief How many were passed over as never having been delivered.
         */
        long held{};
    };

    /**
     * @brief A topic's retention period after setting it, in seconds. Zero means the installation's
     * own; -1 means the topic keeps everything published to it.
     */
    struct EUCLID_CDK_API TopicRetentionResult {
        std::string ern;
        long retentionPeriod{};
    };

    /**
     * @brief A topic's message counters - the server's own three, which are not a queue's.
     *
     * @par
     * A topic does not hold a backlog the way a queue does, so these count delivery rather than
     * state: what is on the topic, what has gone out to subscribers, and what had to go out again.
     */
    struct EUCLID_CDK_API MessageCount {
        std::string ern;
        long available{};
        long send{};
        long resend{};
    };

    /**
     * @brief Reads a topic.
     */
    [[nodiscard]]
    EUCLID_CDK_API Topic ToTopic(const boost::json::value &value);

    /**
     * @brief Reads a published message.
     */
    [[nodiscard]]
    EUCLID_CDK_API Message ToMessage(const boost::json::value &value);

    /**
     * @brief Reads a message attribute.
     */
    [[nodiscard]]
    EUCLID_CDK_API MessageAttribute ToMessageAttribute(const boost::json::value &value);

    /**
     * @brief Reads a create-topic response.
     */
    [[nodiscard]]
    EUCLID_CDK_API CreateTopicResult ToCreateTopicResult(const boost::json::value &value);

    /**
     * @brief Reads a get-topic-metadata response.
     */
    [[nodiscard]]
    EUCLID_CDK_API TopicMetadata ToTopicMetadata(const boost::json::value &value);

    /**
     * @brief Reads a start-topic or stop-topic response.
     */
    [[nodiscard]]
    EUCLID_CDK_API TopicStateResult ToTopicStateResult(const boost::json::value &value);

    /**
     * @brief Reads a resend-messages response.
     */
    EUCLID_CDK_API ResendResult ToResendResult(const boost::json::value &value);

    /**
     * @brief Reads a set-topic-retention response.
     */
    [[nodiscard]]
    EUCLID_CDK_API TopicRetentionResult ToTopicRetentionResult(const boost::json::value &value);

    /**
     * @brief Reads a get-message-count response.
     */
    [[nodiscard]]
    EUCLID_CDK_API MessageCount ToMessageCount(const boost::json::value &value);

}// namespace Euclid::CDK::ENS
