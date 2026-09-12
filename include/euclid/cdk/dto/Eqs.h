// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <map>
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/dto/Com.h>
#include <euclid/cdk/dto/Page.h>

/**
 * @file
 * @brief The shapes EQS sends back, and the readers that parse them.
 *
 * @par
 * Structs rather than raw boost::json::value, as everywhere in this SDK. Field names are the
 * server's own (dto/include/euclid/dto/eqs in the euclid repository), and so are the type names -
 * EQS::Message is what the server calls Dto::EQS::Message. ENS has the same words for different
 * things: a topic's message has no receipt handle and no visibility, because nothing leases it. The
 * server keeps the two apart, so this does too, and the namespace is what tells them apart here.
 */

namespace Euclid::CDK::EQS {

    /**
     * @brief A queue, and the counters that say what is in it.
     *
     * @par
     * The three counts are the states a message can be in: "available" is waiting to be received,
     * "invisible" is out with a consumer whose visibility timeout has not expired, and "delayed" is
     * not yet due. They are what a consumer's backlog actually looks like - "size" is bytes.
     */
    struct EUCLID_CDK_API Queue {
        std::string name;
        std::string owner;
        std::string ern;
        std::map<std::string, std::string> tags;
        long size{};
        long delay{};
        long available{};
        long delayed{};
        long invisible{};
        long visibility{};
        long maxMessageLength{};
        long maxReceiveCount{};

        /**
         * @brief The dead letter queue messages go to once they have been received maxReceiveCount
         * times. Sent as "deadLetterQueueArn" - an "arn" the server has never renamed.
         */
        std::string deadLetterQueueErn;
        std::string priority;

        /**
         * @brief "AVAILABLE" or "STOPPED"; a stopped queue hands nothing out - see
         * EQS::Eqs::StopQueue().
         */
        std::string status;

        /**
         * @brief One of euclid's own queues rather than somebody's. Left out of a listing unless
         * asked for.
         */
        bool internal{};
        std::string created;
        std::string modified;
    };

    /**
     * @brief One message on a queue.
     *
     * @par
     * "receiptHandle" is the lease a receive hands out: it is what EQS::Eqs::DeleteMessage() takes,
     * and it stops working once the visibility timeout expires and the message goes back on the
     * queue.
     *
     * @par
     * Two attribute maps, as everywhere in euclid: "attributes" are the sender's own, and
     * "systemAttributes" are euclid's envelope, which is how a message that has hopped through a
     * bucket or a topic still carries what it was sent with.
     */
    struct EUCLID_CDK_API Message {
        std::string ern;
        std::string queueErn;
        std::string messageId;
        std::string status;
        std::string priority;
        std::string body;
        std::string receiptHandle;
        long size{};
        long receivedCount{};
        std::string contentType;
        COM::VariantMap attributes;
        COM::VariantMap systemAttributes;
        std::string lastReceived;
        std::string created;
        std::string modified;
    };

    /**
     * @brief One attribute of one message, as the server stored it.
     */
    struct EUCLID_CDK_API MessageAttribute {
        std::string messageId;
        std::string name;
        COM::Variant value;
    };

    /**
     * @brief A newly created queue: its name, and the ERN everything else names it by.
     */
    struct EUCLID_CDK_API CreateQueueResult {
        std::string name;
        std::string ern;
    };

    /**
     * @brief Where a queue lives and how much is in it.
     */
    struct EUCLID_CDK_API QueueMetadata {
        std::string region;
        std::string accountId;
        std::string owner;
        std::string nameSpace;
        std::string name;
        std::string ern;
        long size{};
        long messages{};
    };

    /**
     * @brief How many messages a queue holds, by the state they are in.
     */
    struct EUCLID_CDK_API MessageCount {
        std::string ern;
        long available{};
        long delayed{};
        long invisible{};
        long total{};
    };

