// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Com.h>
#include <euclid/cdk/dto/Ens.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::ENS {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "ens";

    /**
     * @brief The largest message a topic accepts, in bytes.
     */
    inline constexpr long DefaultMaxMessageLength = 1024 * 1024;

    /**
     * @brief What a topic's status reads as: delivering what is published to it...
     */
    inline constexpr std::string_view TopicRunning = "RUNNING";

    /**
     * @brief ...or holding it until somebody starts the topic again.
     */
    inline constexpr std::string_view TopicStopped = "STOPPED";

    /**
     * @brief The retention period that means "whatever the installation says", rather than a number
     * of seconds of this topic's own - see ENS::Ens::SetTopicRetention().
     */
    inline constexpr long InstallationRetention = 0;

    /**
     * @brief The retention period that keeps every message published to the topic.
     *
     * @par
     * Not a very large number of seconds: the server stores such a message with no expiry at all,
     * which is what its TTL index ignores, so nothing is ever going to remove it. The topic then
     * grows without limit and only PurgeTopic() empties it, which is why this is worth choosing
     * rather than defaulting into - see ENS::Ens::SetTopicRetention().
     */
    inline constexpr long RetentionForever = -1;

    /**
     * @brief The namespace that means "every namespace of the account" on the actions that take one
     * as a filter.
     *
     * @par
     * Empty rather than absent, because the server reads the two the same way. Spelled out because
     * "" is the one value whose meaning here is the opposite of narrow.
     */
    inline constexpr std::string_view EveryNamespace = "";

    /**
     * @brief What a published message carries besides its body.
     */
    struct EUCLID_CDK_API PublishMessageOptions {

        /**
         * @brief The publisher's own attributes, which travel onto the queues the message is
         * delivered to.
         */
        COM::VariantMap attributes;

        /**
         * @brief COM::PriorityLow, PriorityMiddle or PriorityHigh; left empty, the topic's own
         * default applies.
         */
        std::string priority;
    };

    /**
     * @brief Which topics a blanket purge applies to. The account and region default to the
     * session's own.
     */
    struct EUCLID_CDK_API PurgeAllTopicsOptions {
        std::string region;
        std::string accountId;

        /**
         * @brief The namespace to narrow it to, which defaults to the session's own. Name
         * EveryNamespace to purge every namespace of the account instead.
         */
        std::string nameSpace;

        /**
         * @brief Whether nameSpace was meant, empty included.
         *
         * @par
         * An empty namespace is a value here rather than "unspecified" - it is what the server reads
         * as "all" - so a caller that means it says so, and one that says nothing gets the session's
         * own namespace.
         */
        bool nameSpaceSet{false};
    };

    /**
     * @brief ENS - euclid's notification module: topics, published messages, and the subscriptions
     * that deliver them onward.
     *
     * @par
     * @code
     * const ENS::Ens ens(session);
     * const EQS::Eqs eqs(session);
     *
     * const auto topic = ens.CreateTopic("order-events");
     * ens.Subscribe(topic.ern, eqs.GetQueueErn("orders"));
     * ens.PublishMessage(topic.ern, R"({"order": 17})");
     * @endcode
     *
     * @par
     * The difference from EQS is what happens to a message once it is there. A queue holds a message
     * until a consumer takes it; a topic hands each message to every subscriber and keeps it as a
     * record of having done so. So there is no receive here, and no receipt handle: a subscriber
     * consumes from its own queue, which is where the message was delivered.
     *
     * @par
     * Two things about a topic can be changed while it is in service. StopTopic() holds delivery
     * without refusing publishers - what arrives meanwhile is kept and fanned out when the topic is
     * started again - which is what a subscriber being redeployed asks for. And SetTopicRetention()
     * says how long a published message is kept at all, since a topic is fanned out at publish time
     * and nothing else would ever remove it.
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Ens final : public ModuleClient {
    public:

        /**
         * @brief Builds ENS's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Ens(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Ens(EAM::Session &&) = delete;

        // -- topics -----------------------------------------------------------------------------

        /**
         * @brief Creates a topic, and answers with the ERN everything else names it by.
         */
        [[nodiscard]]
        CreateTopicResult CreateTopic(const std::string &name, long maxMessageLength = DefaultMaxMessageLength) const;

        /**
         * @brief Deletes a topic, its messages and its subscriptions.
         */
        void DeleteTopic(const std::string &ern) const;

        /**
         * @brief One page of topics, and how many exist in total.
         */
        [[nodiscard]]
        Page<Topic> ListTopics(const ListOptions &options = {}) const;

        /**
         * @brief The ERN of the topic of this name, in the session's account and namespace.
         */
        [[nodiscard]]
        std::string GetTopicErn(const std::string &name) const;

        /**
         * @brief Where a topic lives, how much has been published to it, and whether it is
         * delivering.
         *
         * @par
         * "held" is the useful half of a stopped topic: it says how much has piled up waiting for
         * StartTopic(), which is what decides whether starting it is a moment's work or a fan-out of
         * a fortnight's traffic.
         */
        [[nodiscard]]
        TopicMetadata GetTopicMetadata(const std::string &ern) const;

        /**
         * @brief Stops a topic delivering, without stopping it accepting.
         *
         * @par
         * A stopped topic still takes what is published to it and stores it - it simply does not fan
         * it out. That is the point: a subscriber being redeployed, or a downstream system taken
         * down for the evening, is a reason to hold delivery rather than to lose what arrives
         * meanwhile. Those messages are kept and delivered oldest first when StartTopic() runs.
         *
         * @par
         * Nothing already delivered is affected: a message on a subscriber's queue belongs to that
         * queue, and this is about what happens next.
         */
        [[nodiscard]]
        TopicStateResult StopTopic(const std::string &ern) const;

        /**
         * @brief Starts a topic delivering again, and hands its subscribers everything it held.
         *
         * @par
         * The backlog goes out oldest first as part of this call, so a topic that collected a
         * fortnight of traffic is a fortnight of fan-out here - the result's "released" says how
         * many messages went. The server works a page at a time and marks each message as it goes,
         * so a start that is interrupted has delivered a prefix rather than nothing, and running it
         * again picks up where it stopped.
         *
         * @par
         * Starting a topic that was never stopped is not an error: there is nothing held, nothing is
         * released, and the status simply reads TopicRunning.
         */
        [[nodiscard]]
        TopicStateResult StartTopic(const std::string &ern) const;

        /**
         * @brief Hands what a topic still holds to its subscribers again, oldest first.
         *
         * @par
         * A topic is not consumed the way a queue is: publishing fans a message out there and then,
         * and what stays behind is the record of what was published - kept for the topic's
         * retention period, and once a subscriber has consumed the queue message it received, that
         * record is the only copy left. This is the way back to it for a subscriber that was down,
         * one subscribed after the fact, or one that acknowledged a message and then failed to
         * process it.
         *
         * @par It goes to every subscriber
         * Not only the one that missed something. A consumer that is idempotent does not care; one
         * that is not will double-process. On a busy topic, name a single message rather than
         * replaying a fortnight of traffic to everybody.
         *
         * @par
         * Messages held because the topic was stopped are not resent - they have never been
         * delivered at all, and StartTopic() is what releases them and marks them delivered. They
         * are counted in the result's "held" instead. A stopped topic is refused outright, for the
         * same reason: it delivers nothing by somebody's decision, and this would be the way around
         * that.
         *
         * @param ern the topic.
         * @param messageId resend only this message, as ListMessages() reports its id; empty resends
         * everything the topic holds. One belonging to another topic is refused rather than fanned
         * out to subscriptions it was never published to.
         */
        [[nodiscard]]
        ResendResult ResendMessages(const std::string &ern, const std::string &messageId = {}) const;

        /**
         * @brief Sets how long a message published to this topic is kept, in seconds.
         *
         * @par
         * Worth setting. A topic is fanned out at publish time, so nothing ever consumes its
         * messages and nothing else removes them: without a retention period the collection only
         * grows, and because every topic shares it, one busy topic is paid for by every publish in
         * the installation.
         *
         * @par
         * The change applies to messages published afterwards; the ones already stored keep the
         * expiry they were given, since that is stamped on each message rather than looked up when
         * it is read.
         *
         * @param ern             the topic.
         * @param retentionPeriod seconds; InstallationRetention to follow
         * euclid.modules.ens.retention-period as it changes rather than freezing a copy of what it
         * says today; or RetentionForever to keep every message published to this topic.
         * @throws EuclidError if the period is below RetentionForever, which the server refuses
         * anyway - this just says so before the round trip.
         */
        [[nodiscard]]
        TopicRetentionResult SetTopicRetention(const std::string &ern, long retentionPeriod) const;

        /**
         * @brief Changes the largest message a topic accepts, and answers with the length it now
         * has.
         *
         * @par
         * What is published from here on, and nothing else: a message already in the topic was
         * accepted under the rule in force when it arrived, and lowering the limit is not a reason
         * to go back and lose it.
         *
         * @par
         * The length is the body's alone - the same figure GetTopicMetadata() reports - so the limit
         * is in the units of the numbers it is compared against. Attributes travel alongside and are
         * not counted.
         *
         * @param ern              the topic.
         * @param maxMessageLength bytes, and a positive number of them. Zero is not "no limit" here
         * but a topic that accepts nothing, so the server refuses it - where EQS takes zero for a
         * queue and reads it as "no limit of its own". Taking nothing for a while is what
         * StopTopic() is for, and it says so reversibly.
         * @throws EuclidError if the length is not positive, which the server refuses anyway - this
         * just says so before the round trip.
         */
        [[nodiscard]]
        long SetTopicMaxMessageLength(const std::string &ern, long maxMessageLength) const;

        /**
         * @brief Deletes every message a topic has kept, leaving the topic and its subscriptions in
         * place.
         *
         * @par
         * It does not un-deliver anything: a message already handed to a subscriber is on that
         * subscriber's queue and belongs to it now.
         */
        void PurgeTopic(const std::string &ern) const;

        /**
         * @brief Purges every topic of an account, which defaults to this session's own.
         *
         * @par
         * As blunt as it sounds, and there is no undo: it exists for a test environment between
         * runs.
         *
         * @par
         * Narrowed to the session's namespace unless told otherwise, so a session scoped to one
         * namespace cannot empty another's topics by accident. Naming EveryNamespace asks for the
         * account's lot, which is what EQS::Eqs::PurgeAllQueues() does by default - the two differ
         * because each keeps the default it shipped with.
         */
        void PurgeAllTopics(const PurgeAllTopicsOptions &options = {}) const;

        /**
         * @brief Tags a topic.
         */
        void AddTopicTag(const std::string &ern, const std::string &key, const std::string &value) const;

        /**
         * @brief Sets the value of a tag the topic already has.
         */
        void SetTopicTag(const std::string &ern, const std::string &key, const std::string &value) const;

        /**
         * @brief Removes a tag from a topic.
         */
        void DeleteTopicTag(const std::string &ern, const std::string &key) const;

        // -- messages ---------------------------------------------------------------------------

        /**
         * @brief Publishes a message to a topic, and answers with the ID the server gave it.
         *
         * @par
         * Every subscription on the topic gets a copy, each on its own queue and each consumed
         * independently: a subscriber that is slow or stopped delays nobody else, and a message
         * already delivered is not withdrawn if the subscription is later removed.
         */
        [[nodiscard]]
        std::string PublishMessage(const std::string &topicErn, const std::string &body, const PublishMessageOptions &options = {}) const;

        /**
         * @brief One page of the messages a topic has kept, and how many it holds in total.
         */
        [[nodiscard]]
        Page<Message> ListMessages(const std::string &topicErn, const PageOptions &options = {}) const;

        /**
         * @brief A topic's message counters: what is on it, what went out, and what had to go out
         * again.
         */
        [[nodiscard]]
        MessageCount GetMessageCount(const std::string &ern) const;

        /**
         * @brief One attribute of one published message.
         *
         * @par
         * The attribute's name travels as "key" throughout ENS and as "name" in most of EQS - the
         * server's own asymmetry, reproduced rather than papered over.
         */
        [[nodiscard]]
        MessageAttribute GetMessageAttribute(const std::string &messageId, const std::string &key) const;

        /**
         * @brief Sets one attribute of one published message, creating it if it was not there.
         */
        [[nodiscard]]
        MessageAttribute SetMessageAttribute(const std::string &messageId, const std::string &key, const COM::Variant &value) const;

        // -- subscriptions ----------------------------------------------------------------------

        /**
         * @brief Delivers a topic's messages onward to a queue from now on.
         *
         * @par
         * Only COM::Queue is a target type so far, so targetErn names an EQS queue. A message
         * published before this call is not delivered retrospectively - a subscription says what
         * happens next.
         *
         * @par
         * Not idempotent: a second call registers a second subscription and the queue then receives
         * every message twice, so a caller that may run twice checks ListSubscriptions() first.
         */
        [[nodiscard]]
        COM::SubscribeResult Subscribe(const std::string &topicErn, const std::string &targetErn, const std::string &targetType = std::string(COM::Queue)) const;

        /**
         * @brief Removes a subscription, by the ERN Subscribe() answered with - not the topic's, and
         * not the queue's.
         */
        void Unsubscribe(const std::string &ern) const;

        /**
         * @brief Every subscription currently registered on a topic.
         */
        [[nodiscard]]
        std::vector<COM::Subscription> ListSubscriptions(const std::string &topicErn) const;

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief ENS's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to ENS.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

    private:

        /**
         * @brief start-topic and stop-topic take the same request and differ only in what they
         * record.
         */
        [[nodiscard]]
        TopicStateResult SetTopicState(const std::string &action, const std::string &ern) const;
    };

}// namespace Euclid::CDK::ENS
