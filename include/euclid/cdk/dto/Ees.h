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
 * @brief The shapes EES sends back, and the readers that parse them.
 *
 * @par
 * An event's payload is left as JSON rather than parsed into anything: its shape belongs to whatever
 * module published it, and a reader here would have to know all of them. CDK::Json is how it is read.
 */

namespace Euclid::CDK::EES {

    /**
     * @brief One standing interest in one kind of event.
     *
     * @par
     * A subscriber names itself and gets one of these per event type it asked for, so a subscriber
     * watching three kinds of event has three.
     */
    struct EUCLID_CDK_API Subscription {

        /**
         * @brief The name the subscriber calls itself, which is what every other call here names.
         */
        std::string subscriber;
        std::string eventType;

        /**
         * @brief What narrows the event type, or empty for every event of it.
         */
        boost::json::object filter;
        std::string accountId;

        /**
         * @brief ModeDurable or ModeLive.
         */
        std::string mode;
        std::string created;

        /**
         * @brief When this subscriber last collected anything.
         */
        std::string lastSeen;
    };

    /**
     * @brief One event, as it was published and as it is delivered.
     */
    struct EUCLID_CDK_API Event {

        /**
         * @brief What Ees::AckEvents() takes to say this one is done with.
         */
        std::string eventId;
        std::string eventType;

        /**
         * @brief The module that published it - "esm", "eqs", and so on.
         */
        std::string sourceModule;

        /**
         * @brief The event's body, whose shape belongs to sourceModule rather than to EES.
         */
        boost::json::value payload;

        /**
         * @brief How many times delivery of this event has been attempted, this one included.
         *
         * @par
         * Above one means a previous delivery was claimed and never acknowledged, so the visibility
         * timeout returned it. Worth reading by anything whose handling is not idempotent.
         */
        long attempts{};
        std::string created;
    };

    /**
     * @brief A subscriber's standing interests, and how much is waiting for it.
     */
    struct EUCLID_CDK_API SubscriptionsResult {
        std::vector<Subscription> subscriptions;

        /**
         * @brief Events claimable right now - the backlog, not a total ever delivered.
         */
        long waiting{};
    };

    /**
     * @brief What a receive handed over.
     */
    struct EUCLID_CDK_API EventsResult {
        std::vector<Event> events;
        long total{};
    };

    /**
     * @brief What unsubscribing removed.
     */
    struct EUCLID_CDK_API UnsubscribeResult {
        std::string subscriber;

        /**
         * @brief How many subscriptions went: one when an event type was named, and all of the
         * subscriber's when none was.
         */
        long removed{};
    };

    /**
     * @brief What an acknowledgement came to.
     *
     * @par
     * "acknowledged" can be smaller than the number of ids sent without anything being wrong: an
     * event already acknowledged, or one whose retention ran out, is not there to acknowledge again
     * and means the same thing to a caller either way.
     */
    struct EUCLID_CDK_API AckResult {
        std::string subscriber;
        long acknowledged{};

        /**
         * @brief What is still waiting after this.
         */
        long waiting{};
    };

    /**
     * @brief Reads a subscription.
     */
    [[nodiscard]]
    EUCLID_CDK_API Subscription ToSubscription(const boost::json::value &value);

    /**
     * @brief Reads an event.
     */
    [[nodiscard]]
    EUCLID_CDK_API Event ToEvent(const boost::json::value &value);

    /**
     * @brief Reads a subscribe-events or list-subscriptions response.
     */
    [[nodiscard]]
    EUCLID_CDK_API SubscriptionsResult ToSubscriptionsResult(const boost::json::value &value);

    /**
     * @brief Reads a receive-events response.
     */
    [[nodiscard]]
    EUCLID_CDK_API EventsResult ToEventsResult(const boost::json::value &value);

    /**
     * @brief Reads an unsubscribe-events response.
     */
    [[nodiscard]]
    EUCLID_CDK_API UnsubscribeResult ToUnsubscribeResult(const boost::json::value &value);

    /**
     * @brief Reads an ack-events response.
     */
    [[nodiscard]]
    EUCLID_CDK_API AckResult ToAckResult(const boost::json::value &value);

}// namespace Euclid::CDK::EES
