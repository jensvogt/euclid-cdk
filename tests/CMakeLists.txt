find_package(Boost CONFIG REQUIRED COMPONENTS unit_test_framework)

add_executable(euclid-cdk-tests
        CredentialsTest.cpp
        EamTest.cpp
        EnsTest.cpp
        EqsTest.cpp
        EsmTest.cpp
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

# Boost.Test's dynamic variant supplies its own main(); the static one does not, and mixing the two
# is the usual cause of an undefined-reference-to-main link failure here.
get_target_property(EUCLID_CDK_BOOST_TEST_TYPE Boost::unit_test_framework TYPE)
if (EUCLID_CDK_BOOST_TEST_TYPE STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(euclid-cdk-tests PRIVATE BOOST_TEST_DYN_LINK)
endif ()

add_test(NAME euclid-cdk-tests COMMAND euclid-cdk-tests)
