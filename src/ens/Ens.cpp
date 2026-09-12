// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/ens/Ens.h>

namespace Euclid::CDK::ENS {

    Ens::Ens(const EAM::Session &session) : ModuleClient(session, std::string(ENS::Target)) {}

    // -- topics -------------------------------------------------------------------------------------

    CreateTopicResult Ens::CreateTopic(const std::string &name, const long maxMessageLength) const {
        return ToCreateTopicResult(Call("create-topic", {{"name", name}, {"maxMessageLength", maxMessageLength}}));
    }

    void Ens::DeleteTopic(const std::string &ern) const {
        std::ignore = Call("delete-topic", {{"ern", ern}});
    }

    Page<Topic> Ens::ListTopics(const ListOptions &options) const {
        return ToPage<Topic>(Call("list-topics", ListPayload(options, "name")), "topics", ToTopic);
    }

    std::string Ens::GetTopicErn(const std::string &name) const {
        return TextOf("get-topic-ern", {{"name", name}}, "ern");
    }

    TopicMetadata Ens::GetTopicMetadata(const std::string &ern) const {
        return ToTopicMetadata(Call("get-topic-metadata", {{"ern", ern}}));
    }

    TopicStateResult Ens::StopTopic(const std::string &ern) const {
        return SetTopicState("stop-topic", ern);
    }

    TopicStateResult Ens::StartTopic(const std::string &ern) const {
        return SetTopicState("start-topic", ern);
    }

    TopicStateResult Ens::SetTopicState(const std::string &action, const std::string &ern) const {
        return ToTopicStateResult(Call(action, {{"ern", ern}}));
    }

    TopicRetentionResult Ens::SetTopicRetention(const std::string &ern, const long retentionPeriod) const {
        // -1 is the one negative that means something: keep everything. Anything below it is a typo
        // the server refuses too, said here so that it costs no round trip.
        if (retentionPeriod < RetentionForever) {
            throw EuclidError("retentionPeriod has to be seconds, 0 to follow the installation default, "
                              "or -1 to keep messages forever");
        }
        return ToTopicRetentionResult(Call("set-topic-retention", {{"ern", ern}, {"retentionPeriod", retentionPeriod}}));
    }

    void Ens::PurgeTopic(const std::string &ern) const {
        std::ignore = Call("purge-topic", {{"ern", ern}});
    }

    void Ens::PurgeAllTopics(const PurgeAllTopicsOptions &options) const {
        const boost::json::object payload{
                {"region", options.region.empty() ? Session().Region() : options.region},
                {"accountId", options.accountId.empty() ? Session().AccountId() : options.accountId},
                // The session's namespace unless the caller said otherwise, empty included: an empty
                // namespace is what the server reads as "every namespace of the account", so it has
                // to be possible to ask for it deliberately.
                {"nameSpace", options.nameSpaceSet ? options.nameSpace : Session().Namespace()},
        };
        std::ignore = Call("purge-all-topics", payload);
    }

    void Ens::AddTopicTag(const std::string &ern, const std::string &key, const std::string &value) const {
        std::ignore = Call("add-topic-tag", {{"ern", ern}, {"key", key}, {"value", value}});
    }

    void Ens::SetTopicTag(const std::string &ern, const std::string &key, const std::string &value) const {
        std::ignore = Call("set-topic-tag", {{"ern", ern}, {"key", key}, {"value", value}});
    }

    void Ens::DeleteTopicTag(const std::string &ern, const std::string &key) const {
        std::ignore = Call("delete-topic-tag", {{"ern", ern}, {"key", key}});
    }

    // -- messages -----------------------------------------------------------------------------------

    std::string Ens::PublishMessage(const std::string &topicErn, const std::string &body, const PublishMessageOptions &options) const {

        boost::json::object payload{
                {"ern", topicErn},
                {"body", body},
                {"attributes", COM::VariantMapToJson(options.attributes)},
        };
        // Left out entirely when there is nothing to say about it, so the topic's own default is
        // what applies rather than an empty string the server would have to interpret.
        if (!options.priority.empty()) payload["priority"] = options.priority;

        return TextOf("publish-message", payload, "messageId");
    }

    Page<Message> Ens::ListMessages(const std::string &topicErn, const PageOptions &options) const {
        auto payload = PagePayload(options, "created");
        payload["topicErn"] = topicErn;
        return ToPage<Message>(Call("list-messages", payload), "messages", ToMessage);
    }

    MessageCount Ens::GetMessageCount(const std::string &ern) const {
        return ToMessageCount(Call("get-message-count", {{"ern", ern}}));
    }

    MessageAttribute Ens::GetMessageAttribute(const std::string &messageId, const std::string &key) const {
        return ToMessageAttribute(Call("get-message-attribute", {{"messageId", messageId}, {"key", key}}));
    }

    MessageAttribute Ens::SetMessageAttribute(const std::string &messageId, const std::string &key, const COM::Variant &value) const {
        const boost::json::object payload{{"messageId", messageId}, {"key", key}, {"value", value.ToJson()}};
        return ToMessageAttribute(Call("set-message-attribute", payload));
    }

    // -- subscriptions ------------------------------------------------------------------------------

    COM::SubscribeResult Ens::Subscribe(const std::string &topicErn, const std::string &targetErn, const std::string &targetType) const {
        const boost::json::object payload{{"sourceErn", topicErn}, {"type", targetType}, {"targetErn", targetErn}};
        return COM::ToSubscribeResult(Call("subscribe", payload));
    }

    void Ens::Unsubscribe(const std::string &ern) const {
        std::ignore = Call("unsubscribe", {{"ern", ern}});
    }

    std::vector<COM::Subscription> Ens::ListSubscriptions(const std::string &topicErn) const {
        std::vector<COM::Subscription> subscriptions;
        for (const auto &document: Json::Documents(Call("list-subscriptions", {{"topicErn", topicErn}}), "subscriptions")) {
            subscriptions.push_back(COM::ToSubscription(document));
        }
        return subscriptions;
    }

    // -- monitoring ---------------------------------------------------------------------------------

    boost::json::object Ens::Metrics() const {
        return Call("get-metrics");
    }

}// namespace Euclid::CDK::ENS
