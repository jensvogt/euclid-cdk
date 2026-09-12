// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/Json.h>

/**
 * @file
 * @brief The shapes EAM sends back, and the readers that parse them.
 *
 * @par
 * Structs rather than raw boost::json::value, so a typo in a field name is a compile error here
 * instead of a null that travels. Field names are the server's own
 * (dto/include/euclid/dto/eam in the euclid repository), which is why some of them read oddly in
 * C++: "ns" is the C++ spelling of a field the wire calls "namespace", because the wire name is a
 * keyword here.
 */

namespace Euclid::CDK::EAM {

    /**
     * @brief The caller identity a response echoes back, from the server's BaseDto.
     */
    struct EUCLID_CDK_API Metadata {
        std::string region;
        std::string accountId;
        std::string user;
    };

    /**
     * @brief A signing credential. The secret is returned once, at creation, and never again.
     */
    struct EUCLID_CDK_API AccessKey {
        std::string accessKeyId;
        bool active{true};
        std::string createdAt;
    };

    /**
     * @brief What a user may reach in one account: which namespaces, and whether they administer it.
     */
    struct EUCLID_CDK_API AccountGrant {
        std::string accountId;
        std::vector<std::string> namespaces;
        bool isAdmin{false};
        std::string granted;
    };

    /**
     * @brief A user. "password" is a hash when the server sends one at all - never the plaintext.
     */
    struct EUCLID_CDK_API User {
        std::string userId;
        std::string ern;
        std::string password;
        std::string email;
        std::string accountId;
        std::string region;
        std::vector<AccountGrant> accountGrants;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A named set of users. Membership in the "administrator" group is what makes an admin.
     */
    struct EUCLID_CDK_API UserGroup {
        std::string name;
        std::string ern;
        std::string accountId;
        std::string region;
        std::string description;
        std::vector<std::string> userIds;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A tenant. Namespaces live under it, and everything else is scoped by the pair.
     */
    struct EUCLID_CDK_API Account {
        std::string accountId;
        std::string name;
        std::string ern;
        std::string description;
        std::string created;
        std::string modified;
    };

    /**
     * @brief A namespace within an account, unique by name within it.
     */
    struct EUCLID_CDK_API Namespace {
        std::string accountId;
        std::string name;
        std::string ern;
        std::string description;
        std::string created;
        std::string modified;
    };

    /**
     * @brief What "login" answers with: a bearer token and, usually, an access key to sign with.
     */
    struct EUCLID_CDK_API LoginResult {
        std::string token;
        std::string accessKeyId;
        std::string secretAccessKey;
        std::string createdAt;
        bool isAdmin{false};
        Metadata metadata;

        /**
         * @brief The response as it arrived, for anything a later euclid added that this does not
         * name.
         */
        boost::json::object raw;
    };

    /**
     * @brief A newly created access key. This is the only time the secret is ever returned.
     */
    struct EUCLID_CDK_API CreateAccessKeyResult {
        std::string accessKeyId;
        std::string secretAccessKey;
        std::string createdAt;
        Metadata metadata;
    };

    /**
     * @brief One page of something, and how many exist in total.
     *
     * @par
     * "total" counts everything the listing matched, not what this page holds - it is what a caller
     * pages through.
     */
    template<typename T>
    struct Page {
        long total{};
        std::vector<T> items;
    };

    /**
     * @brief Reads a "metadata" object.
     */
    [[nodiscard]]
    EUCLID_CDK_API Metadata ToMetadata(const boost::json::value &value);

    /**
     * @brief Reads an access key.
     */
    [[nodiscard]]
    EUCLID_CDK_API AccessKey ToAccessKey(const boost::json::value &value);

    /**
     * @brief Reads an account grant.
     */
    [[nodiscard]]
    EUCLID_CDK_API AccountGrant ToAccountGrant(const boost::json::value &value);

    /**
     * @brief Reads a user.
     */
    [[nodiscard]]
    EUCLID_CDK_API User ToUser(const boost::json::value &value);

    /**
     * @brief Reads a user group.
     */
    [[nodiscard]]
    EUCLID_CDK_API UserGroup ToUserGroup(const boost::json::value &value);

    /**
     * @brief Reads an account.
     */
    [[nodiscard]]
    EUCLID_CDK_API Account ToAccount(const boost::json::value &value);

    /**
     * @brief Reads a namespace.
     */
    [[nodiscard]]
    EUCLID_CDK_API Namespace ToNamespace(const boost::json::value &value);

    /**
     * @brief Reads a login response.
     */
    [[nodiscard]]
    EUCLID_CDK_API LoginResult ToLoginResult(const boost::json::value &value);

    /**
     * @brief Reads a create-access-key response.
     */
    [[nodiscard]]
    EUCLID_CDK_API CreateAccessKeyResult ToCreateAccessKeyResult(const boost::json::value &value);

    /**
     * @brief Reads one page of whatever a listing returns, under the field the server puts it in.
     *
     * @param value  the response.
     * @param field  the field holding the array, e.g. "users".
     * @param parse  reader for one element.
     * @return the page.
     */
    template<typename T, typename Parser>
    [[nodiscard]] Page<T> ToPage(const boost::json::value &value, const std::string &field, Parser parse) {
        Page<T> page;
        page.total = Json::Number(value, "total");
        for (const auto &document: Json::Documents(value, field)) {
            page.items.push_back(parse(document));
        }
        return page;
    }

}// namespace Euclid::CDK::EAM
