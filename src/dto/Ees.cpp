// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Ees.h>

namespace Euclid::CDK::EES {

    Subscription ToSubscription(const boost::json::value &value) {
        return {
                .subscriber = Json::Text(value, "subscriber"),
                .eventType = Json::Text(value, "eventType"),
                .filter = Json::Object(Json::Child(value, "filter")),
                .accountId = Json::Text(value, "accountId"),
                .mode = Json::Text(value, "mode"),
                .created = Json::Text(value, "created"),
                .lastSeen = Json::Text(value, "lastSeen"),
        };
    }

    Event ToEvent(const boost::json::value &value) {
        return {
                .eventId = Json::Text(value, "eventId"),
                .eventType = Json::Text(value, "eventType"),
                .sourceModule = Json::Text(value, "sourceModule"),
                // Left as it came: the payload's shape belongs to whichever module published it.
                .payload = Json::Child(value, "payload"),
                .attempts = Json::Number(value, "attempts"),
                .created = Json::Text(value, "created"),
        };
    }

    SubscriptionsResult ToSubscriptionsResult(const boost::json::value &value) {

        SubscriptionsResult result{.subscriptions = {}, .waiting = Json::Number(value, "waiting")};
        for (const auto &document: Json::Documents(value, "subscriptions")) {
            result.subscriptions.push_back(ToSubscription(document));
        }
        return result;
    }

    EventsResult ToEventsResult(const boost::json::value &value) {

        EventsResult result{.events = {}, .total = Json::Number(value, "total")};
        for (const auto &document: Json::Documents(value, "events")) {
            result.events.push_back(ToEvent(document));
        }
        return result;
    }

    UnsubscribeResult ToUnsubscribeResult(const boost::json::value &value) {
        return {.subscriber = Json::Text(value, "subscriber"), .removed = Json::Number(value, "removed")};
    }

    AckResult ToAckResult(const boost::json::value &value) {
        return {
                .subscriber = Json::Text(value, "subscriber"),
                .acknowledged = Json::Number(value, "acknowledged"),
                .waiting = Json::Number(value, "waiting"),
        };
    }

}// namespace Euclid::CDK::EES
