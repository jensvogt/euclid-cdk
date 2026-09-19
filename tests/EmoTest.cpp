// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief EMO, end to end against a fake euclid server - and the registry that feeds it.
 *
 * @par
 * The client's half is the usual: what went on the wire. The registry's half is what a graph would
 * otherwise lie about - that a counter reports the step rather than its life, that a gauge does the
 * opposite, and that a timer lands in the three series euclid-jdk publishes it as.
 */

// C++ includes
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

// Boost includes
#include <boost/json.hpp>
#include <boost/test/unit_test.hpp>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/emo/Emo.h>
#include <euclid/cdk/emo/MeterRegistry.h>

#include "FakeGateway.h"
#include "TestSupport.h"

using namespace Euclid::CDK;
using Euclid::CDK::Test::FakeGateway;

namespace {

    /**
     * @brief A gateway, a logged-in session and an EMO client on it - declared in the order that
     * lets each refer to the one before it.
     */
    struct EmoClient {

        explicit EmoClient(FakeGateway::Handler handler)
            : gateway(std::move(handler)), session(Test::Builder(gateway).Login()), emo(session) {}

        FakeGateway gateway;
        EAM::Session session;
        EMO::Emo emo;
    };

    boost::json::value lastBody(const FakeGateway &gateway) {
        return boost::json::parse(gateway.LastRequest().body());
    }

    /**
     * @brief The items of the last push, by metric name.
     */
    std::map<std::string, boost::json::object> pushedByName(const FakeGateway &gateway) {
        // The body in a variable of its own, not iterated straight out of lastBody(): a temporary in
        // a range-for's *subexpression* is not lifetime-extended, so the array would be read out of
        // a document that had already gone.
        const auto body = lastBody(gateway);

        std::map<std::string, boost::json::object> items;
        for (const auto &item: body.at("items").as_array()) {
            items[std::string(item.at("name").as_string())] = item.as_object();
        }
        return items;
    }

    /**
     * @brief One metric of a collected batch, by name.
     */
    const EMO::Metric *find(const std::vector<EMO::Metric> &batch, const std::string &name) {
        for (const auto &metric: batch) {
            if (metric.name == name) return &metric;
        }
        return nullptr;
    }

}// namespace

