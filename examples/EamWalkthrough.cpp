// SPDX-License-Identifier: Apache-2.0

/**
 * @file
 * @brief Logs in and walks through EAM: users, groups, accounts, namespaces and access keys.
 *
 * @par
 * Run it against a euclid server:
 *
 * @code
 * eam-walkthrough https://euclid.example.com jens secret
 * @endcode
 *
 * @par
 * Reads only, apart from the namespace it switches to when one is given - so it is safe to point at
 * a running deployment. Everything it prints comes back from the server; nothing here is stubbed.
 */

// C++ includes
#include <iostream>
#include <string>

// Euclid includes
#include <euclid/cdk/Euclid.h>

using namespace Euclid::CDK;

namespace {

    void printUsers(const EAM::Session &session) {

        const auto [total, items] = session.ListUsers({.pageSize = 10});
        std::cout << "\nusers (" << total << " in total)\n";

        for (const auto &user: items) {
            std::cout << "  " << user.userId << "  " << user.email << "  " << user.ern << "\n";
            for (const auto &grant: user.accountGrants) {
                std::cout << "      grant on " << grant.accountId << (grant.isAdmin ? " (admin)" : "") << ", namespaces:";
                for (const auto &nameSpace: grant.namespaces) std::cout << " " << nameSpace;
                std::cout << "\n";
            }
        }
    }

    void printUserGroups(const EAM::Session &session) {

        const auto [total, items] = session.ListUserGroups({.pageSize = 10});
        std::cout << "\nuser groups (" << total << " in total)\n";

        for (const auto &group: items) {
            std::cout << "  " << group.name << "  " << group.userIds.size() << " member(s)  " << group.description << "\n";
        }
    }

    void printAccountsAndNamespaces(const EAM::Session &session) {

        const auto [total, items] = session.ListAccounts({.pageSize = 10});
        std::cout << "\naccounts (" << total << " in total)\n";

        for (const auto &account: items) {
            std::cout << "  " << account.accountId << "  " << account.name << "\n";
            for (const auto namespaces = session.ListNamespaces(account.accountId); const auto &nameSpace: namespaces.items) {
                std::cout << "      namespace " << nameSpace.name << "  " << nameSpace.description << "\n";
            }
        }
    }

    void printAccessKeys(const EAM::Session &session) {

        // The secret of an existing key is never shown again - only the key that CreateAccessKey()
        // answers with carries one, and only that once.
        std::cout << "\naccess keys\n";
        for (const auto &[accessKeyId, active, createdAt]: session.ListAccessKeys()) {
            std::cout << "  " << accessKeyId << (active ? "  active" : "  inactive") << "  created " << createdAt << "\n";
        }
    }

}// namespace

int main(const int argc, char *argv[]) {

    if (argc < 4) {
        std::cerr << "usage: " << argv[0] << " <server-url> <user-id> <password> [namespace]\n"
                  << "   e.g. " << argv[0] << " https://euclid.example.com jens secret reports\n";
        return 2;
    }

    const std::string baseUrl = argv[1];
    const std::string userId = argv[2];
    const std::string password = argv[3];
    const std::string nameSpace = argc > 4 ? argv[4] : "";

    try {
        auto eam = EAM::Eam::ForServer(baseUrl).Credentials(userId, password);
        if (!nameSpace.empty()) eam.Namespace(nameSpace);

        const auto session = eam.Login();

        std::cout << "logged in to " << session.BaseUrl() << "\n"
                  << "  user       " << session.UserId() << (session.IsAdmin() ? " (administrator)" : "") << "\n"
                  << "  account    " << session.AccountId() << "\n"
                  << "  region     " << session.Region() << "\n"
                  << "  namespace  " << (session.Namespace().empty() ? "<unscoped>" : session.Namespace()) << "\n"
                  << "  signing    " << session.Scheme().Name()
                  << (session.AccessKeyId().empty() ? " (no access key - calls present the bearer token)" : " with " + session.AccessKeyId()) << "\n"
                  << "  authority  " << session.Authority() << "\n";

        printUsers(session);
        printUserGroups(session);
        printAccountsAndNamespaces(session);
        printAccessKeys(session);

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
