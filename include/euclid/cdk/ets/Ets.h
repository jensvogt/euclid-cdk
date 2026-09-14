// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Ets.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::ETS {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "ets";

    /**
     * @brief The protocols a transfer server can speak.
     *
     * @par
     * Fixed when it is created: which one it is decides which process the manager starts, so
     * changing it would be a different server.
     */
    inline constexpr std::string_view Ftp = "FTP";
    inline constexpr std::string_view Sftp = "SFTP";

    /**
     * @brief What a transfer server's state reads as, asked for and observed.
     */
    inline constexpr std::string_view StateRunning = "RUNNING";
    inline constexpr std::string_view StateStopped = "STOPPED";

    /**
     * @brief What an address of nothing in particular means - every interface the host has.
     */
    inline constexpr std::string_view EveryInterface = "0.0.0.0";

    /**
     * @brief The passive-mode port range an FTP server takes unless told otherwise.
     *
     * @par
     * Whatever sits in front of euclid has to let these through as well as the control port, which
     * is what makes the range worth naming rather than leaving to chance.
     */
    inline constexpr long DefaultPasvMin = 6000;
    inline constexpr long DefaultPasvMax = 6100;

    /**
     * @brief What a transfer server is defined as, beyond the bucket and the port it must have.
     */
    struct EUCLID_CDK_API CreateServerOptions {

        /**
         * @brief Sftp or Ftp. Fixed from here on.
         */
        std::string protocol = std::string(Sftp);

        /**
         * @brief The interface to bind to; every one of them by default.
         */
        std::string address = std::string(EveryInterface);

        /**
         * @brief The key prefix a logged-in user lands in, which is what lets one bucket serve
         * several servers without either seeing the other's files.
         */
        std::string homeDirectory;

        /**
         * @brief The EAM users who may log in.
         */
        std::vector<std::string> userIds;

        /**
         * @brief The groups whose members may - usually the better answer, since it outlives the
         * individual accounts.
         */
        std::vector<std::string> userGroups;

        /**
         * @brief Key prefixes to present as directories, for the clients that will not show what
         * they cannot list.
         */
        std::vector<std::string> directories;

        /**
         * @brief SFTP only: the private SSH host key. Generated on first start when empty - which is
         * what to leave it as unless a key that clients already trust has to be kept.
         */
        std::string hostKey;

        /**
         * @brief FTP only: the passive-mode port range.
         */
        long pasvMin{DefaultPasvMin};
        long pasvMax{DefaultPasvMax};
    };

    /**
     * @brief What an update changes, which is only what it names.
     *
     * @par
     * The distinction the server draws is between a field being sent and not being sent, rather than
     * between its values - so a field left unset leaves the stored one alone, while one set to an
     * empty string or an empty list replaces it. That is what std::optional says here.
     *
     * @par
     * The protocol is not here: which one a server speaks decides which process runs it, so changing
     * it would be a different server. Neither is the state - see ETS::Ets::StartServer().
     */
    struct EUCLID_CDK_API UpdateServerOptions {
        std::optional<std::string> address;
        std::optional<long> port;
        std::optional<std::string> bucket;
        std::optional<std::string> homeDirectory;
        std::optional<std::vector<std::string>> userIds;
        std::optional<std::vector<std::string>> userGroups;
        std::optional<std::vector<std::string>> directories;
        std::optional<std::string> hostKey;
        std::optional<long> pasvMin;
        std::optional<long> pasvMax;
    };

    /**
     * @brief ETS - euclid's transfer service: the FTP and SFTP endpoints onto a bucket.
     *
     * @par
     * @code
     * const ETS::Ets ets(session);
     *
     * ets.CreateServer("partner-drop", "invoices", 2222, {.userGroups = {"partners"}, .homeDirectory = "incoming/"});
     * ets.StartServer("partner-drop");
     * @endcode
     *
     * @par
     * ETS never speaks either protocol itself. It owns the definitions - which protocol, which port,
     * which EAM users and groups may log in, which ESM bucket the files really live in - and
     * starting one is nothing more than writing a desired state onto a definition. euclid's manager
     * is what turns that into a running process, and the process reads its own definition back from
     * here.
     *
     * @par
     * So a file uploaded over FTP is an object in a bucket, with the events and the lifecycle every
     * other object has: this is a protocol somebody's existing tooling already speaks, put in front
     * of storage, rather than a second place files live.
     *
     * @par
     * Every action here is administrator-only, server-side. EAM::Session::IsAdmin() says whether the
     * logged-in user is one, though the server enforces it regardless.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Ets final : public ModuleClient {
    public:

        /**
         * @brief Builds ETS's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Ets(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Ets(EAM::Session &&) = delete;

        // -- definitions ------------------------------------------------------------------------

        /**
         * @brief Defines a transfer server, stopped, and answers with it as it was stored.
         *
         * @par
         * Nothing listens yet: a new server's desired state is StateStopped, so StartServer() is
         * what puts it in service. Refused with HTTP 409 if the ID or the port is taken, and with
         * 404 if the bucket is not there. The ID is unique within the account and the namespace,
         * rather than across the installation.
         *
         * @param serverId the server's ID.
         * @param bucket   the name of the bucket the files live in - a name, not an ERN.
         * @param port     the port to listen on, 1 to 65535.
         * @param options  the protocol, who may log in, and where they land.
         */
        [[nodiscard]]
        TransferServer CreateServer(const std::string &serverId, const std::string &bucket, long port, const CreateServerOptions &options = {}) const;

        /**
         * @brief Changes a transfer server's definition. Only what this names changes.
         *
         * @par
         * A list that is named replaces the stored one rather than adding to it. A running server
         * keeps running on its old definition until it is restarted, since the process reads this
         * once at startup.
         */
        [[nodiscard]]
        TransferServer UpdateServer(const std::string &serverId, const UpdateServerOptions &options = {}) const;

        /**
         * @brief The transfer servers whose ID starts with a prefix; an empty prefix lists them all.
         *
         * @par
         * A plain list rather than a page: ETS answers with every match and no total, since a
         * deployment has as many transfer servers as it has ports to spare.
         */
        [[nodiscard]]
        std::vector<TransferServer> ListServers(const std::string &prefix = {}) const;

        /**
         * @brief One transfer server, by its ID, with the state it is observed in.
         */
        [[nodiscard]]
        TransferServer GetServer(const std::string &serverId) const;

        /**
         * @brief Removes a transfer server's definition. Stop it first - this does not.
         *
         * @par
         * The bucket and its objects are untouched: what is deleted is the endpoint onto them.
         */
        void DeleteServer(const std::string &serverId) const;

        // -- running ----------------------------------------------------------------------------

        /**
         * @brief Asks for a transfer server to run, and answers with it as it stands.
         *
         * @par
         * Asking is all this does: the desired state changes here and the manager acts on it, so the
         * server in the answer is usually still StateStopped - it says what was asked for, not what
         * has happened yet.
         */
        [[nodiscard]]
        TransferServer StartServer(const std::string &serverId) const;

        /**
         * @brief Asks for a transfer server to stop, and answers with it as it stands.
         *
         * @par
         * A client mid-transfer is not this call's concern: the process is stopped, and whatever was
         * in flight fails the way a dropped connection does.
         */
        [[nodiscard]]
        TransferServer StopServer(const std::string &serverId) const;

        // -- monitoring -------------------------------------------------------------------------

        /**
         * @brief ETS's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to ETS.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;

    private:

        /**
         * @brief The actions that answer with one transfer server, which is all of them but the
         * listing.
         */
        [[nodiscard]]
        TransferServer Server(const std::string &action, const boost::json::object &payload) const;
    };

}// namespace Euclid::CDK::ETS
