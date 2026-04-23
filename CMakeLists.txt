cmake_minimum_required(VERSION 3.31)
cmake_policy(SET CMP0169 OLD)
set(CMAKE_C_COMPILER "gcc-14")
set(CMAKE_CXX_COMPILER "g++-14")

project(utx)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(FetchContent)
find_package(OpenSSL 3.0 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_library(SODIUM_LIB sodium REQUIRED)
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

FetchContent_Declare(
        tree_sitter_runtime
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
        GIT_TAG v0.25.3
)

FetchContent_Declare(
        tree_sitter_html_grammar
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-html.git
        GIT_TAG v0.23.2
)

FetchContent_Declare(
        tree_sitter_css_grammar
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-css.git
        GIT_TAG v0.23.2
)

FetchContent_Declare(
        tree_sitter_javascript_grammar
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
        GIT_TAG v0.25.0
)


FetchContent_MakeAvailable(tree_sitter_runtime)

FetchContent_GetProperties(tree_sitter_html_grammar)
if (NOT tree_sitter_html_grammar_POPULATED)
    FetchContent_Populate(tree_sitter_html_grammar)
endif ()

FetchContent_GetProperties(tree_sitter_css_grammar)
if (NOT tree_sitter_css_grammar_POPULATED)
    FetchContent_Populate(tree_sitter_css_grammar)
endif ()

FetchContent_GetProperties(tree_sitter_javascript_grammar)
if (NOT tree_sitter_javascript_grammar_POPULATED)
    FetchContent_Populate(tree_sitter_javascript_grammar)
endif ()

add_library(tree-sitter STATIC
        ${tree_sitter_runtime_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree-sitter PUBLIC
        ${tree_sitter_runtime_SOURCE_DIR}/lib/include
)

function(add_tree_sitter_grammar target_name grammar_source_dir)
    set(grammar_sources
            ${grammar_source_dir}/src/parser.c
    )

    if (EXISTS "${grammar_source_dir}/src/scanner.c")
        list(APPEND grammar_sources "${grammar_source_dir}/src/scanner.c")
    endif ()

    if (EXISTS "${grammar_source_dir}/src/scanner.cc")
        list(APPEND grammar_sources "${grammar_source_dir}/src/scanner.cc")
    endif ()

    add_library(${target_name} STATIC ${grammar_sources})
    target_include_directories(${target_name} PUBLIC
            ${tree_sitter_runtime_SOURCE_DIR}/lib/include
            ${grammar_source_dir}/src
    )
    target_link_libraries(${target_name} PUBLIC tree-sitter)
endfunction()

add_tree_sitter_grammar(tree-sitter-html ${tree_sitter_html_grammar_SOURCE_DIR})
add_tree_sitter_grammar(tree-sitter-css ${tree_sitter_css_grammar_SOURCE_DIR})
add_tree_sitter_grammar(tree-sitter-javascript ${tree_sitter_javascript_grammar_SOURCE_DIR})

add_executable(utx
        main.cpp
        domain/Types.cpp
        infrastructure/chain/TxManager.cpp
        infrastructure/parser/GraphParser.cpp
        infrastructure/deploy_config/DeployConfig.cpp
        common/Uuid.cpp
        common/Hash.cpp
        domain/graph/Graph.cpp
        domain/graph/GraphElement.cpp
        domain/graph/GraphDiffer.cpp
        domain/graph/Action.cpp
        domain/deploy/DeployPlanner.cpp
        infrastructure/languages/javascript/JsParser.cpp
        infrastructure/languages/javascript/JsGenerator.cpp
        infrastructure/blackhole/BlackholeRebuilder.cpp
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
        tree-sitter
        tree-sitter-html
        tree-sitter-css
        tree-sitter-javascript
)
