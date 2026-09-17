// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <thread>
#include <vector>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/dto/Emo.h>
#include <euclid/cdk/emo/Emo.h>

namespace Euclid::CDK::EMO {

    namespace detail {
        struct CounterState;
        struct GaugeState;
        struct TimerState;
    }// namespace detail

    /**
     * @brief A count of things that happened, reported per step and then started again.
     *
     * @par
     * A handle rather than the meter itself: the registry owns what this points at, so copying one
     * of these is free and every copy counts into the same place. Incrementing is safe from any
     * thread.
     */
    class EUCLID_CDK_API Counter {
    public:

        /**
         * @brief Counts one more, or amount more.
         */
        void Increment(double amount = 1.0) const;

        /**
         * @brief What has accumulated since the last publish. For a test or a log line - the
         * registry reads and resets this itself.
         */
        [[nodiscard]]
        double Count() const;

    private:

        friend class MeterRegistry;
        explicit Counter(std::shared_ptr<detail::CounterState> state);

        std::shared_ptr<detail::CounterState> _state;
    };

    /**
     * @brief A value that stands on its own whenever it is read - a queue depth, a pool size.
     *
     * @par
     * Not reset by a publish, because a gauge is what it is rather than what happened: a depth of
     * nine is still nine after somebody has looked at it.
     */
    class EUCLID_CDK_API Gauge {
    public:

        /**
         * @brief Sets what this gauge reads now.
         */
        void Set(double value) const;

        /**
         * @brief What it last read.
         */
        [[nodiscard]]
        double Value() const;

    private:

        friend class MeterRegistry;
        explicit Gauge(std::shared_ptr<detail::GaugeState> state);

        std::shared_ptr<detail::GaugeState> _state;
    };

    /**
     * @brief How long something took, and how often it was done.
     *
     * @par
     * Published as three series, named the way euclid-jdk's Micrometer registry names them so that a
     * C++ application's timings graph beside a Java one's: "<name>.count" and "<name>.total" as
     * rates, and "<name>.max" as a gauge. Durations are milliseconds, which is the base unit
     * euclid-jdk publishes in and the one euclid's own modules time in.
     *
     * @par
     * There are no percentiles, and that is a property of where this goes rather than an omission:
     * EMO stores a value per series per interval, so a p99 would have to be computed here and pushed
     * as a series of its own. The count, the total and the worst case are what the JDK registry
     * publishes too.
     */
    class EUCLID_CDK_API Timer {
    public:

        /**
         * @brief Records one timing.
         */
        void Record(std::chrono::nanoseconds elapsed) const;

        /**
         * @brief Times a callable, records it however it ends, and answers with whatever it
         * answered.
         *
         * @par
         * A throwing callable is timed too and the exception goes on - a call that failed slowly is
         * exactly the one worth having timed.
         */
        template<typename Callable>
        decltype(auto) Time(Callable &&callable) const {
            const ScopedMeasurement measurement(*this);
            return std::forward<Callable>(callable)();
        }

        /**
         * @brief How many timings since the last publish.
         */
        [[nodiscard]]
        long Count() const;

    private:

        friend class MeterRegistry;
        explicit Timer(std::shared_ptr<detail::TimerState> state);

        /**
         * @brief Times from here to the end of the scope, which is what Time() is built on.
         */
        class ScopedMeasurement {
        public:

            explicit ScopedMeasurement(const Timer &timer) : _timer(timer), _start(std::chrono::steady_clock::now()) {}

            ~ScopedMeasurement() { _timer.Record(std::chrono::steady_clock::now() - _start); }

            ScopedMeasurement(const ScopedMeasurement &) = delete;
            ScopedMeasurement &operator=(const ScopedMeasurement &) = delete;

        private:

            const Timer &_timer;
            std::chrono::steady_clock::time_point _start;
        };

        std::shared_ptr<detail::TimerState> _state;
    };

