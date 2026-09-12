// SPDX-License-Identifier: Apache-2.0

#pragma once

// Boost includes
#include <boost/beast/http.hpp>

namespace Euclid::CDK {

    /**
     * @brief The request the signing schemes work on, and the one that goes on the wire.
     *
     * @par
     * Deliberately Boost.Beast's own type rather than a transport-agnostic stand-in: a signature
     * has to be computed over headers that are already set and then written back as more headers,
     * and the object that is signed has to be the object that is sent. Anything in between is a
     * second chance for the two to disagree, and a signature that covers a Host header the client
     * then spells differently fails on arrival saying nothing about why.
     *
     * @par
     * It is also the type euclid's own Core::SigV4 and Core::HttpSignature sign and verify, so the
     * canonicalization here and the one on the server are the same code reading the same object.
     */
    using Request = boost::beast::http::request<boost::beast::http::string_body>;

    /**
     * @brief The response as Boost.Beast parses it.
     */
    using RawResponse = boost::beast::http::response<boost::beast::http::string_body>;

}// namespace Euclid::CDK
