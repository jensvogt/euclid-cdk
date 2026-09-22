// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/dto/Eag.h>
#include <euclid/cdk/eag/Eag.h>

namespace Euclid::CDK::EAG {

    bool Route::IsUpload() const { return type == TypeUpload; }

    bool Route::IsModuleRoute() const { return !moduleTarget.empty(); }

    bool Route::IsAuthenticated() const { return !authentication.empty() && authentication != AuthNone; }

    bool Listener::IsHttps() const { return protocol == ProtocolHttps; }

    UploadSpec ToUploadSpec(const boost::json::value &value) {
        return {
                .bucket = Json::Text(value, "bucket"),
                .keyPrefix = Json::Text(value, "keyPrefix"),
                .maxBytes = Json::Number(value, "maxBytes"),
                .partSize = Json::Number(value, "partSize"),
                .contentTypes = Json::Strings(value, "contentTypes"),
        };
    }

    Route ToRoute(const boost::json::value &value) {
        return {
                .routeId = Json::Text(value, "routeId"),
                .ern = Json::Text(value, "ern"),
                .accountId = Json::Text(value, "accountId"),
                .region = Json::Text(value, "region"),
                // "namespace" here, as in EAP and ETS; every other module spells it "nameSpace".
                .nameSpace = Json::Text(value, "namespace"),
                .path = Json::Text(value, "path"),
                .type = Json::Text(value, "type"),
                .upload = ToUploadSpec(Json::Child(value, "upload")),
                .applicationId = Json::Text(value, "applicationId"),
                .moduleTarget = Json::Text(value, "moduleTarget"),
                .moduleAction = Json::Text(value, "moduleAction"),
                .methods = Json::Strings(value, "methods"),
                .authentication = Json::Text(value, "authentication"),
                // A route the server describes is one it stores, and a stored route that says
                // nothing about being active was written before the field existed - which is a
                // route that serves. Defaulting it to false would take every one of them offline in
                // the eyes of whatever reads this.
                .active = Json::Flag(value, "active", true),
                .created = Json::Text(value, "created"),
                .modified = Json::Text(value, "modified"),
        };
    }

    Listener ToListener(const boost::json::value &value) {
        return {
                .nameSpace = Json::Text(value, "namespace"),
                .port = Json::Number(value, "port"),
                .protocol = Json::Text(value, "protocol"),
                .serving = Json::Flag(value, "serving"),
                .certificate = Json::Text(value, "certificate"),
                .certificateConfigured = Json::Text(value, "certificateConfigured"),
                .certificateFound = Json::Flag(value, "certificateFound"),
                .certificateErn = Json::Text(value, "certificateErn"),
                .certificateSubject = Json::Text(value, "certificateSubject"),
                .certificateIssuer = Json::Text(value, "certificateIssuer"),
                .certificateSerialNumber = Json::Text(value, "certificateSerialNumber"),
                .certificateFingerprint = Json::Text(value, "certificateFingerprint"),
                .certificateSubjectAltNames = Json::Strings(value, "certificateSubjectAltNames"),
                .certificateGenerated = Json::Flag(value, "certificateGenerated"),
                .certificateNotBefore = Json::Text(value, "certificateNotBefore"),
                .certificateNotAfter = Json::Text(value, "certificateNotAfter"),
                .certificateExpired = Json::Flag(value, "certificateExpired"),
        };
    }

    ListenersResult ToListenersResult(const boost::json::value &value) {

        ListenersResult result{
                .listeners = {},
                .total = Json::Number(value, "total"),
                .serving = Json::Flag(value, "serving"),
        };
        for (const auto &document: Json::Documents(value, "listeners")) {
            result.listeners.push_back(ToListener(document));
        }
        return result;
    }

}// namespace Euclid::CDK::EAG