    /**
     * @brief Times a scope, and records it on the way out.
     *
     * @par
     * @code
     * void Handle(const Request &request) {
     *     const EMO::ScopedTimer measure(requestTimer);
     *     ...
     * }   // recorded here, by whichever path leaves the scope
     * @endcode
     *
     * @par
     * The same thing euclid's own modules do with Core::Monitoring::MonitoringTimer, and the reason
     * it is worth having: a timing written by hand at the end of a function is a timing that is not
     * taken when the function returns early or throws.
     */
    class EUCLID_CDK_API ScopedTimer {
    public:

        explicit ScopedTimer(Timer timer);

        ~ScopedTimer();

        ScopedTimer(const ScopedTimer &) = delete;
        ScopedTimer &operator=(const ScopedTimer &) = delete;

    private:

        Timer _timer;
        std::chrono::steady_clock::time_point _start;
    };

    /**
     * @brief What a registry reports under, how often, and with what on every metric.
     */
    struct EUCLID_CDK_API RegistryOptions {

        /**
         * @brief What is reporting - an application's own id. It is how a reader tells one pool's
         * numbers from another's.
         */
        std::string module;

        /**
         * @brief How long a step is: how much a counter accumulates before it is published and
         * started again.
         *
         * @par
         * One minute by default, which is what euclid-jdk's registry defaults to. Zero starts no
         * thread at all and leaves publishing to whoever calls Publish() - for an application with a
         * loop of its own, and for a test.
         */
        std::chrono::milliseconds step{std::chrono::minutes(1)};

        /**
         * @brief Labels added to every metric this registry publishes - the host, the instance, the
         * version. A label on a meter of the same name wins.
         */
        Labels commonLabels;

        /**
         * @brief Whether stopping publishes the step in hand rather than dropping it.
         */
        bool publishOnStop{true};
    };

    /**
     * @brief The meters an application keeps, and the thread that pushes them to EMO.
     *
     * @par
     * @code
     * const EMO::Emo emo(session);
     * EMO::MeterRegistry metrics(emo, {.module = "invoice-parser", .commonLabels = {{"host", hostname}}});
     *
     * const auto parsed = metrics.CounterOf("invoices.parsed");
     * const auto failed = metrics.CounterOf("invoices.parsed", {{"outcome", "failed"}});
     * const auto duration = metrics.TimerOf("invoice.parse");
     * metrics.GaugeFrom("queue.depth", [&queue] { return static_cast<double>(queue.size()); });
     *
     * for (const auto &invoice: incoming) {
     *     const EMO::ScopedTimer measure(duration);
     *     Parse(invoice) ? parsed.Increment() : failed.Increment();
     * }
     * @endcode
     *
     * @par Why this exists
     * euclid-jdk publishes an application's metrics through Micrometer, which C++ has no equivalent
     * of. What Micrometer actually provides is two things: meters an application records into, and a
     * registry that accumulates them over a step and publishes the result. Neither needs Micrometer,
     * and euclid's own C++ modules have done both for years with Core::Monitoring - this is that,
     * pushed through an authenticated session rather than a Unix socket, and named the way the JDK
     * registry names things so both land in the same series.
     *
     * @par What a step means
     * A counter and a timer report what accumulated since the last publish and start again, which is
     * what makes them rates to EMO - the thing a rollup sums. A gauge reports what it reads at the
     * moment of publishing and is not reset. Every registered meter is published every step,
     * including the ones that did not move: a zero is a fact, and a gap in a graph is not.
     *
     * @par What it costs
     * Every meter is one stored row per step, forever - so the number of label combinations is the
     * number of series, and a label carrying a request id or a customer name is how a monitoring
     * database is filled up. Decide the labels where the meter is created, which is the one place
     * that can.
     *
     * @par Failure
     * A push that fails is counted in FailedPublishes() and the batch is dropped. It is not retried:
     * a rate sent twice is counted twice, and a monitoring system that lies about throughput is
     * worse than one with a gap in it. Nothing here throws at the application - a process does not
     * stop because it could not say how it was doing.
     *
     * @par
     * Safe to use from any thread: incrementing a counter and timing a scope are atomic, and
     * creating a meter takes a lock that nothing on the recording path holds.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API MeterRegistry {
    public:

        /**
         * @brief Builds a registry that publishes through this client, and starts its thread unless
         * the step is zero.
         *
         * @param emo     the client the batches are pushed through; it has to outlive the registry.
         * @param options what this reports under, how often, and with what labels.
         * @throws EuclidError if no module name was given: a batch has to say what is reporting.
         */
        MeterRegistry(const Emo &emo, RegistryOptions options);

