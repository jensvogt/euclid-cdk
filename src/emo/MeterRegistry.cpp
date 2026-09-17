// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <cmath>
#include <utility>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/emo/MeterRegistry.h>

namespace Euclid::CDK::EMO {

    namespace detail {

        /**
         * @brief What a counter holds between two publishes.
         *
         * @par
         * std::atomic<double> rather than a lock: incrementing is what an application does on its
         * hot path, and it should cost an instruction rather than a contended mutex.
         */
        struct CounterState {
            std::atomic<double> value{0.0};
        };

        /**
         * @brief What a gauge last read. Not reset by a publish.
         */
        struct GaugeState {
            std::atomic<double> value{0.0};
        };

        /**
         * @brief What a timer holds between two publishes: how many, how long in total, and the
         * worst one. Milliseconds, as euclid-jdk publishes and euclid's own modules time in.
         */
        struct TimerState {
            std::atomic<long> count{0};
            std::atomic<double> total{0.0};
            std::atomic<double> max{0.0};
        };

    }// namespace detail

    namespace {

        /**
         * @brief Raises a maximum to this value, if this value is higher.
         *
         * @par
         * A compare-and-exchange loop because there is no atomic maximum: read what is there, and
         * write ours only if it is still the number we compared against. Another thread getting in
         * between simply means going round again with what it left.
         */
        void raiseTo(std::atomic<double> &maximum, const double value) {
            double seen = maximum.load(std::memory_order_relaxed);
            while (value > seen && !maximum.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {
            }
        }

        /**
         * @brief Whether a value is worth pushing.
         *
         * @par
         * A NaN or an infinity is what a gauge over an empty collection reads, and it would poison
         * every rollup that averaged it afterwards. Skipped here rather than stored.
         */
        bool publishable(const double value) {
            return std::isfinite(value);
        }

    }// namespace

    // -- the meters ---------------------------------------------------------------------------------

    Counter::Counter(std::shared_ptr<detail::CounterState> state) : _state(std::move(state)) {}

    void Counter::Increment(const double amount) const {
        _state->value.fetch_add(amount, std::memory_order_relaxed);
    }

    double Counter::Count() const {
        return _state->value.load(std::memory_order_relaxed);
    }

    Gauge::Gauge(std::shared_ptr<detail::GaugeState> state) : _state(std::move(state)) {}

    void Gauge::Set(const double value) const {
        _state->value.store(value, std::memory_order_relaxed);
    }

    double Gauge::Value() const {
        return _state->value.load(std::memory_order_relaxed);
    }

    Timer::Timer(std::shared_ptr<detail::TimerState> state) : _state(std::move(state)) {}

    void Timer::Record(const std::chrono::nanoseconds elapsed) const {

        // Through nanoseconds and down to fractional milliseconds: a request that took 400
        // microseconds is not a request that took no time, and rounding it to zero here would be a
        // timer that reports nothing for everything fast.
        const auto milliseconds = std::chrono::duration<double, std::milli>(elapsed).count();

        _state->count.fetch_add(1, std::memory_order_relaxed);
        _state->total.fetch_add(milliseconds, std::memory_order_relaxed);
        raiseTo(_state->max, milliseconds);
    }

    long Timer::Count() const {
        return _state->count.load(std::memory_order_relaxed);
    }

    ScopedTimer::ScopedTimer(Timer timer) : _timer(std::move(timer)), _start(std::chrono::steady_clock::now()) {}

    ScopedTimer::~ScopedTimer() {
        _timer.Record(std::chrono::steady_clock::now() - _start);
    }

    // -- the registry -------------------------------------------------------------------------------

    MeterRegistry::MeterRegistry(const Emo &emo, RegistryOptions options) : _emo(emo), _options(std::move(options)) {

        if (_options.module.empty()) {
            throw EuclidError("a metrics registry has to say what is reporting - see RegistryOptions::module");
        }

        // Zero starts nothing: the application publishes on its own schedule, which is what a test
        // does and what an application with a loop of its own may prefer.
        if (_options.step > std::chrono::milliseconds::zero()) {
            _thread = std::thread([this] { Run(); });
        }
    }

    MeterRegistry::~MeterRegistry() {
        Stop();
    }

    void MeterRegistry::Stop() {

        if (_thread.joinable()) {
            {
                const std::lock_guard lock(_sleepMutex);
                _stopped = true;
            }
            _wakeUp.notify_all();
            _thread.join();
        }

        // After the thread has gone rather than alongside it, so the last batch is collected once.
        if (_options.publishOnStop) Publish();
    }

