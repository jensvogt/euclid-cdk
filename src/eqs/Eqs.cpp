// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <thread>
#include <tuple>
#include <utility>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/eqs/Eqs.h>

namespace Euclid::CDK::EQS {

    namespace {

        /**
         * @brief An attribute map as a payload field, left out entirely when there is nothing to say.
         */
        void putAttributes(boost::json::object &payload, const std::string &field, const COM::VariantMap &attributes) {
            if (!attributes.empty()) payload[field] = COM::VariantMapToJson(attributes);
        }

    }// namespace

    Eqs::Eqs(const EAM::Session &session) : ModuleClient(session, std::string(EQS::Target)) {}

    Eqs::Eqs(const EAM::Session &session, Headers headers)
        : ModuleClient(session, std::string(EQS::Target), {}, std::move(headers)) {}

    void Eqs::SetSlotsBusyBackoff(const std::chrono::milliseconds backoff) { _slotsBusyBackoff = backoff; }

    // -- queues -------------------------------------------------------------------------------------

    CreateQueueResult Eqs::CreateQueue(const std::string &name, const CreateQueueOptions &options) const {
        const boost::json::object payload{
                {"name", name},
                {"visibility", options.visibility},
                {"maxRetries", options.maxRetries},
                {"maxMessageLength", options.maxMessageLength},
                {"dlqName", options.dlqName},
                {"delay", options.delay},
                {"priority", options.priority},
                {"internal", options.internal},
        };
        return ToCreateQueueResult(Call("create-queue", payload));
    }

    void Eqs::DeleteQueue(const std::string &ern) const {
        std::ignore = Call("delete-queue", {{"ern", ern}});
    }

    Page<Queue> Eqs::ListQueues(const ListOptions &options, const bool includeInternal) const {
        auto payload = ListPayload(options, "name");
        payload["includeInternal"] = includeInternal;
        return ToPage<Queue>(Call("list-queues", payload), "queues", ToQueue);
    }

    Queue Eqs::GetQueue(const std::string &nameOrErn) const {
        // Sent as whichever of the two it is: the server resolves a name against the session's own
        // account and namespace, and a name put in the ERN field would simply not be found.
        const boost::json::object payload = nameOrErn.starts_with("ern:")
                                                    ? boost::json::object{{"ern", nameOrErn}}
                                                    : boost::json::object{{"name", nameOrErn}};
        return ToQueue(Call("get-queue", payload).at("queue"));
    }

    Message Eqs::GetMessage(const std::string &messageId) const {
        return ToMessage(Call("get-message", {{"messageId", messageId}}).at("message"));
    }

    std::string Eqs::GetQueueErn(const std::string &name) const {
        return TextOf("get-queue-ern", {{"name", name}}, "ern");
    }

    QueueMetadata Eqs::GetQueueMetadata(const std::string &ern) const {
        return ToQueueMetadata(Call("get-queue-metadata", {{"ern", ern}}));
    }

    void Eqs::PurgeQueue(const std::string &ern) const {
        std::ignore = Call("purge-queue", {{"ern", ern}});
    }

    void Eqs::PurgeAllQueues(const PurgeAllQueuesOptions &options) const {
        const boost::json::object payload{
                {"region", options.region.empty() ? Session().Region() : options.region},
                {"accountId", options.accountId.empty() ? Session().AccountId() : options.accountId},
                // Not defaulted to the session's namespace, deliberately: an empty namespace is what
                // the server reads as "every namespace of the account", and that is what this call
                // has always done.
                {"nameSpace", options.nameSpace},
        };
        std::ignore = Call("purge-all-queues", payload);
    }

    QueueStatusResult Eqs::StopQueue(const std::string &ern) const {
        return SetQueueStatus("stop-queue", ern);
    }

    QueueStatusResult Eqs::StartQueue(const std::string &ern) const {
        return SetQueueStatus("start-queue", ern);
    }

    QueueStatusResult Eqs::SetQueueStatus(const std::string &action, const std::string &ern) const {
        return ToQueueStatusResult(Call(action, {{"ern", ern}}));
    }

    long Eqs::SetQueueVisibility(const std::string &ern, const long visibility) const {
        return NumberOf("set-queue-visibility", {{"ern", ern}, {"visibility", visibility}}, "visibility");
    }

    long Eqs::SetQueueDelay(const std::string &ern, const long delay) const {
        if (delay < 0 || delay > MaxDelay) {
            throw EuclidError("delay has to be between 0 and " + std::to_string(MaxDelay) + " seconds");
        }
        return NumberOf("set-queue-delay", {{"ern", ern}, {"delay", delay}}, "delay");
    }

    MaxMessageLengthResult Eqs::SetQueueMaxMessageLength(const std::string &ern, const long maxMessageLength) const {
        // Zero is allowed and is not "accept nothing": it is the queue carrying no limit of its own,
        // which a send then measures against the installation's figure instead.
        if (maxMessageLength < 0) {
            throw EuclidError("maxMessageLength cannot be negative; zero leaves the queue with no limit of its own");
        }
        return ToMaxMessageLengthResult(Call("set-queue-max-message-length", {{"ern", ern}, {"maxMessageLength", maxMessageLength}}));
    }

    RedriveDlqResult Eqs::RedriveDlq(const std::string &ern, const std::string &targetErn) const {
        return ToRedriveDlqResult(Call("redrive-dlq", {{"ern", ern}, {"targetErn", targetErn}}));
    }

