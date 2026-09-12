// SPDX-License-Identifier: Apache-2.0

#define BOOST_TEST_MODULE euclid_cdk

// C++ includes
#include <cstdlib>
#include <filesystem>
#include <string>

// Boost includes
#include <boost/test/unit_test.hpp>

namespace {

    /**
     * @brief Points EUCLID_CREDENTIALS_FILE at a scratch file for the whole run.
     *
     * @par
     * Without this, any test that logs in with the cache on would read and then overwrite the
     * developer's real ~/.euclid/credentials - logging them out of euclid-cli as a side effect of
     * running the tests. Set once here rather than per test, because the variable is read when the
     * file is touched rather than when a client is built, so a test that forgot would still find it.
     */
    struct ScratchCredentials {

        ScratchCredentials() {
            const auto path = std::filesystem::temp_directory_path() / "euclid-cdk-tests-credentials";
            _path = path.string();
#if defined(_WIN32)
            _putenv_s("EUCLID_CREDENTIALS_FILE", _path.c_str());
#else
            setenv("EUCLID_CREDENTIALS_FILE", _path.c_str(), 1);
#endif
            std::error_code ec;
            std::filesystem::remove(_path, ec);
        }

        ~ScratchCredentials() {
            std::error_code ec;
            std::filesystem::remove(_path, ec);
        }

        std::string _path;
    };

}// namespace

BOOST_GLOBAL_FIXTURE(ScratchCredentials);
