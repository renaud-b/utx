cmake_minimum_required(VERSION 3.31)
cmake_policy(SET CMP0169 OLD)

project(utx)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(FetchContent)
find_package(OpenSSL 3.0 REQUIRED)
find_library(SODIUM_LIB sodium REQUIRED)


FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(nlohmann_json)

# --- Force spdlog static ---
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
)

FetchContent_MakeAvailable(spdlog)

include(FetchContent)
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.14.3
)

FetchContent_MakeAvailable(httplib)


add_executable(utx
        main.cpp
        domain/Types.cpp
        common/Uuid.cpp
        common/Hash.cpp
        domain/graph/Graph.cpp
        domain/graph/GraphElement.cpp
        domain/graph/Action.cpp
        infrastructure/languages/javascript/JsGenerator.cpp
        infrastructure/crypto/OpenSSLCryptoService.cpp
        infrastructure/deploy_config/DeployClient.cpp
)

target_include_directories(utx PRIVATE .)
target_link_libraries(utx
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        httplib::httplib
        spdlog::spdlog
        ${SODIUM_LIB}
)


include(CTest)

if (BUILD_TESTING)
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.15.2
    )

    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(utx_tests
            tests/TestDeployClient.cpp
            tests/TestDeployCommand.cpp
            tests/TestResetCommand.cpp
            tests/TestLoginCommand.cpp
            infrastructure/deploy_config/DeployClient.cpp
            domain/Types.cpp
            domain/graph/Graph.cpp
            domain/graph/GraphElement.cpp
            domain/graph/Action.cpp
            common/Hash.cpp
            common/Uuid.cpp
    )

    target_include_directories(utx_tests PRIVATE .)

    target_link_libraries(utx_tests PRIVATE
            OpenSSL::SSL
            OpenSSL::Crypto
            nlohmann_json::nlohmann_json
            httplib::httplib
            spdlog::spdlog
            ${SODIUM_LIB}
            GTest::gtest_main
                        )

    include(GoogleTest)
    gtest_discover_tests(utx_tests)
endif ()
