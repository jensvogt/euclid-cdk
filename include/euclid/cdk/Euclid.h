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
 * authenticates with; and ESM, euclid's storage module, which is reached through a session EAM
 * answered with:
 *
 * @code
 * const ESM::Esm esm(session);
 * const auto bucket = esm.CreateBucket("reports");
 * esm.UploadFile(bucket.ern, "2026/q3.pdf", "q3.pdf");
 * @endcode
 *
 * @par
 * The rest (EQS, ENS, EKM, EKV, EAP, ESS, EAG) speak the same protocol through the same client and
 * will follow. Until they do, CDK::ModuleClient is what one is built out of, and
 * EAM::Session::NewRequest() and CDK::HttpClient reach any action this SDK does not name.
 *
 * @par Why there is no umbrella class
 * euclid-pdk and euclid-ndk open with a `Euclid` object whose only job is to write the server's URL
 * once and hand out module clients. Here the name would be ambiguous with the enclosing namespace
 * the moment a caller wrote `using namespace Euclid::CDK` - so the login builder is the entry
 * point, and a module client is built from the session it authenticates as, which is the same thing
 * one line longer.
 */

// Euclid includes
#include <euclid/cdk/Credentials.h>
#include <euclid/cdk/Crypto.h>
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/Url.h>
#include <euclid/cdk/Version.h>
#include <euclid/cdk/auth/HttpSignature.h>
#include <euclid/cdk/auth/SigV4.h>
#include <euclid/cdk/auth/SigningScheme.h>
#include <euclid/cdk/dto/Com.h>
#include <euclid/cdk/dto/Eam.h>
#include <euclid/cdk/dto/Esm.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Eam.h>
#include <euclid/cdk/eam/Session.h>
#include <euclid/cdk/esm/Esm.h>
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
