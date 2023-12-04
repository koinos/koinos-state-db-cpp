
cmake_minimum_required(VERSION 3.19.0)

cmake_policy(SET CMP0074 NEW)
cmake_policy(SET CMP0135 NEW)
cmake_policy(SET CMP0114 NEW)
cmake_policy(SET CMP0144 NEW)

include(cmake/Koinos.cmake)

project(koinos_state_db
  VERSION 1.1.0
  DESCRIPTION "The Koinos statedb library"
  LANGUAGES CXX C)

koinos_define_version()

koinos_coverage(
  EXECUTABLE
  koinos_statedb_tests
  EXCLUDE
    "tests/*"
)

koinos_add_package(Boost CONFIG REQUIRED
  ADD_COMPONENTS log exception test
  FIND_COMPONENTS log log_setup
)

koinos_add_package(rocksdb NAME RocksDB CONFIG REQUIRED)
koinos_add_package(Protobuf CONFIG REQUIRED)
koinos_add_package(ethash CONFIG REQUIRED)
koinos_add_package(libsecp256k1-vrf CONFIG REQUIRED)
koinos_add_package(nlohmann_json CONFIG REQUIRED)
koinos_add_package(OpenSSL REQUIRED)
koinos_add_package(yaml-cpp)
koinos_add_package(gRPC CONFIG REQUIRED)

koinos_add_package(koinos_proto CONFIG REQUIRED)
koinos_add_package(koinos_exception CONFIG REQUIRED)
koinos_add_package(koinos_log CONFIG REQUIRED)
koinos_add_package(koinos_crypto CONFIG REQUIRED)
koinos_add_package(koinos_util CONFIG REQUIRED)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(libraries)
add_subdirectory(tests)

export(
  TARGETS state_db
  NAMESPACE Koinos::
  FILE ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-targets.cmake
)

install(
  TARGETS state_db
  EXPORT ${PROJECT_NAME}-targets
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
  EXPORT ${PROJECT_NAME}-targets
  NAMESPACE Koinos::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

configure_package_config_file(
  cmake/Templates/project.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
  DESTINATION
    ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
