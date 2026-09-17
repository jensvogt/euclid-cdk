// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief Application monitoring end to end: meters recorded, published on a step, and read back.
 *
 * @par
 * @code
 * monitoring-walkthrough https://euclid.example.com jens secret
 * @endcode
 *
 * @par
 * Reports under a module name of its own, named after the moment it started. Metrics are the one
 * thing these walkthroughs cannot clean up after themselves - there is no delete for a row, and
 * they go when EMO's retention takes them - so reporting under a name nothing else uses is the way
 * this stays out of your dashboards.
 *
 * @par
 * Reading rows back is administrator-only. A login without those rights does everything but the
 * last step, and says so rather than failing.
 */

// C++ includes
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Euclid includes
#include <euclid/cdk/Euclid.h>

using namespace Euclid::CDK;

namespace {

    /**
     * @brief Work to be measured: parses an invoice, slowly and not always successfully.
     */
    bool parse(const int invoice) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20 + invoice % 7 * 10));
        return invoice % 5 != 0;
    }

    /**
     * @brief Records a run of work through the meters, the way an application would.
     */
    void doWork(EMO::MeterRegistry &metrics, std::vector<int> &backlog) {

        // Kept rather than looked up per use: asking the registry answers the same meter every time,
        // but a handle costs nothing to hold and a map lookup on a hot path is a map lookup.
        const auto parsed = metrics.CounterOf("invoices.parsed", {{"outcome", "ok"}});
        const auto rejected = metrics.CounterOf("invoices.parsed", {{"outcome", "rejected"}});
        const auto duration = metrics.TimerOf("invoice.parse");

        std::cout << "\nparsing " << backlog.size() << " invoices, publishing every 2s\n";

        // Counted here as well, only so that the two numbers can be shown side by side below.
        long okTotal = 0;
        long rejectedTotal = 0;

        while (!backlog.empty()) {
            const auto invoice = backlog.back();
            backlog.pop_back();

            bool ok = false;
            {
                // Recorded however the scope is left - the returned-early and the thrown-out-of
                // cases included, which is what a timing written at the end of a function misses.
                const EMO::ScopedTimer measure(duration);
                ok = parse(invoice);
            }
            ok ? parsed.Increment() : rejected.Increment();
            ok ? ++okTotal : ++rejectedTotal;
        }

        std::cout << "  parsed   " << okTotal << " over the whole run\n"
                  << "  rejected " << rejectedTotal << "\n";

        // The meters hold less than the run did, and that is the point of a step: the publisher took
        // what had accumulated and started them again, so each batch carries its own interval rather
        // than an ever-growing total. It is what makes these rates, and what lets EMO sum them.
        std::cout << "\nthe meters now hold what has accumulated since the last publish:\n"
                  << "  invoices.parsed{outcome=ok} " << parsed.Count() << " of the " << okTotal << " above\n"
                  << "  invoice.parse               " << duration.Count() << " timings\n";
    }

    /**
     * @brief Reads back what was just published, which only an administrator may do.
     */
    void readBack(const EMO::Emo &emo, const std::string &module) {

        std::cout << "\nreading the rows back (administrator only)\n";

        try {
            const auto rows = emo.List({.name = "invoices.parsed", .limit = 10});
            if (rows.empty()) {
                // EMO writes what it is pushed into bucket-aligned rows on a flush of its own, so a
                // listing a moment after a push can legitimately still be empty.
                std::cout << "  nothing yet - EMO flushes on its own period, so give it a moment\n";
                return;
            }

            for (const auto &row: rows) {
                std::cout << "  " << std::setw(20) << std::left << row.name
                          << std::setw(10) << std::right << row.value
                          << "  " << std::setw(6) << row.type
                          << "  " << std::setw(5) << row.resolution
                          << "  over " << row.samples << " sample(s)  " << row.timestamp;
                for (const auto &[name, value]: row.labels) std::cout << "  " << name << "=" << value;
                std::cout << "\n";
            }

            std::cout << "\nmean time to parse one invoice: "
                      << emo.Average({.name = "invoice.parse.total"}) << " ms\n"
                      << "  (the timer's total is a rate, so a mean over the rows is a mean per step)\n";

        } catch (const ServiceError &ex) {
            if (ex.Status() == 403) {
                std::cout << "  refused: " << ex.Reason() << "\n"
                          << "  publishing needs no special rights; reading everybody's numbers does\n";
                return;
            }
            throw;
        }
        std::cout << "\nthe module to look for in a dashboard is " << module << "\n";
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
    const auto module = "cdk-walkthrough-" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                                    std::chrono::system_clock::now().time_since_epoch())
                                                                    .count());

    try {
        auto builder = EAM::Eam::ForServer(baseUrl).Credentials(argv[2], argv[3]);
        if (!nameSpace.empty()) builder.Namespace(nameSpace);

        const auto session = builder.Login();
        const EMO::Emo emo(session);

        std::cout << "logged in to " << session.BaseUrl() << " as " << session.UserId()
                  << " (" << session.AccountId() << ", " << session.Region() << ")\n"
                  << "reporting as module " << module << "\n";

        // What is left of the backlog at any moment, which the gauge below reads for itself rather
        // than being told about.
        std::vector<int> backlog(40);
        for (std::size_t i = 0; i < backlog.size(); ++i) backlog[i] = static_cast<int>(i) + 1;

        {
            // A two-second step so that a walkthrough shows the thread working; a minute is the
            // default, and what an application would leave it at.
            EMO::MeterRegistry metrics(emo, {.module = module,
                                             .step = std::chrono::seconds(2),
                                             .commonLabels = {{"sdk", std::string(Version)}}});

            // Read at every publish, which is what a gauge is for: nobody has to remember to set it.
            metrics.GaugeFrom("invoices.backlog", [&backlog] { return static_cast<double>(backlog.size()); });

            doWork(metrics, backlog);

            // The thread would do this on the next step, and does it once more on the way out; asked
            // for here so that what follows has something to read.
            metrics.Publish();
            std::cout << "\npublished " << metrics.Publishes() << " batch(es), "
                      << metrics.FailedPublishes() << " failed\n";

        }// the registry stops here, publishing the step in hand rather than losing it

        readBack(emo, module);

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
