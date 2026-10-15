// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Ead.h>

namespace Euclid::CDK::EAD {

    AuditEvent ToAuditEvent(const boost::json::value &value) {
        return {
                .accountId = Json::Text(value, "accountId"),
                // "namespace" on the wire here, as in EAP, ETS and EAG.
                .nameSpace = Json::Text(value, "namespace"),
                .userId = Json::Text(value, "userId"),
                .module = Json::Text(value, "module"),
                .command = Json::Text(value, "command"),
                // Left as it came: what a command was called with is that command's own shape.
                .parameters = Json::Child(value, "parameters"),
                .status = Json::Text(value, "status"),
                .created = Json::Text(value, "created"),
        };
    }

    AuditPage ToAuditPage(const boost::json::value &value) {

        AuditPage page{
                .events = {},
                .total = Json::Number(value, "total"),
                // Defaulted true, because a server that does not send it is one that counted
                // exactly - reading its total as approximate would make a pager ask for a page
                // that is not there.
                .totalExact = Json::Flag(value, "totalExact", true),
        };

        for (const auto &document: Json::Documents(value, "events")) {
            page.events.push_back(ToAuditEvent(document));
        }
        return page;
    }

}// namespace Euclid::CDK::EAD
