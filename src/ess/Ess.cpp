// SPDX-License-Identifier: Apache-2.0

// C++ includes
#include <algorithm>
#include <ranges>

// Euclid includes
#include <euclid/cdk/Errors.h>
#include <euclid/cdk/Json.h>
#include <euclid/cdk/ess/Ess.h>

namespace Euclid::CDK::ESS {

    Ess::Ess(const EAM::Session &session) : ModuleClient(session, std::string(ESS::Target)) {}

    Secret Ess::CreateSecret(const std::string &name, const std::string &value, const CreateSecretOptions &options) const {

        // A secret nobody can read is not a secret, which is the server's reason for refusing this
        // too - said here so that it costs no round trip.
        if (value.empty()) throw EuclidError("a secret needs a value; the server refuses an empty one");

        const boost::json::object payload{
                {"name", name},
                {"value", value},
                {"description", options.description},
                {"keyErn", options.keyErn},
        };
        return SecretOf("create-secret", payload);
    }

    SecretValue Ess::GetSecret(const std::string &name) const {
        return ToSecretValue(Call("get-secret", {{"name", name}}));
    }

    Page<Secret> Ess::ListSecrets(const ListOptions &options) const {
        return ToPage<Secret>(Call("list-secrets", ListPayload(options, "name")), "secrets", ToSecret);
    }

    bool Ess::ExistsSecret(const std::string &name) const {

        ListOptions options;
        options.prefix = name;
        options.pageSize = 0;

        const auto matching = ListSecrets(options);
        return std::ranges::any_of(matching.items, [&name](const Secret &secret) { return secret.name == name; });
    }

    Secret Ess::RotateSecret(const std::string &name, const std::string &value) const {
        return UpdateSecret(name, {.value = value});
    }

    Secret Ess::UpdateSecret(const std::string &name, const UpdateSecretOptions &options) const {

        if (!options.value.has_value() && !options.description.has_value() && options.keyErn.empty()) {
            throw EuclidError("UpdateSecret needs a value, a description or a keyErn to change");
        }
        if (options.value.has_value() && options.value->empty()) {
            throw EuclidError("a secret needs a value; the server refuses an empty one");
        }

        boost::json::object payload{{"name", name}};
        if (options.value.has_value()) payload["value"] = *options.value;
        // Sent when it was named, empty included: an empty description clears the stored one, and
        // that is a thing a caller may mean.
        if (options.description.has_value()) payload["description"] = *options.description;
        // Non-empty rather than named: there is no such thing as moving a secret onto the empty key,
        // so an empty one means "leave it where it is" rather than a value to send.
        if (!options.keyErn.empty()) payload["keyErn"] = options.keyErn;

        return SecretOf("update-secret", payload);
    }

    DeleteSecretResult Ess::DeleteSecret(const std::string &name) const {
        return ToDeleteSecretResult(Call("delete-secret", {{"name", name}}));
    }

    Secret Ess::AddSecretTag(const std::string &name, const std::string &key, const std::string &value) const {
        return SecretOf("add-secret-tag", {{"name", name}, {"key", key}, {"value", value}});
    }

    Secret Ess::DeleteSecretTag(const std::string &name, const std::string &key) const {
        return SecretOf("delete-secret-tag", {{"name", name}, {"key", key}});
    }

    boost::json::object Ess::Metrics() const {
        return Call("get-metrics");
    }

    Secret Ess::SecretOf(const std::string &action, const boost::json::object &payload) const {
        return ToSecret(Json::Child(Call(action, payload), "secret"));
    }

}// namespace Euclid::CDK::ESS
