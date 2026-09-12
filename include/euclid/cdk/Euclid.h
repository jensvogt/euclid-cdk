// SPDX-License-Identifier: Apache-2.0

#pragma once

/**
 * @file
 * @brief euclid-cdk - the C++ SDK for a euclid server. Start here.
 *
 * @par
 * @code
 * #include <euclid/cdk/Euclid.h>
 *
 * using namespace Euclid::CDK;
 *
 * const EAM::Session session = EAM::Eam::ForServer("https://euclid.example.com")
 *                                      .Credentials("jens", "secret")
 *                                      .Login();
 *
 * for (const auto &[total, items] = session.ListUsers({.prefix = "j", .pageSize = 25}); const auto &user: items) {
 *     std::cout << user.userId << " " << user.email << "\n";
 * }
 * @endcode
 *
 * @par What is here
 * EAM - euclid's access management module - and the two signing schemes a euclid client
 * authenticates with. EAM is where a login comes from, so it is the module every other one is
 * reached through; the rest (ESM, EQS, ENS, EKM, EKV, EAP, ESS, EAG) speak the same protocol
 * through the same client and will follow. Until they do, EAM::Session::NewRequest() and
 * CDK::HttpClient reach any action this SDK does not name.
 *
 * @par Why there is no umbrella class
 * euclid-pdk and euclid-ndk open with a `Euclid` object whose only job is to write the server's URL
 * once and hand out module clients. Here the name would be ambiguous with the enclosing namespace
 * the moment a caller wrote `using namespace Euclid::CDK`, and with one module there is nothing to
 * hand out - so the login builder is the entry point, and the umbrella arrives with the second
 * module.
 */

// Euclid includes
#include <euclid/cdk/Credentials.h>
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/Url.h>
#include <euclid/cdk/Version.h>
#include <euclid/cdk/auth/HttpSignature.h>
#include <euclid/cdk/auth/SigV4.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/dto/Eam.h>
#include <euclid/cdk/eam/Eam.h>
#include <euclid/cdk/eam/Session.h>
#include <euclid/cdk/http/HttpClient.h>

namespace Euclid::CDK {

    /**
     * @brief Logs in, for the case that needs no builder at all.
     *
     * @param baseUrl  the server, e.g. "https://euclid.example.com".
     * @param username the user ID.
     * @param password the password.
     * @return the authenticated session.
     * @throws AuthenticationError if the server refuses the credentials.
     */
    [[nodiscard]]
    EUCLID_CDK_API EAM::Session Login(const std::string &baseUrl, const std::string &username, const std::string &password);

}// namespace Euclid::CDK
