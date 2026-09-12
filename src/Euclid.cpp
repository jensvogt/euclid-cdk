// SPDX-License-Identifier: Apache-2.0

// Euclid includes
#include <euclid/cdk/Euclid.h>

namespace Euclid::CDK {

    EAM::Session Login(const std::string &baseUrl, const std::string &username, const std::string &password) {
        return EAM::Eam::ForServer(baseUrl).Login(username, password);
    }

}// namespace Euclid::CDK
