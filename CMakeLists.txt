
cmake_minimum_required(VERSION 3.19.0)

cmake_policy(VERSION 3.19.0...3.27.4)

include(FetchContent)
FetchContent_Declare(
  koinos_cmake
  GIT_REPOSITORY https://github.com/koinos/koinos-cmake.git
  GIT_TAG        897f11188f1761ddd810d77839417549e68465aa)
FetchContent_MakeAvailable(koinos_cmake)

include("${koinos_cmake_SOURCE_DIR}/Koinos.cmake")

project(koinos_state_db
  VERSION 1.1.0
  DESCRIPTION "The Koinos statedb library"
  LANGUAGES CXX C)

koinos_define_version()

koinos_add_package(Boost CONFIG REQUIRED
  ADD_COMPONENTS log exception program_options test
  FIND_COMPONENTS log log_setup exception program_options
)

koinos_add_package(rocksdb NAME RocksDB CONFIG REQUIRED)
koinos_add_package(Protobuf CONFIG REQUIRED)
koinos_add_package(ethash CONFIG REQUIRED)
koinos_add_package(libsecp256k1-vrf CONFIG REQUIRED)
koinos_add_package(nlohmann_json CONFIG REQUIRED)
koinos_add_package(OpenSSL REQUIRED)
koinos_add_package(yaml-cpp CONFIG REQUIRED)
koinos_add_package(gRPC CONFIG REQUIRED)

koinos_add_package(koinos_proto CONFIG REQUIRED)
koinos_add_package(koinos_exception CONFIG REQUIRED)
koinos_add_package(koinos_log CONFIG REQUIRED)
koinos_add_package(koinos_crypto CONFIG REQUIRED)
koinos_add_package(koinos_util CONFIG REQUIRED)

add_subdirectory(src)
add_subdirectory(tests)