    void MeterRegistry::Run() {

        for (;;) {
            {
                std::unique_lock lock(_sleepMutex);
                // Waiting on the condition rather than sleeping: a process shutting down should not
                // have to sit out a step before its thread notices.
                _wakeUp.wait_for(lock, _options.step, [this] { return _stopped; });
                if (_stopped) return;
            }
            Publish();
        }
    }

    // -- the meters, by name ------------------------------------------------------------------------

    Counter MeterRegistry::CounterOf(const std::string &name, const Labels &labels) {
        const std::lock_guard lock(_mutex);
        auto &state = _counters[MeterKey(name, labels)];
        if (!state) state = std::make_shared<detail::CounterState>();
        return Counter(state);
    }

    Gauge MeterRegistry::GaugeOf(const std::string &name, const Labels &labels) {
        const std::lock_guard lock(_mutex);
        auto &state = _gauges[MeterKey(name, labels)];
        if (!state) state = std::make_shared<detail::GaugeState>();
        return Gauge(state);
    }

    void MeterRegistry::GaugeFrom(const std::string &name, std::function<double()> supplier, const Labels &labels) {
        const std::lock_guard lock(_mutex);
        _suppliers[MeterKey(name, labels)] = std::move(supplier);
    }

    Timer MeterRegistry::TimerOf(const std::string &name, const Labels &labels) {
        const std::lock_guard lock(_mutex);
        auto &state = _timers[MeterKey(name, labels)];
        if (!state) state = std::make_shared<detail::TimerState>();
        return Timer(state);
    }

    // -- publishing ---------------------------------------------------------------------------------

    std::vector<Metric> MeterRegistry::Collect() {

        std::vector<Metric> batch;
        std::map<MeterKey, std::function<double()>> suppliers;

        {
            const std::lock_guard lock(_mutex);

            // Exchanged rather than read: what a counter accumulated belongs to the step that is
            // ending, and the next one starts from nothing. That is what makes it a rate.
            for (const auto &[key, state]: _counters) {
                const auto value = state->value.exchange(0.0, std::memory_order_relaxed);
                if (publishable(value)) batch.push_back(Metric::Rate(key.first, value, LabelsFor(key.second)));
            }

            for (const auto &[key, state]: _timers) {
                const auto count = static_cast<double>(state->count.exchange(0, std::memory_order_relaxed));
                const auto total = state->total.exchange(0.0, std::memory_order_relaxed);
                const auto max = state->max.exchange(0.0, std::memory_order_relaxed);
                const auto labels = LabelsFor(key.second);

                // The three series euclid-jdk's Micrometer registry publishes for a timer, under the
                // same names, so that the two SDKs' timings are the same shape in a graph.
                if (publishable(count)) batch.push_back(Metric::Rate(key.first + ".count", count, labels));
                if (publishable(total)) batch.push_back(Metric::Rate(key.first + ".total", total, labels));
                if (publishable(max)) batch.push_back(Metric::Gauge(key.first + ".max", max, labels));
            }

            // Read rather than exchanged: a gauge is what it is, and a publish is somebody looking.
            for (const auto &[key, state]: _gauges) {
                const auto value = state->value.load(std::memory_order_relaxed);
                if (publishable(value)) batch.push_back(Metric::Gauge(key.first, value, LabelsFor(key.second)));
            }

            // Copied out, and called below with the lock released: a supplier that reaches back into
            // this registry would otherwise deadlock on a mutex it cannot see.
            suppliers = _suppliers;
        }

        for (const auto &[key, supplier]: suppliers) {
            try {
                if (const auto value = supplier(); publishable(value)) {
                    batch.push_back(Metric::Gauge(key.first, value, LabelsFor(key.second)));
                }
            } catch (...) {
                // One gauge that could not read itself is not a reason to lose the batch it was in.
            }
        }

        return batch;
    }

    void MeterRegistry::Publish() {

        const auto batch = Collect();
        if (batch.empty()) return;

        try {
            _emo.PushMetrics(_options.module, batch);
            _publishes.fetch_add(1, std::memory_order_relaxed);
        } catch (...) {
            // Counted and dropped rather than retried: a rate sent twice is counted twice, and a
            // process does not stop because it could not say how it was doing.
            _failed.fetch_add(1, std::memory_order_relaxed);
        }
    }

    long MeterRegistry::Publishes() const { return _publishes.load(std::memory_order_relaxed); }

    long MeterRegistry::FailedPublishes() const { return _failed.load(std::memory_order_relaxed); }

    Labels MeterRegistry::LabelsFor(const Labels &labels) const {
        if (_options.commonLabels.empty()) return labels;
        // The meter's own laid over the common ones, so a meter that names a label the registry also
        // names keeps its own answer.
        Labels merged = _options.commonLabels;
        for (const auto &[name, value]: labels) merged[name] = value;
        return merged;
    }

}// namespace Euclid::CDK::EMO
