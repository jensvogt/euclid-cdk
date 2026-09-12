cmake_minimum_required(VERSION 3.28)

# -------------------------------------------------------------------------
# Read version.txt - single source of truth, as in the euclid server repo
# -------------------------------------------------------------------------
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" EUCLID_CDK_VERSION_RAW)
string(STRIP "${EUCLID_CDK_VERSION_RAW}" EUCLID_CDK_VERSION)

project(euclid-cdk VERSION ${EUCLID_CDK_VERSION} LANGUAGES CXX)

option(EUCLID_CDK_BUILD_SHARED "Build the shared library" ON)
option(EUCLID_CDK_BUILD_STATIC "Build the static library" ON)
option(EUCLID_CDK_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(EUCLID_CDK_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(EUCLID_CDK_INSTALL "Generate the install and package-config targets" ${PROJECT_IS_TOP_LEVEL})

if (NOT EUCLID_CDK_BUILD_SHARED AND NOT EUCLID_CDK_BUILD_STATIC)
    message(FATAL_ERROR "EUCLID_CDK_BUILD_SHARED and EUCLID_CDK_BUILD_STATIC are both OFF - there is nothing to build")
endif ()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if (MSVC)
    # Matches the euclid server's own build: vcpkg's x64-windows-static triplet builds Boost and
    # OpenSSL against the static CRT, and every target here has to agree with them or linking fails
    # with LNK2038. /bigobj is what heavily-templated Boost.Beast translation units need (C1128).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/MP /FS /bigobj)
endif ()

if (WIN32)
    # Same baseline the server targets: Windows 10+ so boost::winapi exposes WaitOnAddress, and
    # NOMINMAX so Windows.h does not break std::numeric_limits<T>::max().
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()

# -------------------------------------------------------------------------
# Generate include/euclid/cdk/Version.h from version.txt
# -------------------------------------------------------------------------
set(EUCLID_CDK_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in ${EUCLID_CDK_GENERATED_DIR}/euclid/cdk/Version.h @ONLY)

find_package(Boost CONFIG REQUIRED COMPONENTS asio beast json system url)
find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)
find_package(Threads REQUIRED)

set(EUCLID_CDK_SOURCES
        src/Crypto.cpp
        src/Credentials.cpp
        src/Errors.cpp
        src/Euclid.cpp
        src/Json.cpp
        src/ModuleClient.cpp
        src/Url.cpp
        src/auth/HttpSignature.cpp
        src/auth/SigV4.cpp
        src/auth/SigningScheme.cpp
        src/dto/Com.cpp
        src/dto/Eam.cpp
        src/dto/Esm.cpp
        src/dto/Page.cpp
        src/eam/Eam.cpp
        src/eam/Session.cpp
        src/esm/Esm.cpp
        src/http/HttpClient.cpp)

set(EUCLID_CDK_HEADERS
        include/euclid/cdk/Credentials.h
        include/euclid/cdk/Crypto.h
        include/euclid/cdk/Errors.h
        include/euclid/cdk/Euclid.h
        include/euclid/cdk/Export.h
        include/euclid/cdk/Json.h
        include/euclid/cdk/ModuleClient.h
        include/euclid/cdk/Url.h
        include/euclid/cdk/auth/HttpSignature.h
        include/euclid/cdk/auth/SigV4.h
        include/euclid/cdk/auth/SigningScheme.h
        include/euclid/cdk/dto/Com.h
        include/euclid/cdk/dto/Eam.h
        include/euclid/cdk/dto/Esm.h
        include/euclid/cdk/dto/Page.h
        include/euclid/cdk/eam/Eam.h
        include/euclid/cdk/eam/Session.h
        include/euclid/cdk/esm/Esm.h
        include/euclid/cdk/http/HttpClient.h
        include/euclid/cdk/http/Message.h)

# -------------------------------------------------------------------------
# Both libraries are built from the same sources, compiled twice rather than
# shared through an OBJECT library: the export macros differ between the two
# (EUCLID_CDK_BUILDING_SHARED vs EUCLID_CDK_STATIC_DEFINE), and on Windows a
# static library built with __declspec(dllexport) in it is not usable.
# -------------------------------------------------------------------------
function(euclid_cdk_configure_target TARGET)
    target_sources(${TARGET} PRIVATE ${EUCLID_CDK_SOURCES} ${EUCLID_CDK_HEADERS})
    target_include_directories(${TARGET}
            PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${EUCLID_CDK_GENERATED_DIR}>
            $<INSTALL_INTERFACE:include>)
    target_link_libraries(${TARGET}
            PUBLIC Boost::asio Boost::beast Boost::json Boost::system Boost::url
            PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    set_target_properties(${TARGET} PROPERTIES
            CXX_SCAN_FOR_MODULES OFF
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            POSITION_INDEPENDENT_CODE ON)
endfunction()

if (EUCLID_CDK_BUILD_SHARED)
    add_library(euclid-cdk SHARED)
    add_library(euclid::cdk ALIAS euclid-cdk)
    euclid_cdk_configure_target(euclid-cdk)
    target_compile_definitions(euclid-cdk PRIVATE EUCLID_CDK_BUILDING_SHARED)
    # EXPORT_NAME, so that the name a consumer links after find_package() is the same euclid::cdk
    # the ALIAS above gives a project that added this one with add_subdirectory().
    set_target_properties(euclid-cdk PROPERTIES
            OUTPUT_NAME euclid-cdk
            EXPORT_NAME cdk
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})
    list(APPEND EUCLID_CDK_TARGETS euclid-cdk)
endif ()

if (EUCLID_CDK_BUILD_STATIC)
    add_library(euclid-cdk-static STATIC)
    add_library(euclid::cdk-static ALIAS euclid-cdk-static)
    euclid_cdk_configure_target(euclid-cdk-static)
    # PUBLIC, not PRIVATE: a consumer that links the static library has to see the same macro, or
    # its declarations say dllimport while the archive holds ordinary symbols.
    target_compile_definitions(euclid-cdk-static PUBLIC EUCLID_CDK_STATIC_DEFINE)
    set_target_properties(euclid-cdk-static PROPERTIES EXPORT_NAME cdk-static)
    # On a single-suffix platform both libraries can be called libeuclid-cdk; on Windows the shared
    # library's import library would collide with the static one, so there the name keeps its suffix.
    if (NOT WIN32)
        set_target_properties(euclid-cdk-static PROPERTIES OUTPUT_NAME euclid-cdk)
    endif ()
    list(APPEND EUCLID_CDK_TARGETS euclid-cdk-static)
endif ()

# What the tests and examples link: the shared library when it is built, so that what they exercise
# is what most consumers get, and the static one otherwise.
if (EUCLID_CDK_BUILD_SHARED)
    set(EUCLID_CDK_DEFAULT_TARGET euclid-cdk)
else ()
    set(EUCLID_CDK_DEFAULT_TARGET euclid-cdk-static)
endif ()

if (EUCLID_CDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (EUCLID_CDK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

# -------------------------------------------------------------------------
# Install, and a package config so a consumer can find_package(euclid-cdk)
# -------------------------------------------------------------------------
if (EUCLID_CDK_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS ${EUCLID_CDK_TARGETS}
            EXPORT euclid-cdk-targets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    install(DIRECTORY include/euclid DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES ${EUCLID_CDK_GENERATED_DIR}/euclid/cdk/Version.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/euclid/cdk)

    install(EXPORT euclid-cdk-targets
            FILE euclid-cdk-targets.cmake
            NAMESPACE euclid::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/euclid-cdk)

    configure_package_config_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/euclid-cdk-config.cmake.in
            ${CMAKE_CURRENT_BINARY_DIR}/euclid-cdk-config.cmake
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/euclid-cdk)

    write_basic_package_version_file(
            ${CMAKE_CURRENT_BINARY_DIR}/euclid-cdk-config-version.cmake
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion)

    install(FILES
            ${CMAKE_CURRENT_BINARY_DIR}/euclid-cdk-config.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/euclid-cdk-config-version.cmake
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/euclid-cdk)
endif ()
