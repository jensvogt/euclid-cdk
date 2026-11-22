// SPDX-License-Identifier: Apache-2.0

#pragma once

// C++ includes
#include <string>
#include <string_view>
#include <vector>

// Boost includes
#include <boost/json.hpp>

// Euclid includes
#include <euclid/cdk/Export.h>
#include <euclid/cdk/ModuleClient.h>
#include <euclid/cdk/dto/Eds.h>
#include <euclid/cdk/dto/Page.h>
#include <euclid/cdk/eam/Session.h>

namespace Euclid::CDK::EDS {

    /**
     * @brief The module this client talks to - what travels in x-euclid-target.
     */
    inline constexpr std::string_view Target = "eds";

    /**
     * @brief The record types a zone can hold.
     *
     * @par
     * These eight are what PutRecord() will store. GetAnswer() asks questions rather than stores
     * them, so it also takes TypeSoa and TypeAny, which a zone answers with but nobody puts.
     */
    inline constexpr std::string_view TypeA = "A";
    inline constexpr std::string_view TypeAaaa = "AAAA";
    inline constexpr std::string_view TypeCname = "CNAME";
    inline constexpr std::string_view TypeMx = "MX";
    inline constexpr std::string_view TypeNs = "NS";
    inline constexpr std::string_view TypePtr = "PTR";
    inline constexpr std::string_view TypeSrv = "SRV";
    inline constexpr std::string_view TypeTxt = "TXT";

    /**
     * @brief Types GetAnswer() can ask for but PutRecord() will not store.
     *
     * @par
     * The zone's SOA is made from the zone itself rather than stored in it, and ANY is a question
     * rather than a type at all.
     */
    inline constexpr std::string_view TypeSoa = "SOA";
    inline constexpr std::string_view TypeAny = "ANY";

    /**
     * @brief What an answer's rcode can be.
     *
     * @par
     * RcodeRefused is the one that is not about the name: it means this installation holds no zone
     * covering it, and so has nothing to say either way. RcodeNxDomain is a zone of ours saying the
     * name does not exist, which is a different and much stronger statement.
     */
    inline constexpr std::string_view RcodeNoError = "NOERROR";
    inline constexpr std::string_view RcodeNxDomain = "NXDOMAIN";
    inline constexpr std::string_view RcodeServFail = "SERVFAIL";
    inline constexpr std::string_view RcodeRefused = "REFUSED";

    /**
     * @brief The TTL a zone gives its records when CreateZone() is not told one, in seconds.
     */
    inline constexpr long DefaultTtl = 300;

    /**
     * @brief The largest TTL a zone or a record will take, in seconds - the RFC 2181 ceiling.
     */
    inline constexpr long MaxTtl = 2147483647;

    /**
     * @brief Which zones to list.
     */
    struct EUCLID_CDK_API ListZonesOptions {

        /**
         * @brief Only zones whose name starts with this; normalised the same way a zone name is, so
         * "Example" and "example" narrow alike.
         */
        std::string prefix;
        long pageSize{10};
        long pageIndex{0};
    };

    /**
     * @brief Which records of a zone to list.
     */
    struct EUCLID_CDK_API ListRecordsOptions {

        /**
         * @brief Only this name. Relative to the zone unless it ends in a dot, so "www" and
         * "www.example.com" mean the same thing in the zone example.com.
         */
        std::string name;

        /**
         * @brief Only this type, by mnemonic - TypeA and the rest. Empty for every type.
         */
        std::string type;
        long pageSize{10};
        long pageIndex{0};
    };

    /**
     * @brief EDS - euclid's domain name service: the zones this installation answers for.
     *
     * @par
     * @code
     * const auto session = EAM::Eam::ForServer(url).Credentials("jens", "secret").Login();
     * const EDS::Eds eds(session);
     *
     * eds.CreateZone("example.com", 300, "the example zone");
     * eds.PutRecord("example.com", "www", EDS::TypeA, 60, {"10.0.0.1", "10.0.0.2"});
     *
     * const auto answer = eds.GetAnswer("www.example.com", EDS::TypeA);
     * @endcode
     *
     * @par
     * Built from a session that has already logged in, and holding it rather than a copy of what it
     * knew at the time. The session has to outlive the client.
     *
     * @par Two servers, one module
     * EDS runs a name server beside the API these calls reach. The calls here manage what the zones
     * hold, with credentials and grants; the name server answers DNS queries on its own ports, to
     * anybody who asks and without any of that. GetAnswer() is the seam between them - it runs the
     * name server's resolution through the API, so a test can read what a resolver would get without
     * pointing one at the right port.
     *
     * @par Record sets, not records
     * PutRecord() replaces every value at one name and type, because that is the unit DNS actually
     * stores: three addresses for one name are one A set with three values, not three records. There
     * is no call to add one value to a set - read the set, add to Record::values, and put it back.
     * DeleteRecord() likewise removes the whole set.
     *
     * @par A zone you cannot see does not exist
     * Every call that names a zone answers 404 when it belongs to another account, exactly as it
     * does when there is no such zone. That is deliberate rather than sloppy: telling the two apart
     * would say who else uses this installation, and a zone name is often a company's name. The same
     * goes for a grant that does not cover the zone - ListZones() simply leaves out what the caller
     * may not read.
     *
     * @author jensvogt47\@gmail.com
     */
    class EUCLID_CDK_API Eds final : public ModuleClient {
    public:

        /**
         * @brief Builds EDS's operations on the credentials of a session that has already logged in.
         *
         * @param session the session; it has to outlive this client.
         */
        explicit Eds(const EAM::Session &session);

        /**
         * @brief Refused: the client holds the session rather than copying it, so one built from a
         * temporary would be left pointing at nothing at the end of the statement.
         */
        explicit Eds(EAM::Session &&) = delete;