    /**
     * @brief Everything about one message except its body.
     *
     * @par
     * "receivedCount" against the queue's maxReceiveCount is what decides when a message is moved to
     * the dead letter queue, so this is where a message that keeps coming back explains itself.
     */
    struct EUCLID_CDK_API MessageMetadata {
        std::string messageId;
        std::string queueErn;
        std::string receiptHandle;
        std::string status;
        std::string priority;
        long size{};
        long receivedCount{};
        long visibilityTimeout{};
        std::string contentType;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A queue's status after starting or stopping it, and how many messages are waiting on
     * it.
     */
    struct EUCLID_CDK_API QueueStatusResult {
        std::string ern;
        std::string status;
        long available{};
    };

    /**
     * @brief A queue's message-length limit after setting it, and what a send is measured against.
     *
     * @par
     * The two differ for a queue carrying no limit of its own - "maxMessageLength" zero, which is
     * what a create-queue that omitted the field stored. A send is measured against the
     * installation's figure in that case rather than refusing everything, and
     * "effectiveMaxMessageLength" is that figure.
     */
    struct EUCLID_CDK_API MaxMessageLengthResult {
        std::string ern;
        long maxMessageLength{};
        long effectiveMaxMessageLength{};
    };

    /**
     * @brief One queue a redrive put messages back on, and how many went there.
     */
    struct EUCLID_CDK_API RedriveTarget {
        std::string queueErn;
        long messages{};
    };

    /**
     * @brief What a redrive moved, where it went, and what it left behind.
     *
     * @par
     * "remaining" is not a failure: several queues can share a dead letter queue, and a message that
     * predates the recording of its origin has no answer to the question of where it came from. It
     * is left alone rather than guessed at, and "note" is the server saying so.
     */
    struct EUCLID_CDK_API RedriveDlqResult {
        std::string ern;
        long messages{};
        long remaining{};
        std::vector<RedriveTarget> targets;
        std::string note;
    };

    /**
     * @brief Reads a queue.
     */
    [[nodiscard]]
    EUCLID_CDK_API Queue ToQueue(const boost::json::value &value);

    /**
     * @brief Reads a message.
     */
    [[nodiscard]]
    EUCLID_CDK_API Message ToMessage(const boost::json::value &value);

    /**
     * @brief Reads a message attribute.
     */
    [[nodiscard]]
    EUCLID_CDK_API MessageAttribute ToMessageAttribute(const boost::json::value &value);

    /**
     * @brief Reads a create-queue response.
     */
    [[nodiscard]]
    EUCLID_CDK_API CreateQueueResult ToCreateQueueResult(const boost::json::value &value);

    /**
     * @brief Reads a get-queue-metadata response.
     */
    [[nodiscard]]
    EUCLID_CDK_API QueueMetadata ToQueueMetadata(const boost::json::value &value);

    /**
     * @brief Reads a get-message-count response.
     */
    [[nodiscard]]
    EUCLID_CDK_API MessageCount ToMessageCount(const boost::json::value &value);

    /**
     * @brief Reads a get-message-metadata response.
     */
    [[nodiscard]]
    EUCLID_CDK_API MessageMetadata ToMessageMetadata(const boost::json::value &value);

    /**
     * @brief Reads a start-queue or stop-queue response.
     */
    [[nodiscard]]
    EUCLID_CDK_API QueueStatusResult ToQueueStatusResult(const boost::json::value &value);

    /**
     * @brief Reads a set-queue-max-message-length response.
     */
    [[nodiscard]]
    EUCLID_CDK_API MaxMessageLengthResult ToMaxMessageLengthResult(const boost::json::value &value);

    /**
     * @brief Reads one queue a redrive put messages back on.
     */
    [[nodiscard]]
    EUCLID_CDK_API RedriveTarget ToRedriveTarget(const boost::json::value &value);

    /**
     * @brief Reads a redrive-dlq response.
     */
    [[nodiscard]]
    EUCLID_CDK_API RedriveDlqResult ToRedriveDlqResult(const boost::json::value &value);

}// namespace Euclid::CDK::EQS
