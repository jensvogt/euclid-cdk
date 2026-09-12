// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <stdexcept>
#include <string>

// Euclid includes
#include <euclid/cdk/Export.h>

namespace Euclid::CDK {

    /**
     * @brief Base class for everything this SDK throws.
     *
     * @par
     * The server answers every failure the same way - a non-2xx status and a JSON body of the form
     * {"error": "..."} (Core::HttpActionServer::ErrorResponse) - so the message a caller sees is
     * pulled out of that body when it is there, and falls back to the body verbatim when it is not.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API EuclidError : public std::runtime_error {
    public:

        explicit EuclidError(const std::string &message);
    };

    /**
     * @brief A login was refused.
     *
     * @par
     * Separate from ServiceError because it is the one failure a caller can nearly always do
     * something about: the password is wrong, the account is disabled, or the user is a technical
     * user the server refuses to log in interactively.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API AuthenticationError final : public EuclidError {
    public:

        AuthenticationError(int status, const std::string &body);

        /**
         * @brief HTTP status the server refused with.
         */
        [[nodiscard]]
        int Status() const;

        /**
         * @brief Response body, verbatim.
         */
        [[nodiscard]]
        const std::string &Body() const;

        /**
         * @brief The server's own message, out of the {"error": "..."} body.
         */
        [[nodiscard]]
        const std::string &Reason() const;

    private:

        int _status;
        std::string _body;
        std::string _reason;
    };

    /**
     * @brief A module refused or failed an action.
     *
     * @par
     * Carries the target and the action alongside the status, so a caller catching one of these
     * knows which call failed without having had to wrap each one individually.
     *
     * @author jens.vogt\@opitz-consulting.com
     */
    class EUCLID_CDK_API ServiceError final : public EuclidError {
    public:

        ServiceError(std::string target, std::string action, int status, const std::string &body);

        /**
         * @brief Module the failed call was addressed to, e.g. "eam".
         */
        [[nodiscard]]
        const std::string &Target() const;

        /**
         * @brief Action the failed call asked for, e.g. "list-users".
         */
        [[nodiscard]]
        const std::string &Action() const;

        /**
         * @brief HTTP status the server refused with.
         */
        [[nodiscard]]
        int Status() const;

        /**
         * @brief Response body, verbatim.
         */
        [[nodiscard]]
        const std::string &Body() const;

        /**
         * @brief The server's own message, out of the {"error": "..."} body.
         */
        [[nodiscard]]
        const std::string &Reason() const;

    private:

        std::string _target;
        std::string _action;
        int _status;
        std::string _body;
        std::string _reason;
    };

    /**
     * @brief The server's error message out of a response body.
     *
     * @param body the response body; may be empty, JSON, or something a proxy wrote.
     * @return the "error" field of a JSON body, or the trimmed body itself when it is not the
     * shape we expect - which is what a proxy's HTML error page looks like, and the most useful
     * thing there is to say about one.
     */
    [[nodiscard]]
    EUCLID_CDK_API std::string ReasonOf(const std::string &body);

}// namespace Euclid::CDK