        /**
         * @brief Takes on a zone, so this installation answers for every name under it.
         *
         * @par
         * The name is normalised - lower-cased and stripped of a trailing dot - so "Example.COM."
         * and "example.com" are one zone, and the second create of it is refused as a conflict
         * rather than quietly making a second.
         *
         * @par
         * Wildcards are not zone names. A zone can hold wildcard *records*, but a zone called
         * "*.example.com" would be a zone whose name is a pattern, which is not a thing DNS has.
         *
         * @param name        the zone, e.g. "example.com".
         * @param ttl         the TTL its records take when they name none of their own, in seconds;
         *                    0 means DefaultTtl.
         * @param description what it is for; free text, for whoever reads the listing later.
         * @return the zone as stored, with its ern and serial.
         * @throws EuclidError if the name is not a domain name, if the ttl is out of range, if a
         *         zone of that name exists, or if the name is under the domain euclid gives its own
         *         services, which holds names that are made rather than stored.
         */
        [[nodiscard]]
        Zone CreateZone(const std::string &name, long ttl = 0, const std::string &description = {}) const;

        /**
         * @brief One zone, by name.
         *
         * @param name the zone.
         * @throws EuclidError 404 if there is no such zone, or if it is not this account's - see the
         *         class note on why those are the same answer.
         */
        [[nodiscard]]
        Zone GetZone(const std::string &name) const;

        /**
         * @brief A page of the zones this account holds in the current namespace.
         *
         * @par
         * Zones the caller may not read are left out of the page, but counted in the total: the
         * total is what the account holds, the page is what this caller may see of it.
         *
         * @param options which zones, and which page of them.
         */
        [[nodiscard]]
        Page<Zone> ListZones(const ListZonesOptions &options = {}) const;

        /**
         * @brief Gives up a zone, and everything in it.
         *
         * @par
         * Every record goes with it - there is no refusal for a zone that is not empty, and nothing
         * to empty it with first. The count that comes back is how many record sets went.
         *
         * @param name the zone.
         * @return how many record sets were removed along with it.
         * @throws EuclidError 404 if there is no such zone, or it is not this account's.
         */
        [[nodiscard]]
        long DeleteZone(const std::string &name) const;

        /**
         * @brief Writes a record set - every value at one name and type, replacing whatever was
         * there.
         *
         * @par
         * The name may be relative to the zone ("www"), absolute ("www.example.com."), or "@" or
         * empty for the zone apex itself; all three end up fully qualified, and a name outside the
         * zone is refused rather than silently moved into it.
         *
         * @par
         * A CNAME is checked harder than the others, because the record type itself has a rule: it
         * says "this name is really that one", which leaves no room for anything else at the name.
         * So it takes exactly one value, cannot sit beside a record of another type, and cannot be
         * put at the apex, where the zone's own SOA and NS already live.
         *
         * @par
         * Values are in presentation form, the way a zone file writes them: "10.0.0.1" for an A,
         * "mail.example.com." for a CNAME, "10 mail.example.com." for an MX, "0 5 443 host." for an
         * SRV. A value this server could not serve is refused here rather than failing later when
         * the zone is loaded.
         *
         * @param zone   the zone the name is in.
         * @param name   the owner name, relative or absolute, or "@" for the apex.
         * @param type   TypeA and the rest; TypeSoa and TypeAny cannot be stored.
         * @param ttl    how long resolvers may hold it, in seconds; 0..MaxTtl.
         * @param values every value at this name and type; must not be empty.
         * @return the record set as stored.
         * @throws EuclidError if values is empty - use DeleteRecord() to remove a set, rather than
         *         putting an empty one - if the type is not storable, if the ttl is out of range, if
         *         a value is not valid for the type, or if the set would break the CNAME rule.
         */
        [[nodiscard]]
        Record PutRecord(const std::string &zone, const std::string &name, std::string_view type, long ttl,
                         const std::vector<std::string> &values) const;

        /**
         * @brief A page of a zone's record sets.
         *
         * @param zone    the zone.
         * @param options which records, and which page of them.
         */
        [[nodiscard]]
        Page<Record> ListRecords(const std::string &zone, const ListRecordsOptions &options = {}) const;

        /**
         * @brief Removes a whole record set.
         *
         * @param zone the zone.
         * @param name the owner name, relative or absolute, or "@" for the apex.
         * @param type which set at that name.
         * @throws EuclidError 404 if there is no set of that type at the name.
         */
        void DeleteRecord(const std::string &zone, const std::string &name, std::string_view type) const;

        /**
         * @brief What the name server would answer for a question, asked over the API instead of
         * over DNS.
         *
         * @par
         * The same resolution a query to the DNS port gets, wildcards, CNAME chasing and all - which
         * makes it the call to reach for when checking that a zone says what it was meant to, since
         * the alternative is a resolver pointed at the right port.
         *
         * @par
         * A name no zone of ours covers comes back with Answer::authoritative false and
         * RcodeRefused. This call does not go upstream: it reports what this installation knows, so
         * a name belonging to the rest of the internet is not an error here, just not ours.
         *
         * @param name the name asked about.
         * @param type the type asked about; TypeA when not given, as DNS itself defaults.
         * @throws EuclidError if the name is empty, or the type is not one EDS knows.
         */
        [[nodiscard]]
        Answer GetAnswer(const std::string &name, std::string_view type = TypeA) const;

        /**
         * @brief EDS's own metrics, as the server collects them. Answered unparsed - the shape
         * belongs to the monitoring module rather than to EDS.
         */
        [[nodiscard]]
        boost::json::object Metrics() const;
    };

}// namespace Euclid::CDK::EDS
