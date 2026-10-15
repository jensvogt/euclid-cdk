// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <thread>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/ees/Ees.h>

namespace Euclid::CDK::EES {

    namespace {

        /**
         * @brief A JSON array of strings, for the payload fields that take a list.
         */
        boost::json::array stringsOf(const std::vector<std::string> &values) {
            boost::json::array array;
            for (const auto &value: values) array.emplace_back(value);
            return array;
        }

    }// namespace

    Ees::Ees(const EAM::Session &session) : ModuleClient(session, std::string(EES::Target)) {}

    // -- subscribing --------------------------------------------------------------------------------

    SubscriptionsResult Ees::Subscribe(const std::string &subscriber, const std::vector<std::string> &eventTypes,
                                       const SubscribeOptions &options) const {

        // Refused here rather than on arrival: a subscription to nothing is a subscriber that will
        // wait forever for events it never asked for, and the server's 400 costs a round trip to
        // say so.
        if (eventTypes.empty()) {
            throw EuclidError("a subscription has to name at least one event type - see EES::ObjectCreated and its siblings");
        }

        boost::json::object payload{
                // "name" on the wire, and the subscriber everywhere else in this module.
                {"name", subscriber},
                {"eventTypes", stringsOf(eventTypes)},
                {"filter", options.filter},
        };
        // Left out when empty rather than sent empty: the server reads an absent mode as durable and
        // refuses one it cannot parse, so an empty string would be a 400 rather than the default.
        if (!options.mode.empty()) payload["mode"] = options.mode;

        return ToSubscriptionsResult(Call("subscribe-events", payload));
    }

    UnsubscribeResult Ees::Unsubscribe(const std::string &subscriber, const std::string &eventType) const {
        // The event type is sent as it came, empty included: empty is what removes the subscriber
        // entirely rather than narrowing it, which is a thing a caller asks for on purpose.
        const boost::json::object payload{{"name", subscriber}, {"eventType", eventType}};
        return ToUnsubscribeResult(Call("unsubscribe-events", payload));
    }

    SubscriptionsResult Ees::ListSubscriptions(const std::string &subscriber) const {
        return ToSubscriptionsResult(Call("list-subscriptions", {{"name", subscriber}}));
    }

    // -- collecting ---------------------------------------------------------------------------------

    EventsResult Ees::ReceiveEvents(const std::string &subscriber, const ReceiveOptions &options) const {

        if (options.waitTime <= std::chrono::seconds::zero()) {
            return Receive(subscriber, options, std::chrono::seconds::zero());
        }

        // Asked for again until the caller's own deadline, because one request cannot wait longer
        // than MaxWaitTime however long it asks for - the server clamps it. A caller asking for a
        // minute means a minute, and getting twenty seconds and an empty answer is not that. The
        // same loop EQS::Eqs::ReceiveMessages() runs, for the same reason.
        const auto deadline = std::chrono::steady_clock::now() + options.waitTime;
        for (;;) {

            // Rounded up rather than truncated: the wait travels in whole seconds, and a caller who
            // asked for five would otherwise be given four and a round trip to ask for the fifth.
            const auto left = deadline - std::chrono::steady_clock::now();
            const auto wait = std::max(std::chrono::seconds(1), std::chrono::ceil<std::chrono::seconds>(left));

            auto received = Receive(subscriber, options, std::min(wait, MaxWaitTime));
            if (!received.events.empty()) return received;

            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= HonouredWaitTolerance) return received;

            // Answered early because the server had no long-poll slot free, which is the moment to
            // ask less often rather than more.
            std::this_thread::sleep_for(std::min(std::chrono::duration_cast<std::chrono::milliseconds>(remaining), SlotsBusyBackoff));
        }
    }

    AckResult Ees::AckEvents(const std::string &subscriber, const std::vector<std::string> &eventIds) const {

        // Refused here for the same reason an empty subscription is: there is nothing for the server
        // to do and it answers 400, which is a round trip to learn what is plain from the argument.
        if (eventIds.empty()) {
            throw EuclidError("acknowledging nothing is not an acknowledgement - pass the eventIds that were handled");
        }

        const boost::json::object payload{{"name", subscriber}, {"eventIds", stringsOf(eventIds)}};
        return ToAckResult(Call("ack-events", payload));
    }

    // -- monitoring ---------------------------------------------------------------------------------

    boost::json::object Ees::Metrics() const {
        return Call("get-metrics");
    }

    EventsResult Ees::Receive(const std::string &subscriber, const ReceiveOptions &options,
                              const std::chrono::seconds waitTime) const {

        const boost::json::object payload{
                {"name", subscriber},
                {"maxEvents", options.maxEvents},
                {"waitTime", waitTime.count()},
                {"visibilityTimeout", options.visibilityTimeout.count()},
        };

        // The transport has to outlast the server's wait, or the client gives up on a request that
        // was about to answer. Zero leaves the client's own timeout alone, which is right for a
        // receive that is not waiting at all.
        const auto timeout = waitTime > std::chrono::seconds::zero()
                                     ? std::chrono::duration_cast<std::chrono::milliseconds>(waitTime) + LongPollResponseMargin
                                     : std::chrono::milliseconds::zero();

        return ToEventsResult(Result("receive-events", Post("receive-events", payload, {}, timeout)));
    }

}// namespace Euclid::CDK::EES