BOOST_AUTO_TEST_SUITE(EmoClientTest)

    BOOST_AUTO_TEST_CASE(PushesABatchUnderTheNameThatIsReporting) {
        const EmoClient client(Test::Answering());

        client.emo.PushMetrics("invoice-parser", {EMO::Metric::Rate("invoices.parsed", 41, {{"outcome", "ok"}}),
                                                  EMO::Metric::Gauge("queue.depth", 7)});

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("module").as_string() == "invoice-parser");
        BOOST_REQUIRE(body.at("items").as_array().size() == 2U);

        const auto items = pushedByName(client.gateway);
        // The type is not decoration: it is what says whether a rollup sums or averages.
        BOOST_TEST(items.at("invoices.parsed").at("type").as_string() == std::string(EMO::TypeRate));
        BOOST_TEST(items.at("invoices.parsed").at("value").as_double() == 41.0);
        BOOST_TEST(items.at("invoices.parsed").at("labels").at("outcome").as_string() == "ok");
        BOOST_TEST(items.at("queue.depth").at("type").as_string() == std::string(EMO::TypeGauge));
        BOOST_TEST(items.at("queue.depth").at("labels").as_object().empty());

        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-target"]) == "emo");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "push-metrics");
    }

    // Nothing to record, and a request that says so is a round trip for nothing - which matters for
    // a call that runs on a timer forever.
    BOOST_AUTO_TEST_CASE(SendsNothingWhenThereIsNothingToSend) {
        const EmoClient client(Test::Answering());

        client.emo.PushMetrics("invoice-parser", {});
        BOOST_TEST(client.gateway.Received().size() == 1U);// the login, and nothing else
    }

    BOOST_AUTO_TEST_CASE(ReadsTheRowsAListingAnswersWith) {
        const auto listed = boost::json::serialize(boost::json::object{
                {"items", boost::json::array{
                                  boost::json::object{
                                          {"name", "invoices.parsed"},
                                          {"labels", boost::json::object{{"outcome", "ok"}}},
                                          {"labelName", "outcome"},
                                          {"labelValue", "ok"},
                                          {"value", 41.5},
                                          {"minValue", 0.25},
                                          {"maxValue", 96.5},
                                          {"samples", 12},
                                          {"type", "RATE"},
                                          {"resolution", "HOUR"},
                                          {"timestamp", "2026-09-17T10:00:00Z"}},
                                  boost::json::object{{"name", "queue.depth"}, {"type", "GAUGE"}}}}});

        const EmoClient client(Test::Answering(listed));

        const auto rows = client.emo.List({.name = "invoices.parsed",
                                           .labels = {{"outcome", "ok"}},
                                           .limit = 50,
                                           .resolution = std::string(EMO::ResolutionHour)});
        BOOST_REQUIRE(rows.size() == 2U);
        BOOST_TEST(rows[0].name == "invoices.parsed");
        BOOST_TEST(rows[0].labels.at("outcome") == "ok");
        // Read as doubles, not counts: half a millisecond is not no milliseconds.
        BOOST_TEST(rows[0].value == 41.5);
        BOOST_TEST(rows[0].minValue == 0.25);
        BOOST_TEST(rows[0].maxValue == 96.5);
        BOOST_TEST(rows[0].samples == 12L);
        // Upper case coming back, lower case going out - the server's own spelling, both ways.
        BOOST_TEST(rows[0].type == std::string(EMO::StoredRate));
        BOOST_TEST(rows[0].resolution == std::string(EMO::ResolutionHour));
        BOOST_TEST(rows[1].type == std::string(EMO::StoredGauge));

        const auto body = lastBody(client.gateway);
        BOOST_TEST(body.at("name").as_string() == "invoices.parsed");
        BOOST_TEST(body.at("labels").at("outcome").as_string() == "ok");
        BOOST_TEST(body.at("limit").as_int64() == 50);
        BOOST_TEST(body.at("resolution").as_string() == "HOUR");
    }

    // A query that says nothing narrows nothing: an absent field is not an empty one.
    BOOST_AUTO_TEST_CASE(AnEmptyQueryAsksForNothingInParticular) {
        const EmoClient client(Test::Answering(R"({"items": []})"));

        BOOST_TEST(client.emo.List().empty());

        const auto body = lastBody(client.gateway).as_object();
        BOOST_TEST(body.empty());
        BOOST_TEST(!body.contains("name"));
        BOOST_TEST(!body.contains("limit"));
    }

    BOOST_AUTO_TEST_CASE(ReadsAnAverageAsOneNumber) {
        const EmoClient client(Test::Answering(R"({"average": 12.75})"));

        BOOST_TEST(client.emo.Average({.name = "invoice.parse.total", .from = "2026-09-17T00:00:00Z"}) == 12.75);
        BOOST_TEST(lastBody(client.gateway).at("from").as_string() == "2026-09-17T00:00:00Z");
        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "average");
    }

    BOOST_AUTO_TEST_CASE(SignsItsOwnTargetAndCarriesTheServersReason) {
        const FakeGateway gateway(Test::Authenticated([](const Request &request) {
            if (std::string(request["x-euclid-action"]) == "list") {
                return FakeGateway::Json(403, R"({"error": "Administrator privileges required"})");
            }
            return FakeGateway::Json(200, "{}");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EMO::Emo emo(session);

        emo.PushMetrics("invoice-parser", {EMO::Metric::Gauge("queue.depth", 1)});
        BOOST_REQUIRE(SigningScheme::Of(gateway.LastRequest()) != nullptr);

        // Reading everybody's numbers is a different question from publishing your own.
        try {
            std::ignore = emo.List();
            BOOST_FAIL("expected a ServiceError");
        } catch (const ServiceError &ex) {
            BOOST_TEST(ex.Target() == "emo");
            BOOST_TEST(ex.Action() == "list");
            BOOST_TEST(ex.Status() == 403);
        }
    }

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(EmoRegistryTest)

    // A counter reports the step and starts again, which is what makes it a rate; a gauge reports
    // what it reads and keeps reading it.
    BOOST_AUTO_TEST_CASE(ACounterIsTheStepAndAGaugeIsTheValue) {
        const EmoClient client(Test::Answering());
        EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});

        const auto parsed = registry.CounterOf("invoices.parsed");
        const auto depth = registry.GaugeOf("queue.depth");

        parsed.Increment();
        parsed.Increment(4);
        depth.Set(7);

        auto batch = registry.Collect();
        BOOST_TEST(find(batch, "invoices.parsed")->value == 5.0);
        BOOST_TEST(find(batch, "invoices.parsed")->type == std::string(EMO::TypeRate));
        BOOST_TEST(find(batch, "queue.depth")->value == 7.0);
        BOOST_TEST(find(batch, "queue.depth")->type == std::string(EMO::TypeGauge));

        // The second step: the counter starts from nothing, the gauge still reads what it reads.
        batch = registry.Collect();
        BOOST_TEST(find(batch, "invoices.parsed")->value == 0.0);
        BOOST_TEST(find(batch, "queue.depth")->value == 7.0);
    }

    // The three series euclid-jdk publishes a timer as, under the same names and in milliseconds -
    // so a C++ application's timings graph beside a Java one's.
    BOOST_AUTO_TEST_CASE(ATimerIsACountATotalAndAMax) {
        const EmoClient client(Test::Answering());
        EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});

        const auto parse = registry.TimerOf("invoice.parse");
        parse.Record(std::chrono::milliseconds(10));
        parse.Record(std::chrono::milliseconds(30));
        // Sub-millisecond, which a timer that rounded to whole milliseconds would report as nothing.
        parse.Record(std::chrono::microseconds(400));

        const auto batch = registry.Collect();
        BOOST_REQUIRE(find(batch, "invoice.parse.count") != nullptr);
        BOOST_TEST(find(batch, "invoice.parse.count")->value == 3.0);
        BOOST_TEST(find(batch, "invoice.parse.count")->type == std::string(EMO::TypeRate));
        BOOST_TEST(find(batch, "invoice.parse.total")->value == 40.4);
        BOOST_TEST(find(batch, "invoice.parse.total")->type == std::string(EMO::TypeRate));
        BOOST_TEST(find(batch, "invoice.parse.max")->value == 30.0);
        // The worst case is a gauge: averaging maxima is meaningful, summing them is not.
        BOOST_TEST(find(batch, "invoice.parse.max")->type == std::string(EMO::TypeGauge));

        // And the step starts again, maximum included.
        const auto next = registry.Collect();
        BOOST_TEST(find(next, "invoice.parse.count")->value == 0.0);
        BOOST_TEST(find(next, "invoice.parse.max")->value == 0.0);
    }

    BOOST_AUTO_TEST_CASE(AScopedTimerRecordsWhicheverWayTheScopeIsLeft) {
        const EmoClient client(Test::Answering());
        EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});

        const auto parse = registry.TimerOf("invoice.parse");

        {
            const EMO::ScopedTimer measure(parse);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        // Thrown out of rather than returned from, which is the case a hand-written timing misses.
        try {
            const EMO::ScopedTimer measure(parse);
            throw EuclidError("parse failed");
        } catch (const EuclidError &) {
        }

        BOOST_TEST(parse.Count() == 2L);
        BOOST_TEST(find(registry.Collect(), "invoice.parse.total")->value >= 2.0);
    }

    BOOST_AUTO_TEST_CASE(TimesACallableAndAnswersWithWhatItAnswered) {
        const EmoClient client(Test::Answering());
        EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});

        const auto parse = registry.TimerOf("invoice.parse");
        BOOST_TEST(parse.Time([] { return 17; }) == 17);
        BOOST_TEST(parse.Count() == 1L);
    }

    // The same name and labels is the same meter; a different label is a different series.
    BOOST_AUTO_TEST_CASE(LabelsAreWhatSplitsOneSeriesIntoSeveral) {
        const EmoClient client(Test::Answering());
        EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser",
                                                 .step = std::chrono::milliseconds::zero(),
                                                 .commonLabels = {{"host", "laptop"}, {"outcome", "unset"}}});

        registry.CounterOf("invoices.parsed", {{"outcome", "ok"}}).Increment(2);
        registry.CounterOf("invoices.parsed", {{"outcome", "ok"}}).Increment();
        registry.CounterOf("invoices.parsed", {{"outcome", "failed"}}).Increment();

        const auto batch = registry.Collect();
        long series = 0;
        for (const auto &metric: batch) {
            if (metric.name != "invoices.parsed") continue;
            ++series;
            // The common labels are on every metric, and a meter's own wins where they collide.
            BOOST_TEST(metric.labels.at("host") == "laptop");
            if (metric.labels.at("outcome") == "ok") BOOST_TEST(metric.value == 3.0);
            if (metric.labels.at("outcome") == "failed") BOOST_TEST(metric.value == 1.0);
        }
        BOOST_TEST(series == 2L);
    }

    // For what something already knows, where setting a gauge would mean remembering to.
    BOOST_AUTO_TEST_CASE(AGaugeCanReadItself) {
        const EmoClient client(Test::Answering());
        EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});

        std::atomic<int> depth{4};
        registry.GaugeFrom("queue.depth", [&depth] { return static_cast<double>(depth.load()); });

        BOOST_TEST(find(registry.Collect(), "queue.depth")->value == 4.0);
        depth = 9;
        BOOST_TEST(find(registry.Collect(), "queue.depth")->value == 9.0);
    }

    // A gauge over an empty collection reads NaN, and one that threw read nothing at all. Neither
    // is a number a rollup can average, so neither is sent.
    BOOST_AUTO_TEST_CASE(WhatIsNotANumberIsNotPushed) {
        const EmoClient client(Test::Answering());
        EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});

        registry.GaugeFrom("mean.age", [] { return std::nan(""); });
        registry.GaugeFrom("queue.depth", [] { return 3.0; });
        registry.GaugeFrom("unreadable", []() -> double { throw EuclidError("no"); });

        const auto batch = registry.Collect();
        BOOST_TEST(find(batch, "mean.age") == nullptr);
        BOOST_TEST(find(batch, "unreadable") == nullptr);
        // And the one that could be read still is: one bad gauge does not lose the batch.
        BOOST_TEST(find(batch, "queue.depth")->value == 3.0);
    }

    BOOST_AUTO_TEST_CASE(PublishingSendsTheStepUnderTheModulesName) {
        const EmoClient client(Test::Answering());
        EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});

        registry.CounterOf("invoices.parsed").Increment(3);
        registry.Publish();

        BOOST_TEST(registry.Publishes() == 1L);
        BOOST_TEST(registry.FailedPublishes() == 0L);
        BOOST_TEST(lastBody(client.gateway).at("module").as_string() == "invoice-parser");
        BOOST_TEST(pushedByName(client.gateway).at("invoices.parsed").at("value").as_double() == 3.0);
    }

    // A process does not stop because it could not say how it was doing.
    BOOST_AUTO_TEST_CASE(APushThatFailsIsCountedRatherThanThrown) {
        const FakeGateway gateway(Test::Authenticated([](const Request &) {
            return FakeGateway::Json(503, R"({"error": "monitoring is down"})");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EMO::Emo emo(session);
        EMO::MeterRegistry registry(emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});

        registry.CounterOf("invoices.parsed").Increment();
        BOOST_CHECK_NO_THROW(registry.Publish());

        BOOST_TEST(registry.Publishes() == 0L);
        BOOST_TEST(registry.FailedPublishes() == 1L);
    }

    // The step in hand goes out on the way down rather than being lost with the process.
    BOOST_AUTO_TEST_CASE(StoppingPublishesWhatIsInHand) {
        const EmoClient client(Test::Answering());
        {
            EMO::MeterRegistry registry(client.emo, {.module = "invoice-parser", .step = std::chrono::milliseconds::zero()});
            registry.CounterOf("invoices.parsed").Increment(8);
        }

        BOOST_TEST(std::string(client.gateway.LastRequest()["x-euclid-action"]) == "push-metrics");
        BOOST_TEST(pushedByName(client.gateway).at("invoices.parsed").at("value").as_double() == 8.0);
    }

    // The thread is the whole point of a step: an application records, and something else sends.
    BOOST_AUTO_TEST_CASE(TheStepPublishesOnItsOwn) {
        std::atomic<int> pushes{0};
        const FakeGateway gateway(Test::Authenticated([&pushes](const Request &request) {
            if (std::string(request["x-euclid-action"]) == "push-metrics") ++pushes;
            return FakeGateway::Json(200, "{}");
        }));
        const auto session = Test::Builder(gateway).Login();
        const EMO::Emo emo(session);

        {
            EMO::MeterRegistry registry(emo, {.module = "invoice-parser",
                                              .step = std::chrono::milliseconds(50),
                                              .publishOnStop = false});
            registry.CounterOf("invoices.parsed").Increment();

            // Waited for rather than slept through. What this has to show is that the thread
            // publishes again and again with nobody asking it to; how many steps a machine gets
            // through in a fixed window is a property of the machine, and a push here is a real
            // round trip through a gateway that serves one connection at a time. Sleeping a fixed
            // 250ms and demanding two of them is a test that fails on a loaded runner while the
            // thread is working perfectly - which is what it did.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (pushes.load() < 2 && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        // Several steps went by and each sent once - and stopping did not add one, since it was
        // told not to.
        BOOST_TEST(pushes.load() >= 2);
    }

    // A batch has to say what is reporting, and there is no sensible default for that.
    BOOST_AUTO_TEST_CASE(ARegistryWithoutAModuleIsRefused) {
        const EmoClient client(Test::Answering());
        BOOST_CHECK_THROW(EMO::MeterRegistry(client.emo, {.module = ""}), EuclidError);
    }

BOOST_AUTO_TEST_SUITE_END()
