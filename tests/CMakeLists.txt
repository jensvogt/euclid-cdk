find_package(Boost CONFIG REQUIRED COMPONENTS unit_test_framework)

add_executable(euclid-cdk-tests
        CredentialsTest.cpp
        EamTest.cpp
        EkmTest.cpp
        EnsTest.cpp
        EqsTest.cpp
        EsmTest.cpp
        EtsTest.cpp
        FakeGateway.cpp
        FakeStorage.cpp
        HttpSignatureTest.cpp
        SigV4Test.cpp
        TestMain.cpp
        TestSupport.cpp
        UrlTest.cpp)

target_link_libraries(euclid-cdk-tests PRIVATE ${EUCLID_CDK_DEFAULT_TARGET} Boost::unit_test_framework)
target_include_directories(euclid-cdk-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(euclid-cdk-tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)
euclid_cdk_copy_runtime_dlls(euclid-cdk-tests)

# Boost.Test's dynamic variant supplies its own main(); the static one does not, and mixing the two
# is the usual cause of an undefined-reference-to-main link failure here.
get_target_property(EUCLID_CDK_BOOST_TEST_TYPE Boost::unit_test_framework TYPE)
if (EUCLID_CDK_BOOST_TEST_TYPE STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(euclid-cdk-tests PRIVATE BOOST_TEST_DYN_LINK)
endif ()

# -------------------------------------------------------------------------
# One ctest entry per prefix rather than one for the whole binary. A test
# that hangs is killed by ctest's timeout with its output still in the
# buffer, so a single entry says only that "the tests" timed out - which is
# what a run on a machine nobody here can attach a debugger to says today.
# Split, ctest names the group that hung, and the groups run in parallel.
#
# Boost.Test fails a filter that matches nothing, so a prefix left behind by
# a renamed suite fails loudly rather than quietly running none of it. The
# list has to cover every BOOST_AUTO_TEST_SUITE name in this directory.
# -------------------------------------------------------------------------
foreach (EUCLID_CDK_SUITE IN ITEMS Credentials Eam Ekm Ens Eqs Esm Ets HttpSignature SigningScheme SigV4 Url)
    add_test(NAME ${EUCLID_CDK_SUITE} COMMAND euclid-cdk-tests --run_test=${EUCLID_CDK_SUITE}*)
endforeach ()