    void Eqs::AddQueueTag(const std::string &ern, const std::string &key, const std::string &value) const {
        std::ignore = Call("add-queue-tag", {{"ern", ern}, {"key", key}, {"value", value}});
    }

    void Eqs::SetQueueTag(const std::string &ern, const std::string &key, const std::string &value) const {
        std::ignore = Call("set-queue-tag", {{"ern", ern}, {"key", key}, {"value", value}});
    }

    void Eqs::DeleteQueueTag(const std::string &ern, const std::string &key) const {
        std::ignore = Call("delete-queue-tag", {{"ern", ern}, {"key", key}});
    }

    // -- messages -----------------------------------------------------------------------------------

    std::string Eqs::SendMessage(const std::string &queueErn, const std::string &body, const SendMessageOptions &options) const {

        boost::json::object payload{
                {"ern", queueErn},
                {"body", body},
                {"attributes", COM::VariantMapToJson(options.attributes)},
        };
        putAttributes(payload, "systemAttributes", options.systemAttributes);
        if (!options.priority.empty()) payload["priority"] = options.priority;

        return TextOf("send-message", payload, "messageId");
    }

    Page<Message> Eqs::ReceiveMessages(const std::string &queueErn, const ReceiveMessagesOptions &options) const {

        if (options.waitTime <= std::chrono::seconds::zero()) {
            // A receive is a write, and one that takes nothing is work the server did for nothing.
            if (GetMessageCount(queueErn).available <= 0) return {};
            return Receive(queueErn, options.maxMessages, std::chrono::seconds::zero());
        }

        const auto deadline = std::chrono::steady_clock::now() + options.waitTime;
        for (;;) {

            // Rounded up rather than truncated: the wait travels in whole seconds, and a caller who
            // asked for five would otherwise be given four and a round trip to ask for the fifth.
            const auto left = deadline - std::chrono::steady_clock::now();
            const auto wait = std::max(std::chrono::seconds(1), std::chrono::ceil<std::chrono::seconds>(left));

            auto page = Receive(queueErn, options.maxMessages, wait);
            if (!page.items.empty()) return page;

            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= HonouredWaitTolerance) return page;

            // Answered early because the server had no long-poll slot free, which is the moment to
            // ask less often rather than more.
            std::this_thread::sleep_for(std::min(std::chrono::duration_cast<std::chrono::milliseconds>(remaining), _slotsBusyBackoff));
        }
    }

    std::vector<Message> Eqs::ReceiveAllMessages(const std::string &queueErn, const long batchSize) const {

        std::vector<Message> messages;
        for (;;) {
            auto batch = ReceiveMessages(queueErn, {.maxMessages = batchSize});
            if (batch.items.empty()) return messages;
            messages.insert(messages.end(), std::make_move_iterator(batch.items.begin()), std::make_move_iterator(batch.items.end()));
        }
    }

    Page<Message> Eqs::ListMessages(const std::string &queueErn, const PageOptions &options) const {
        auto payload = PagePayload(options, "created");
        payload["queueErn"] = queueErn;
        return ToPage<Message>(Call("list-messages", payload), "messages", ToMessage);
    }

    void Eqs::DeleteMessage(const std::string &receiptHandle) const {
        std::ignore = Call("delete-message", {{"receiptHandle", receiptHandle}});
    }

    void Eqs::DeleteMessageById(const std::string &messageId) const {
        std::ignore = Call("delete-message", {{"messageId", messageId}});
    }

    MessageCount Eqs::GetMessageCount(const std::string &ern) const {
        return ToMessageCount(Call("get-message-count", {{"ern", ern}}));
    }

    MessageMetadata Eqs::GetMessageMetadata(const std::string &messageId) const {
        return ToMessageMetadata(Call("get-message-metadata", {{"messageId", messageId}}));
    }

    void Eqs::SetMessageVisibility(const std::string &messageId, const long visibility) const {
        std::ignore = Call("set-message-visibility", {{"messageId", messageId}, {"visibility", visibility}});
    }

    MessageAttribute Eqs::GetMessageAttribute(const std::string &messageId, const std::string &name) const {
        return ToMessageAttribute(Call("get-message-attribute", {{"messageId", messageId}, {"name", name}}));
    }

    MessageAttribute Eqs::SetMessageAttribute(const std::string &messageId, const std::string &name, const COM::Variant &value) const {
        // "key" going out and "name" coming back, which is the server's own asymmetry.
        const boost::json::object payload{{"messageId", messageId}, {"key", name}, {"value", value.ToJson()}};
        return ToMessageAttribute(Call("set-message-attribute", payload));
    }

    // -- monitoring ---------------------------------------------------------------------------------

    boost::json::object Eqs::Metrics() const {
        return Call("get-metrics");
    }

    Eqs Eqs::AsInternal() const {
        return {Session(), {{"x-euclid-internal", "true"}}};
    }

    // -- transport ----------------------------------------------------------------------------------

    Page<Message> Eqs::Receive(const std::string &queueErn, const long maxMessages, const std::chrono::seconds waitTime) const {

        const boost::json::object payload{
                {"ern", queueErn},
                {"maxCount", maxMessages},
                {"waitTime", waitTime.count()},
        };

        const auto timeout = waitTime > std::chrono::seconds::zero()
                                     ? std::chrono::duration_cast<std::chrono::milliseconds>(waitTime) + LongPollResponseMargin
                                     : std::chrono::milliseconds::zero();

        return ToPage<Message>(Result("receive-messages", Post("receive-messages", payload, {}, timeout)), "messages", ToMessage);
    }

}// namespace Euclid::CDK::EQS
