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
#include <euclid/cdk/dto/Com.h>
#include <euclid/cdk/dto/Eqs.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EQS {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "eqs";

    /**
     * @brief How long a received message stays invisible before it goes back on the queue.
     */
    inline constexpr long DefaultVisibility = 30;

    /**
     * @brief How many times a message may be received before it goes to the dead letter queue.
     */
    inline constexpr long DefaultMaxRetries = 3;

    /**
     * @brief The largest message a queue accepts, in bytes.
     *
     * @par
     * The server's own default, both for a queue created without the field and for one whose stored
     * limit is InstallationMaxMessageLength.
     */
    inline constexpr long DefaultMaxMessageLength = 1024 * 1024;

    /**
     * @brief The message-length limit that means "no limit of this queue's own".
     *
     * @par
     * A send is then measured against the installation's figure rather than refused, which is what
     * makes zero the answer for a queue that has no opinion - and not a queue that accepts nothing.
     * See EQS::Eqs::SetQueueMaxMessageLength().
     */
    inline constexpr long InstallationMaxMessageLength = 0;

    /**
     * @brief The longest delay a queue may hold its messages for, in seconds.
     *
     * @par
     * The bound AWS SQS holds DelaySeconds to, and the one euclid refuses above. A delay is for
     * smoothing a burst or letting a writer finish, not for scheduling: something that has to wait a
     * quarter of an hour wants a timestamp of its own rather than a queue that holds everything
     * back.
     */
    inline constexpr long MaxDelay = 900;

    /**
     * @brief How many messages one receive takes unless the caller says otherwise.
     */
    inline constexpr long DefaultMaxMessages = 10;

    /**
     * @brief What a queue's status reads as: handing messages out...
     */
    inline constexpr std::string_view QueueAvailable = "AVAILABLE";

    /**
     * @brief ...or stopped, and handing nothing out until it is started again.
     */
    inline constexpr std::string_view QueueStopped = "STOPPED";

    /**
     * @brief How long to pause before asking again when the server answered a long poll immediately
     * because it had no slot free to wait in.
     *
     * @par
     * Only reached when the server is short of threads, which is the moment to ask less often rather
     * than more.
     */
    inline constexpr std::chrono::milliseconds SlotsBusyBackoff{500};

    /**
     * @brief How close to its deadline a long poll may come back and still count as having been
     * waited out rather than answered early.
     *
     * @par
     * Absorbs the jitter between the server's clock and this one, so an honoured wait is not
     * followed by a pointless extra request for the last few milliseconds.
     */
    inline constexpr std::chrono::milliseconds HonouredWaitTolerance{250};

    /**
     * @brief Added to a long poll's wait to give the response time to travel: the server answers at
     * the end of the window it was asked for, so a timeout of exactly that window would race the
     * network.
     */
    inline constexpr std::chrono::milliseconds LongPollResponseMargin{10000};

    /**
     * @brief What a queue is created with. Every field has a server-side default.
     */
    struct EUCLID_CDK_API CreateQueueOptions {

        /**
         * @brief How long a received message stays invisible, in seconds, unless a receive says
         * otherwise.
         */
        long visibility{DefaultVisibility};

        /**
         * @brief How many times a message may be received before it is moved to dlqName. A queue
         * without a dead letter queue keeps redelivering.
         */
        long maxRetries{DefaultMaxRetries};

        /**
         * @brief The largest message this queue accepts, in bytes.
         */
        long maxMessageLength{DefaultMaxMessageLength};

        /**
         * @brief The name of the queue that failed messages end up on.
         */
        std::string dlqName;

        /**
         * @brief How long a sent message waits before it can be received at all, in seconds.
         */
        long delay{0};

        /**
         * @brief The priority every message of this queue gets unless a send overrides it -
         * COM::PriorityLow and its siblings, or the server's default when left empty.
         */
        std::string priority;

        /**
         * @brief Marks the queue as euclid's own plumbing, which leaves it out of an ordinary
         * listing.
         */
        bool internal{false};
    };

    /**
     * @brief One message within a batch - the same fields a single send takes, minus the queue.
     *
     * @par
     * A batch names the queue once, so every message in one goes to the same queue.
     */
    struct EUCLID_CDK_API SendMessageBatchEntry {

        /**
         * @brief The message body.
         */
        std::string body;

        /**
         * @brief The sender's own attributes, which come back on the received message.
         */
        COM::VariantMap attributes;

        /**
         * @brief euclid's envelope, carried across every hop.
         */
        COM::VariantMap systemAttributes;

        /**
         * @brief "LOW", "MEDIUM" or "HIGH"; empty takes the queue's own.
         */
        std::string priority;
    };

    /**
     * @brief One message a batch would not send, and why.
     *
     * @par
     * The index is where the message sat in the list that was sent. The server minted nothing for a
     * message it did not accept, so the position is the only thing the two sides share.
     */
    struct EUCLID_CDK_API SendBatchFailure {
        long index{};
        std::string reason;
    };

    /**
     * @brief What a SendMessageBatch() did.
     *
     * @par
     * Counts, the ids of what went in request order, and the failures named one by one. `asked`
     * always equals `sent` plus `failed.size()`.
     *
     * @par
     * The failure list is what differs from every other multi-item call in euclid, which only
     * counts. A delete that skipped a key removed something already gone; a send that skipped a
     * message dropped it, and a producer holding "97 of 100" cannot act on that without knowing
     * which three to send again.
     */
    struct EUCLID_CDK_API SendBatchResult {
        std::string ern;
        long asked{};
        long sent{};
        std::vector<std::string> messageIds;
        std::vector<SendBatchFailure> failed;
    };

    /**
     * @brief What a message carries besides its body.
     */
    struct EUCLID_CDK_API SendMessageOptions {

        /**
         * @brief The sender's own attributes, which come back on the received message.
         */
        COM::VariantMap attributes;

        /**
         * @brief euclid's envelope, which travels with the message across every hop - what lets a
         * service pass on what it received rather than what it happens to know.
         */
        COM::VariantMap systemAttributes;

        /**
         * @brief COM::PriorityLow, PriorityMiddle or PriorityHigh; left empty, the message takes the
         * queue's own default.
         */
        std::string priority;
    };

    /**
     * @brief How many messages a receive takes, and how long it is willing to wait for them.
     */
    struct EUCLID_CDK_API ReceiveMessagesOptions {
        long maxMessages{DefaultMaxMessages};

        /**
         * @brief How long the server may hold the request open. Zero does not wait at all.
         */
        std::chrono::seconds waitTime{0};
    };

    /**
     * @brief Which queues a blanket purge applies to. The account and region default to the
     * session's own.
     */
    struct EUCLID_CDK_API PurgeAllQueuesOptions {
        std::string region;
        std::string accountId;

        /**
         * @brief The namespace to narrow it to. Left empty it purges every namespace of the account,
         * which is what this call has always done.
         */
        std::string nameSpace;
    };

    /**
     * @brief EQS - euclid's queue module: queues, messages, leases and dead letter queues.
     *
     * @par
     * @code
     * const auto session = EAM::Eam::ForServer(url).Credentials("jens", "secret").Login();
     * const EQS::Eqs eqs(session);
     *
     * const auto queue = eqs.CreateQueue("orders");
     * eqs.SendMessage(queue.ern, R"({"order": 17})");
     *
     * for (const auto &message: eqs.ReceiveMessages(queue.ern, {.waitTime = std::chrono::seconds(20)}).items) {
     *     handle(message.body);
     *     eqs.DeleteMessage(message.receiptHandle);
     * }
     * @endcode
     *
     * @par
     * Receiving is a lease rather than a read: a message a consumer takes is invisible to every
     * other consumer until its visibility timeout expires, and deleting it with the receipt handle
     * is what says the work was done. A consumer that dies instead simply stops holding the lease,
     * and the message comes back - which is why the delete belongs after the work rather than before
     * it.
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client. One request at a time, like the
     * session; a caller that wants several consumers wants a session each.
     *
     * @par
     * "get-metadata" and "add-metadata" - the server's older names for reading and writing a queue's
     * attributes - are not wrapped here, as in euclid-pdk and euclid-ndk; Call() reaches them.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Eqs final : public ModuleClient {
    public:

        /**
         * @brief Builds EQS's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Eqs(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Eqs(EAM::Session &&) = delete;

        /**
         * @brief How long to pause before asking again when the server declined to wait out a long
         * poll.
         *
         * @par
         * Settable because it is the one part of the polling a caller may reasonably want to change
         * - and because nothing in a test suite wants to sit out a backoff that exists to be kind to
         * a server short of threads.
         */
        void SetSlotsBusyBackoff(std::chrono::milliseconds backoff);

        // -- queues -----------------------------------------------------------------------------

        /**
         * @brief Creates a queue, and answers with the ERN everything else names it by.
         */
        [[nodiscard]]
        CreateQueueResult CreateQueue(const std::string &name, const CreateQueueOptions &options = {}) const;

        /**
         * @brief Deletes a queue and everything on it.
         */
        void DeleteQueue(const std::string &ern) const;

        /**
         * @brief One page of queues, and how many exist in total.
         *
         * @param options         paging, ordering and prefix.
         * @param includeInternal whether euclid's own queues - the delivery queue behind a bucket
         * listener, say - are in it. Left out by default, so a listing shows what a person would
         * recognise; a component looking for the queues it created has to ask.
         */
        [[nodiscard]]
        Page<Queue> ListQueues(const ListOptions &options = {}, bool includeInternal = false) const;

        /**
         * @brief One queue, by name or by ERN.
         *
         * @par
         * What comes back is exactly what ListQueues() describes each of its own
         * with - the ERN, the owner, visibility, delay and retention, the dead letter queue, tags and the available/delayed/in-flight counts - so this is the single-queue form of a listing rather than another
         * view of one.
         *
         * @par
         * A value starting with "ern:" is taken as an ERN and names one queue in the
         * installation; anything else is a name and is resolved in the session's own account and
         * namespace, the way GetQueueErn() resolves one.
         *
         * @param nameOrErn the queue's name, or its ERN.
         */
        [[nodiscard]]
        Queue GetQueue(const std::string &nameOrErn) const;

        /**
         * @brief One message, by its id.
         *
         * @par
         * The message id, not a receipt handle: a receipt handle belongs to one delivery and is
         * void once that delivery's claim has expired, while the id names the message for as long
         * as it exists - and asking about a message is something one does after the fact.
         *
         * @param messageId the message's id.
         */
        [[nodiscard]]
        Message GetMessage(const std::string &messageId) const;

        /**
         * @brief The ERN of the queue of this name, in the session's account and namespace.
         */
        [[nodiscard]]
        std::string GetQueueErn(const std::string &name) const;

        /**
         * @brief Whether a queue exists.
         *
         * @par
         * Three answers, not two. true and false are the ones a caller expects; the third is a
         * ServiceError, and it is the one that matters. An expired session, an unreachable gateway
         * or a refused permission is not the same as "not there", and returning false for them
         * would have callers deleting and recreating things over an outage. Only HTTP 404 - the
         * answer that actually says it is absent - becomes false; everything else is rethrown.
         *
         * @param name name of the queue, resolved in the session's account and namespace.
         * @throws ServiceError if the question could not be answered.
         */
        [[nodiscard]]
        bool ExistsQueue(const std::string &name) const;

        /**
         * @brief Where a queue lives and how much is in it.
         */
        [[nodiscard]]
        QueueMetadata GetQueueMetadata(const std::string &ern) const;

        /**
         * @brief Deletes every message on a queue, leaving the queue itself in place.
         */
        void PurgeQueue(const std::string &ern) const;

        /**
         * @brief Deletes every message on every queue of an account, which defaults to this
         * session's own.
         *
         * @par
         * Exactly as blunt as it sounds, and there is no undo: it exists for a test environment
         * between runs rather than for anything that has consumers attached.
         *
         * @par
         * Every namespace of that account unless options.nameSpace narrows it - the opposite default
         * to ENS::Ens::PurgeAllTopics(), which follows the session. Neither is wrong: this one has
         * purged the account since it existed, and narrowing it silently would quietly spare queues
         * a caller meant to empty.
         */
        void PurgeAllQueues(const PurgeAllQueuesOptions &options = {}) const;

        /**
         * @brief Stops a queue, so it hands no more messages out.
         *
         * @par
         * Messages already in flight are left alone: their consumer took them before the queue was
         * stopped and is still entitled to finish, so deleting one still works. Only new receives
         * are refused, with HTTP 409.
         */
        [[nodiscard]]
        QueueStatusResult StopQueue(const std::string &ern) const;

        /**
         * @brief Starts a queue that was stopped, so it hands messages out again.
         */
        [[nodiscard]]
        QueueStatusResult StartQueue(const std::string &ern) const;

        /**
         * @brief Changes a queue's default visibility timeout, and answers with the one it now has.
         *
         * @par
         * Only the default changes. Messages already in flight keep the window they were given when
         * they were received, so this can neither expire a lease a consumer is still working on nor
         * hold back a message its consumer has already given up on.
         */
        [[nodiscard]]
        long SetQueueVisibility(const std::string &ern, long visibility) const;

        /**
         * @brief Changes how long a sent message waits before it can be received at all, and answers
         * with the delay the queue now has.
         *
         * @par
         * Only what is sent from here on. A message already waiting had its delay turned into a
         * timestamp when it arrived, and moving that now would either release a message early or
         * hold back one that was promised sooner.
         *
         * @param ern   the queue.
         * @param delay seconds, from zero to MaxDelay.
         * @throws EuclidError if the delay is outside that range, which the server refuses anyway -
         * this just says so before the round trip.
         */
        [[nodiscard]]
        long SetQueueDelay(const std::string &ern, long delay) const;

        /**
         * @brief Changes the largest message a queue accepts, and answers with what it now holds
         * alongside what a send is actually measured against.
         *
         * @par
         * What is sent from here on: a message already in the queue was measured against the limit
         * in force when it arrived, and lowering this is not a reason to go back and reject it.
         *
         * @par
         * The length is the body's alone - the same figure Message::size carries and
         * GetQueueMetadata() reports - so the limit is in the units of the numbers it is compared
         * against. Attributes travel alongside and are not counted.
         *
         * @param ern              the queue.
         * @param maxMessageLength bytes, or InstallationMaxMessageLength to leave the queue with no
         * limit of its own. ENS refuses that value for a topic; EQS takes it, and the two are not
         * the same rule.
         * @throws EuclidError if the length is negative, which the server refuses anyway.
         */
        [[nodiscard]]
        MaxMessageLengthResult SetQueueMaxMessageLength(const std::string &ern, long maxMessageLength) const;

        /**
         * @brief Moves messages out of a dead letter queue and back onto the queues they came from.
         *
         * @par
         * "ern" has to name a queue that some other queue points at as its dead letter queue; an
         * ordinary queue is refused rather than redriven into itself. A named targetErn has to be
         * one of the queues that feed it, since anything else would be a move rather than a redrive.
         *
         * @par
         * Left unnamed, each message goes back where it came from - and a message whose origin was
         * never recorded is left alone rather than guessed at. The result says how many, so a caller
         * can name a target and deal with them deliberately.
         */
        [[nodiscard]]
        RedriveDlqResult RedriveDlq(const std::string &ern, const std::string &targetErn = {}) const;

        /**
         * @brief Tags a queue.
         */
        void AddQueueTag(const std::string &ern, const std::string &key, const std::string &value) const;

        /**
         * @brief Sets the value of a tag the queue already has.
         */
        void SetQueueTag(const std::string &ern, const std::string &key, const std::string &value) const;

        /**
         * @brief Removes a tag from a queue.
         */
        void DeleteQueueTag(const std::string &ern, const std::string &key) const;

        // -- messages ---------------------------------------------------------------------------

        /**
         * @brief Puts a message on a queue, and answers with the ID the server gave it.
         *
         * @par
         * The envelope and the priority are left out of the request entirely when there is nothing
         * to say about them, so the queue's own defaults are what apply rather than an empty string
         * the server would have to interpret.
         */
        [[nodiscard]]
        std::string SendMessage(const std::string &queueErn, const std::string &body, const SendMessageOptions &options = {}) const;

        /**
         * @brief Sends several messages to one queue in a single call.
         *
         * @par
         * The saving over calling SendMessage() in a loop is mostly in the database rather than the
         * round trips: the whole batch is written in one insert, and the queue's counters are
         * adjusted once instead of once per message.
         *
         * @par
         * A message that cannot be sent does not stop the others. The result says how many were
         * asked for and how many went, lists the ids of those that went in request order, and names
         * each rejection by its position in `messages` - so a producer retries exactly those rather
         * than the whole batch and duplicates everything else.
         *
         * @par
         * Every message being rejected still returns rather than throwing: the request was well
         * formed and has been answered with a reason for each. Check `sent`, not the absence of an
         * exception. An empty batch, or one over the installation's
         * euclid.modules.eqs.max-batch-size, is refused with HTTP 400 - those are mistakes in the
         * request rather than in a message, so there is no partial outcome to report.
         *
         * @param queueErn the queue ERN, or a bare queue name
         * @param messages the messages, in the order they are to be sent
         * @return what was sent, and what was not
         */
        [[nodiscard]]
        SendBatchResult SendMessageBatch(const std::string &queueErn, const std::vector<SendMessageBatchEntry> &messages) const;

        /**
         * @brief Takes up to options.maxMessages messages off a queue, waiting up to
         * options.waitTime for them.
         *
         * @par
         * The waiting is the server's, not this client's: it holds the request open until a message
         * lands or the time runs out, so an idle queue costs one request for the whole window rather
         * than one per poll tick, and a message comes back the instant it is sent.
         *
         * @par
         * The one case that loops is the server declining to wait. It keeps a bounded number of
         * long-poll slots - one fewer than it has threads - so that consumers sitting in a wait
         * cannot starve the producers trying to send to them; with none free it answers at once with
         * whatever is on the queue. That comes back empty with time still on the clock, and the
         * answer is to wait a moment and ask again rather than immediately, since asking again at
         * once is what a server short of threads does not need.
         *
         * @par
         * With no wait asked for, the queue's depth is checked first and an empty queue costs no
         * receive at all - a receive is a write, and one that takes nothing is work the server did
         * for nothing.
         */
        [[nodiscard]]
        Page<Message> ReceiveMessages(const std::string &queueErn, const ReceiveMessagesOptions &options = {}) const;

        /**
         * @brief Takes everything off a queue, a batch at a time, until it comes back empty.
         *
         * @par
         * For draining a queue rather than for consuming one: every message comes back on a lease,
         * so a caller that does not delete them will see them all again once the visibility timeout
         * expires.
         */
        [[nodiscard]]
        std::vector<Message> ReceiveAllMessages(const std::string &queueErn, long batchSize = DefaultMaxMessages) const;

        /**
         * @brief One page of a queue's messages, without receiving them.
         *
         * @par
         * A read rather than a lease: nothing here becomes invisible, nothing counts as a delivery,
         * and nothing can be deleted by receipt handle afterwards. It is how a queue is inspected,
         * not how it is consumed.
         */
        [[nodiscard]]
        Page<Message> ListMessages(const std::string &queueErn, const PageOptions &options = {}) const;

        /**
         * @brief Deletes a received message, by the handle the receive handed out.
         *
         * @par
         * The handle is a lease: this works while the message's visibility timeout is still running
         * and fails once it has expired and the message has gone back on the queue.
         */
        void DeleteMessage(const std::string &receiptHandle) const;

        /**
         * @brief Deletes a message by its ID, including one nobody has received.
         *
         * @par
         * Bypasses the lease DeleteMessage() goes through, which is what makes it able to remove a
         * message that is still waiting or still delayed. A euclid extension with no SQS equivalent.
         */
        void DeleteMessageById(const std::string &messageId) const;

        /**
         * @brief How many messages a queue holds, by the state they are in.
         */
        [[nodiscard]]
        MessageCount GetMessageCount(const std::string &ern) const;

        /**
         * @brief Everything about one message except its body.
         */
        [[nodiscard]]
        MessageMetadata GetMessageMetadata(const std::string &messageId) const;

        /**
         * @brief Changes how long one message stays invisible - extending a lease a consumer needs
         * longer.
         *
         * @par
         * Sent as "set-message-visibility", the name that says what it changes and pairs with
         * SetQueueVisibility(). euclid answers to "set-visibility" as well, which is what euclid-jdk
         * sends and what a server older than the newer name knows it by; such a server refuses this
         * with HTTP 404, and Call() is the way round that.
         */
        void SetMessageVisibility(const std::string &messageId, long visibility) const;

        /**
         * @brief One attribute of one message.
         */
        [[nodiscard]]
        MessageAttribute GetMessageAttribute(const std::string &messageId, const std::string &name) const;

        /**
         * @brief Sets one attribute of one message, creating it if it was not there.
         *
         * @par
         * The attribute's name travels as "key" on this action and as "name" on the one that reads
         * it back - the server's own asymmetry, reproduced rather than papered over, so that a
         * request built from this SDK matches what euclid-cli and euclid-jdk send.
         */
        [[nodiscard]]
        MessageAttribute SetMessageAttribute(const std::string &messageId, const std::string &name, const COM::Variant &value) const;

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief EQS's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to EQS.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

        /**
         * @brief A view of this client whose requests are marked as euclid's own traffic.
         *
         * @par
         * Some calls observe the system rather than use it: reading a queue's depth to report it,
         * polling for a heartbeat. They are indistinguishable from real work by their action alone -
         * the same get-message-count is a user's question one moment and a metric collector's poll
         * the next - so the caller says which it is, and the server logs and scales accordingly.
         * Instrumentation that polls every few seconds would otherwise keep a pool permanently awake
         * and make an idle module look busy: the monitoring preventing the thing it exists to
         * measure.
         *
         * @par
         * A separate client rather than a flag on this one, so that no call has to remember to set
         * it back. It authenticates as the same session, which has to outlive it as well.
         */
        [[nodiscard]]
        Eqs AsInternal() const;

    private:

        /**
         * @brief Builds a client that carries one header on every request - what AsInternal() is.
         */
        Eqs(const EAM::Session &session, Headers headers);

        /**
         * @brief start-queue and stop-queue take the same request and differ only in what they
         * record.
         */
        [[nodiscard]]
        QueueStatusResult SetQueueStatus(const std::string &action, const std::string &ern) const;

        /**
         * @brief One receive-messages request, held open by the server for waitTime.
         *
         * @par
         * The session's own timeout is sized for an answer that comes straight back, so a long poll
         * gets its own: the request is meant to take as long as the server was asked to hold it, and
         * abandoning it at the usual deadline would abandon a request being served correctly.
         */
        [[nodiscard]]
        Page<Message> Receive(const std::string &queueErn, long maxMessages, std::chrono::seconds waitTime) const;

        std::chrono::milliseconds _slotsBusyBackoff{SlotsBusyBackoff};
    };

}// namespace Euclid::CDK::EQS