        /**
         * @brief Stops the thread, publishing the step in hand unless told not to.
         */
        ~MeterRegistry();

        MeterRegistry(const MeterRegistry &) = delete;
        MeterRegistry &operator=(const MeterRegistry &) = delete;

        /**
         * @brief The counter of this name and these labels, creating it the first time.
         *
         * @par
         * Asking again for the same pair answers the same meter, so a handle need not be passed
         * around - though keeping one is cheaper than looking it up on a hot path.
         */
        [[nodiscard]]
        Counter CounterOf(const std::string &name, const Labels &labels = {});

        /**
         * @brief The gauge of this name and these labels, creating it the first time.
         */
        [[nodiscard]]
        Gauge GaugeOf(const std::string &name, const Labels &labels = {});

        /**
         * @brief A gauge that reads itself, by asking at every publish.
         *
         * @par
         * For what something already knows - a queue's depth, a pool's size - where setting a gauge
         * would mean remembering to. The supplier is called on the publishing thread, so it should
         * answer quickly and not throw; one that throws is skipped for that step.
         */
        void GaugeFrom(const std::string &name, std::function<double()> supplier, const Labels &labels = {});

        /**
         * @brief The timer of this name and these labels, creating it the first time.
         */
        [[nodiscard]]
        Timer TimerOf(const std::string &name, const Labels &labels = {});

        /**
         * @brief The step in hand, as metrics - which takes counters and timers and starts them
         * again.
         *
         * @par
         * Publish() is this plus the push. Exposed because it is what a test asserts on, and what an
         * application that would rather send the batch itself asks for.
         */
        [[nodiscard]]
        std::vector<Metric> Collect();

        /**
         * @brief Collects the step in hand and pushes it.
         *
         * @par
         * Never throws: a failure is counted and the batch dropped.
         */
        void Publish();

        /**
         * @brief How many batches have been pushed, and how many failed to be.
         *
         * @par
         * The answer to "is the monitoring working", which nothing else here can give - a metric
         * about pushing metrics cannot be pushed.
         */
        [[nodiscard]]
        long Publishes() const;

        [[nodiscard]]
        long FailedPublishes() const;

    private:

        /**
         * @brief What identifies a meter: its name and the labels it was created with.
         */
        using MeterKey = std::pair<std::string, Labels>;

        /**
         * @brief The thread's loop - publish, wait a step, publish again, until stopped.
         */
        void Run();

        /**
         * @brief Stops the thread, and publishes what is in hand if that was asked for.
         */
        void Stop();

        /**
         * @brief The common labels, with a meter's own laid over them.
         */
        [[nodiscard]]
        Labels LabelsFor(const Labels &labels) const;

        const Emo &_emo;
        RegistryOptions _options;

        mutable std::mutex _mutex;
        std::map<MeterKey, std::shared_ptr<detail::CounterState>> _counters;
        std::map<MeterKey, std::shared_ptr<detail::GaugeState>> _gauges;
        std::map<MeterKey, std::shared_ptr<detail::TimerState>> _timers;
        std::map<MeterKey, std::function<double()>> _suppliers;

        std::atomic<long> _publishes{0};
        std::atomic<long> _failed{0};

        std::mutex _sleepMutex;
        std::condition_variable _wakeUp;
        bool _stopped{false};
        std::thread _thread;
    };

}// namespace Euclid::CDK::EMO
