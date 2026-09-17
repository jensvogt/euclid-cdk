// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EQS and ENS end to end: a topic, a queue subscribed to it, and a message that travels.
 *
 * @par
 * @code
 * messaging-walkthrough https://euclid.example.com jens secret
 * @endcode
 *
 * @par
 * Works in a queue and a topic of its own, named after the moment it started, and deletes both again
 * at the end - so it is safe to point at a running deployment, and a run that dies halfway leaves
 * two obviously disposable resources behind rather than touching anything of yours.
 */

// C++ includes
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

// Euclid includes
#include <euclid/cdk/Euclid.h>

using namespace Euclid::CDK;

namespace {

    /**
     * @brief Everything between creating the two and deleting them.
     */
    void walk(const EQS::Eqs &eqs, const ENS::Ens &ens, const std::string &queueErn, const std::string &topicErn) {

        // Sent straight to the queue: one message, one consumer, and the lease below is what makes
        // it exactly one.
        const auto messageId = eqs.SendMessage(queueErn, R"({"order": 17})",
                                               {.attributes = {{"tenant", "acme"}},
                                                .priority = std::string(COM::PriorityHigh)});
        std::cout << "\nsent    " << messageId << " to the queue\n";

        // Published to the topic instead, which delivers a copy to every subscription on it.
        const auto subscription = ens.Subscribe(topicErn, queueErn);
        std::cout << "subscribed the queue to the topic as " << subscription.ern << "\n";
        std::ignore = ens.PublishMessage(topicErn, R"({"order": 18})", {.attributes = {{"tenant", "acme"}}});
        std::cout << "published one message to the topic, which delivers it to the queue\n";

        const auto counts = eqs.GetMessageCount(queueErn);
        std::cout << "\nqueue holds " << counts.total << ": " << counts.available << " available, "
                  << counts.delayed << " delayed, " << counts.invisible << " in flight\n";

        // A long poll: the server holds this open until something lands or the window runs out, so
        // the delivery from the topic is waited for rather than polled for.
        const auto [total, items] = eqs.ReceiveMessages(queueErn, {.maxMessages = 10, .waitTime = std::chrono::seconds(10)});
        std::cout << "\nreceived " << items.size() << " message(s):\n";

        for (const auto &message: items) {
            std::cout << "  " << std::setw(38) << std::left << message.messageId
                      << std::setw(8) << message.priority << message.body << "\n"
                      << "      attributes:";
            for (const auto &[name, value]: message.attributes) std::cout << " " << name << "=" << value.ToString();
            std::cout << "\n";
            // After the work, not before: a consumer that dies instead simply stops holding the
            // lease, and the message comes back for somebody else.
            eqs.DeleteMessage(message.receiptHandle);
            std::cout << "      deleted with its receipt handle\n";
        }

        // Holding delivery: a stopped topic still accepts what is published to it, which is what
        // makes this a way of pausing a subscriber rather than a way of losing messages.
        const auto stopped = ens.StopTopic(topicErn);
        std::cout << "\nstopped the topic: status " << stopped.status << "\n";
        std::ignore = ens.PublishMessage(topicErn, R"({"order": 19})");
        std::ignore = ens.PublishMessage(topicErn, R"({"order": 20})");
        std::cout << "  published 2 more: " << ens.GetTopicMetadata(topicErn).held
                  << " message(s) held, nothing on the queue yet\n";

        // The fan-out happens inside this call, oldest first, and "released" is what went.
        const auto restarted = ens.StartTopic(topicErn);
        std::cout << "started it again: status " << restarted.status << ", released " << restarted.released
                  << " held message(s)\n"
                  << "  queue now holds " << eqs.GetMessageCount(queueErn).available << " available message(s)\n";

        // Worth setting on any topic that is published to regularly: a topic is fanned out at
        // publish time, so nothing else ever removes what it keeps.
        const auto retention = ens.SetTopicRetention(topicErn, 7 * 24 * 60 * 60);
        std::cout << "\nretention set to " << retention.retentionPeriod
                  << "s - applies to what is published from now on\n";

        const auto topicCounts = ens.GetMessageCount(topicErn);
        std::cout << "topic counters: " << topicCounts.available << " on the topic, " << topicCounts.send
                  << " sent, " << topicCounts.resend << " resent\n";

        std::cout << "subscriptions: ";
        for (const auto &entry: ens.ListSubscriptions(topicErn)) std::cout << entry.targetErn << " ";
        std::cout << "\n";

        ens.Unsubscribe(subscription.ern);
        std::cout << "unsubscribed " << subscription.ern << "\n";

        std::cout << "\nqueue now holds " << eqs.GetMessageCount(queueErn).total << " message(s)\n";
    }

}// namespace

int main(const int argc, char *argv[]) {

    if (argc < 4) {
        std::cerr << "usage: " << argv[0] << " <server-url> <user-id> <password> [namespace]\n"
                  << "   e.g. " << argv[0] << " https://euclid.example.com jens secret reports\n";
        return 2;
    }

    const std::string baseUrl = argv[1];
    const std::string nameSpace = argc > 4 ? argv[4] : "";
    const auto name = "cdk-walkthrough-" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                                  std::chrono::system_clock::now().time_since_epoch())
                                                                  .count());

    try {
        auto builder = EAM::Eam::ForServer(baseUrl).Credentials(argv[2], argv[3]);
        if (!nameSpace.empty()) builder.Namespace(nameSpace);

        const auto session = builder.Login();
        const EQS::Eqs eqs(session);
        const ENS::Ens ens(session);

        std::cout << "logged in to " << session.BaseUrl() << " as " << session.UserId()
                  << " (" << session.AccountId() << ", " << session.Region() << ")\n";

        const auto queue = eqs.CreateQueue(name, {.visibility = 30});
        const auto topic = ens.CreateTopic(name);
        std::cout << "created queue " << queue.name << "\n  " << queue.ern << "\n"
                  << "created topic " << topic.name << "\n  " << topic.ern << "\n";

        try {
            walk(eqs, ens, queue.ern, topic.ern);
        } catch (...) {
            ens.DeleteTopic(topic.ern);
            eqs.PurgeQueue(queue.ern);
            eqs.DeleteQueue(queue.ern);
            throw;
        }

        ens.DeleteTopic(topic.ern);
        eqs.PurgeQueue(queue.ern);
        eqs.DeleteQueue(queue.ern);
        std::cout << "\ndeleted queue and topic " << name << " again\n";

        std::cout << "\nSDK version " << Version << "\n";
        return 0;

    } catch (const AuthenticationError &ex) {
        std::cerr << "login refused: " << ex.what() << "\n";
        return 1;
    } catch (const ServiceError &ex) {
        std::cerr << ex.Target() << "/" << ex.Action() << " failed: " << ex.Reason() << "\n";
        return 1;
    } catch (const EuclidError &ex) {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
