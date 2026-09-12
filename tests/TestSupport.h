// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>

// Euclid includes
#include <euclid/cdk/eam/Eam.h>

#include "FakeGateway.h"

namespace Euclid::CDK::Test {

    /**
     * @brief The access key the fake gateway knows, and the secret it verifies signatures with.
     *
     * @par
     * AWS's own documentation example, so that a signature computed here can be compared against a
     * published one by hand when something disagrees.
     */
    inline constexpr auto AccessKeyId = "AKIAIOSFODNN7EXAMPLE";
    inline constexpr auto SecretAccessKey = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

    /**
     * @brief A JWT whose "exp" is an hour from now, so a session built from it is not immediately
     * treated as expired.
     */
    [[nodiscard]]
    std::string FutureToken();

    /**
     * @brief What a login answers with: a token, an access key and the caller's identity.
     */
    [[nodiscard]]
    std::string LoginResponse();

    /**
     * @brief Answers the login, checks the credentials on everything else, and hands what is left to
     * handler.
     *
     * @par
     * This is what makes these tests worth more than a recording: a request whose signature does not
     * verify is answered with a 401 by the same rules the server applies, so a client that signs one
     * thing and sends another fails here rather than in production.
     *
     * @param handler answers the requests that authenticated.
     */
    [[nodiscard]]
    FakeGateway::Handler Authenticated(FakeGateway::Handler handler);

    /**
     * @brief The same, for a gateway whose every action answers with one canned body.
     *
     * @param body the JSON body, or empty for "{}".
     */
    [[nodiscard]]
    FakeGateway::Handler Answering(const std::string &body = {});

    /**
     * @brief A login builder pointed at a gateway, with the credentials cache out of the way.
     */
    [[nodiscard]]
    EAM::Eam Builder(const FakeGateway &gateway);

}// namespace Euclid::CDK::Test
