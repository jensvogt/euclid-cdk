// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Eds.h>

namespace Euclid::CDK::EDS {

    Zone ToZone(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .ern = Json::Text(value, "ern"),
                // "namespace" on the wire, as in EAP, ETS, EAG and EAD.
                .nameSpace = Json::Text(value, "namespace"),
                .ttl = Json::Number(value, "ttl"),
                .description = Json::Text(value, "description"),
                .recordCount = Json::Number(value, "recordCount"),
                .serial = Json::Number(value, "serial"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Record ToRecord(const boost::json::value &value) {
        return {
                .zone = Json::Text(value, "zone"),
                .name = Json::Text(value, "name"),
                .type = Json::Text(value, "type"),
                .ttl = Json::Number(value, "ttl"),
                .values = Json::Strings(value, "values"),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    AnswerRecord ToAnswerRecord(const boost::json::value &value) {
        return {
                .name = Json::Text(value, "name"),
                .type = Json::Text(value, "type"),
                .ttl = Json::Number(value, "ttl"),
                // One value per record here, where a stored set carries all of them under
                // "values" - the answer is what goes on the wire rather than what is stored.
                .value = Json::Text(value, "value"),
        };
    }

    Answer ToAnswer(const boost::json::value &value) {

        Answer answer{
                .name = Json::Text(value, "name"),
                .type = Json::Text(value, "type"),
                .authoritative = Json::Flag(value, "authoritative"),
                .zone = Json::Text(value, "zone"),
                .rcode = Json::Text(value, "rcode"),
                .answers = {},
                .authority = {},
        };

        for (const auto &document: Json::Documents(value, "answers")) {
            answer.answers.push_back(ToAnswerRecord(document));
        }
        for (const auto &document: Json::Documents(value, "authority")) {
            answer.authority.push_back(ToAnswerRecord(document));
        }
        return answer;
    }

}// namespace Euclid::CDK::EDS
