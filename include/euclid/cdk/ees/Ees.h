// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Ees.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EES {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "ees";

    /**
     * @brief Whether a subscription keeps events for a subscriber that is not listening.
     *
     * @par
     * ModeDurable is the default and almost always the right answer: a subscriber that asked for
     * events and was not connected when one arrived should find it waiting.
     */
    inline constexpr std::string_view ModeDurable = "durable";

    /**
     * @brief Events are delivered only while the subscriber is collecting, and dropped otherwise.
     *
     * @par
     * For a watcher rather than a worker - a dashboard that wants what is happening now and has no
     * use for what happened while it was shut. Chosen deliberately, because what it gives up is the
     * guarantee.
     */
    inline constexpr std::string_view ModeLive = "live";

    /**
     * @brief The event types euclid publishes, as "<module>.<thing>.<what happened>".
     *
     * @par
     * Not an exhaustive list, and deliberately not validated: a module newer than this SDK publishes
     * types this header has never heard of, and a subscription naming one has to be possible without
     * waiting for a release. Any string is accepted.
     */
    inline constexpr std::string_view ObjectCreated = "esm.object.created";
    inline constexpr std::string_view ObjectUpdated = "esm.object.updated";
    inline constexpr std::string_view ObjectDeleted = "esm.object.deleted";
    inline constexpr std::string_view BucketModified = "esm.bucket.modified";
    inline constexpr std::string_view BucketDeleted = "esm.bucket.deleted";
    inline constexpr std::string_view MessageSent = "eqs.message.sent";
    inline constexpr std::string_view MessagePublished = "ens.message.published";
    inline constexpr std::string_view KeyCreated = "ekm.key.created";
    inline constexpr std::string_view CertificateCreated = "ekm.certificate.created";
    inline constexpr std::string_view SecretCreated = "ess.secret.created";
    inline constexpr std::string_view SecretRotated = "ess.secret.rotated";
    inline constexpr std::string_view SecretDeleted = "ess.secret.deleted";

    /**
     * @brief How long a claimed event stays invisible to anybody else before it comes back.
     */
    inline constexpr std::chrono::seconds DefaultVisibilityTimeout{300};

    /**
     * @brief How many events one receive takes unless it says otherwise.
     */
    inline constexpr long DefaultMaxEvents = 10;

    /**
     * @brief The longest a receive will wait, whatever it asks for.
     *
     * @par
     * The server clamps to this rather than refusing, because the caller is holding a gateway worker
     * thread for the duration and that is not the caller's to decide.
     */
    inline constexpr std::chrono::seconds MaxWaitTime{20};

    /**
     * @brief How long the transport waits beyond the wait itself, so that a request about to answer
     * is not given up on.
     */
    inline constexpr std::chrono::milliseconds LongPollResponseMargin{10000};

    /**
     * @brief How long to hold off after a receive that answered early because the server had no
     * long-poll slot free - the moment to ask less often rather than more.
     */
    inline constexpr std::chrono::milliseconds SlotsBusyBackoff{500};

    /**
     * @brief How close to the caller's deadline counts as having waited the whole of it.
     */
    inline constexpr std::chrono::milliseconds HonouredWaitTolerance{250};

    /**
     * @brief What a subscription asks for beyond the event types themselves.
     */
    struct EUCLID_CDK_API SubscribeOptions {

        /**
         * @brief What narrows the event types; empty really does mean every event of them.
         *
         * @par
         * Worth setting on anything busy: this is what keeps a subscriber's backlog proportional to
         * what it asked for rather than to how busy the installation is.
         */
        boost::json::object filter;

        /**
         * @brief ModeDurable or ModeLive; empty takes the server's default, which is ModeDurable.
         */
        std::string mode;
    };

    /**
     * @brief How a receive waits, and how much it takes.
     */
    struct EUCLID_CDK_API ReceiveOptions {

        /**
         * @brief At most this many events; at least one, whatever is asked for.
         */
        long maxEvents{DefaultMaxEvents};

        /**
         * @brief How long to wait for an event before answering with none, up to MaxWaitTime.
         *
         * @par
         * Zero takes whatever is there and returns at once. Anything above zero long-polls, which is
         * what to use in a loop: an empty answer that took twenty seconds beats asking again every
         * hundred milliseconds.
         */
        std::chrono::seconds waitTime{0};

        /**
         * @brief How long the events this call hands over stay invisible to anybody else.
         *
         * @par
         * The deadline for acknowledging them. Miss it and they come back for redelivery with
         * Event::attempts raised, which is the mechanism that makes a crashed subscriber lose
         * nothing - and the reason handling has to tolerate seeing an event twice.
         */
        std::chrono::seconds visibilityTimeout{DefaultVisibilityTimeout};
    };

    /**
     * @brief EES - euclid's event service: what is happening inside euclid, as it happens.
     *
     * @par
     * @code
     * const auto session = EAM::Eam::ForServer(url).Credentials("jens", "secret").Login();
     * const EES::Ees ees(session);
     *
     * std::ignore = ees.Subscribe("indexer", {std::string(EES::ObjectCreated)});
     * for (;;) {
     *     const auto received = ees.ReceiveEvents("indexer", {.waitTime = std::chrono::seconds(20)});
     *     std::vector<std::string> done;
     *     for (const auto &event: received.events) {
     *         handle(event);
     *         done.push_back(event.eventId);
     *     }
     *     if (!done.empty()) std::ignore = ees.AckEvents("indexer", done);
     * }
     * @endcode
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client.
     *
     * @par What this is for, and what it is not
     * Everything a module does already goes onto euclid's internal event bus. This is the way out of
     * it for something that is not a module: a subscriber names itself, says which event types it
     * wants, and then collects them. Where ESM's bucket subscriptions put object events into a queue
     * for one bucket, this reaches every event every module publishes - at the cost of the subscriber
     * having to collect and acknowledge rather than being handed a queue it already knows how to
     * read.
     *
     * @par The subscriber name is the identity
     * Every call here names a subscriber rather than carrying a handle, and that name is what owns
     * the backlog. Two processes using one name are two consumers of one subscription, each getting
     * events the other does not - which is how a subscriber is scaled out, and which is a bug if it
     * was not meant. Two processes that each want everything need two names.
     *
     * @par Receive, handle, acknowledge
     * A received event is claimed rather than removed: it is invisible to anybody else until the
     * visibility timeout runs out, and only AckEvents() takes it away. So an event survives a
     * subscriber that crashes mid-handling, and comes back with Event::attempts raised - which is why
     * handling has to tolerate seeing the same event twice.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Ees final : public ModuleClient {
    public:

        /**
         * @brief Builds EES's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Ees(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Ees(EAM::Session &&) = delete;

        // -- subscribing ------------------------------------------------------------------------

        /**
         * @brief Registers a standing interest in one or more kinds of event.
         *
         * @par
         * Idempotent per event type, and additive: subscribing again for a type the subscriber
         * already has changes nothing, and subscribing for a new one leaves the others in place. The
         * answer is everything the subscriber is now interested in rather than only what this call
         * added.
         *
         * @par
         * A durable subscription starts collecting from the moment it exists, so a subscriber is only
         * guaranteed what happened after it first subscribed - not what happened before.
         *
         * @param subscriber the name this subscriber is known by, and what owns its backlog.
         * @param eventTypes the event types wanted; at least one is required.
         * @param options    the filter and the delivery mode.
         * @throws ServiceError 400 if the name or the event types are missing, or the mode is
         * neither ModeDurable nor ModeLive - refused rather than defaulted, since a subscriber that
         * asked for durable and silently got live would lose events and never know.
         */
        [[nodiscard]]
        SubscriptionsResult Subscribe(const std::string &subscriber, const std::vector<std::string> &eventTypes,
                                      const SubscribeOptions &options = {}) const;

        /**
         * @brief Gives up an interest - one event type, or the subscriber entirely.
         *
         * @param subscriber the subscriber.
         * @param eventType  the one type to stop wanting, or empty to remove the subscriber
         * altogether, which is what something being decommissioned wants.
         * @return how many subscriptions went; none is not an error.
         */
        [[nodiscard]]
        UnsubscribeResult Unsubscribe(const std::string &subscriber, const std::string &eventType = {}) const;

        /**
         * @brief What a subscriber is interested in, and how much is waiting for it.
         *
         * @par
         * SubscriptionsResult::waiting is the backlog, which is the figure worth watching: a
         * subscriber whose waiting count only grows is not keeping up with what it asked for.
         *
         * @param subscriber the subscriber.
         */
        [[nodiscard]]
        SubscriptionsResult ListSubscriptions(const std::string &subscriber) const;

        // -- collecting -------------------------------------------------------------------------

        /**
         * @brief Takes events for a subscriber, waiting for them if asked.
         *
         * @par
         * What comes back is claimed rather than removed - see the class documentation. An empty
         * answer is the ordinary state of a subscriber that is keeping up, not a fault.
         *
         * @par
         * The wait is not a promise: the server caps how many callers may be waiting at once, so
         * that clients waiting for events can never take the last worker thread from a client with
         * something to do. A receive that finds no slot answers with whatever is there right now,
         * which is what a long poll returns anyway when nothing arrives - ask again.
         *
         * @param subscriber the subscriber.
         * @param options    how many, how long to wait, and how long to hold them.
         * @throws ServiceError 400 if no subscriber is named.
         */
        [[nodiscard]]
        EventsResult ReceiveEvents(const std::string &subscriber, const ReceiveOptions &options = {}) const;

        /**
         * @brief Says a subscriber is done with events, so they are not delivered again.
         *
         * @par
         * The other half of ReceiveEvents(): until this, a claimed event is only invisible, and the
         * visibility timeout brings it back. Acknowledging an id twice is not an error, and neither
         * is acknowledging one whose retention has run out - both end the same way and mean the same
         * thing to a caller, which is why AckResult::acknowledged can be smaller than the list sent.
         *
         * @param subscriber the subscriber.
         * @param eventIds   the ids, from Event::eventId; at least one is required.
         * @throws ServiceError 400 if the name or the ids are missing.
         */
        [[nodiscard]]
        AckResult AckEvents(const std::string &subscriber, const std::vector<std::string> &eventIds) const;

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief EES's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to EES.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

    private:

        /**
         * @brief One receive-events request, with the transport timeout its wait needs.
         */
        [[nodiscard]]
        EventsResult Receive(const std::string &subscriber, const ReceiveOptions &options,
                             std::chrono::seconds waitTime) const;
    };

}// namespace Euclid::CDK::EES
