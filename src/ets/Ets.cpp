// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <tuple>

// Euclid includes
#include <euclid/cdk/Json.h>
#include <euclid/cdk/ets/Ets.h>

namespace Euclid::CDK::ETS {

    namespace {

        /**
         * @brief A JSON array of strings, for the payload fields that take a list.
         */
        boost::json::array stringsOf(const std::vector<std::string> &values) {
            boost::json::array array;
            for (const auto &value: values) array.emplace_back(value);
            return array;
        }

        /**
         * @brief A field of an update, sent only when the caller named it.
         *
         * @par
         * The server distinguishes a field being sent from one that is not, rather than one value
         * from another, so an empty string that was asked for has to travel and one that was not has
         * to stay away.
         */
        template<typename T>
        void put(boost::json::object &payload, const std::string &field, const std::optional<T> &value) {
            if (!value.has_value()) return;
            if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                payload[field] = stringsOf(*value);
            } else {
                payload[field] = *value;
            }
        }

    }// namespace

    Ets::Ets(const EAM::Session &session) : ModuleClient(session, std::string(ETS::Target)) {}

    // -- definitions --------------------------------------------------------------------------------

    TransferServer Ets::CreateServer(const std::string &serverId, const std::string &bucket, const long port, const CreateServerOptions &options) const {
        const boost::json::object payload{
                {"serverId", serverId},
                {"protocol", options.protocol},
                {"port", port},
                {"bucket", bucket},
                {"address", options.address},
                {"homeDirectory", options.homeDirectory},
                {"userIds", stringsOf(options.userIds)},
                {"userGroups", stringsOf(options.userGroups)},
                {"directories", stringsOf(options.directories)},
                {"hostKey", options.hostKey},
                {"pasvMin", options.pasvMin},
                {"pasvMax", options.pasvMax},
        };
        return Server("create-server", payload);
    }

    TransferServer Ets::UpdateServer(const std::string &serverId, const UpdateServerOptions &options) const {

        boost::json::object payload{{"serverId", serverId}};
        put(payload, "address", options.address);
        put(payload, "port", options.port);
        put(payload, "bucket", options.bucket);
        put(payload, "homeDirectory", options.homeDirectory);
        put(payload, "userIds", options.userIds);
        put(payload, "userGroups", options.userGroups);
        put(payload, "directories", options.directories);
        put(payload, "hostKey", options.hostKey);
        put(payload, "pasvMin", options.pasvMin);
        put(payload, "pasvMax", options.pasvMax);

        return Server("update-server", payload);
    }

    std::vector<TransferServer> Ets::ListServers(const std::string &prefix) const {
        std::vector<TransferServer> servers;
        for (const auto &document: Json::Documents(Call("list-servers", {{"prefix", prefix}}), "servers")) {
            servers.push_back(ToTransferServer(document));
        }
        return servers;
    }

    TransferServer Ets::GetServer(const std::string &serverId) const {
        return Server("get-server", {{"serverId", serverId}});
    }

    void Ets::DeleteServer(const std::string &serverId) const {
        std::ignore = Call("delete-server", {{"serverId", serverId}});
    }

    // -- running ------------------------------------------------------------------------------------

    TransferServer Ets::StartServer(const std::string &serverId) const {
        return Server("start-server", {{"serverId", serverId}});
    }

    TransferServer Ets::StopServer(const std::string &serverId) const {
        return Server("stop-server", {{"serverId", serverId}});
    }

    // -- monitoring ---------------------------------------------------------------------------------

    boost::json::object Ets::Metrics() const {
        return Call("get-metrics");
    }

    TransferServer Ets::Server(const std::string &action, const boost::json::object &payload) const {
        return ToTransferServer(Call(action, payload));
    }

}// namespace Euclid::CDK::ETS
