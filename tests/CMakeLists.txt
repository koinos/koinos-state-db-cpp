if (NOT BUILD_TESTING)
  return()
endif()

include(CTest)

koinos_add_test(
  state_db_tests
    SOURCES
      main.cpp
      state_db_test.cpp)

target_link_libraries(
  state_db_tests
    PRIVATE
      state_db
      Koinos::proto
      Koinos::crypto
      Koinos::log
      Koinos::util
      Koinos::exception)

target_include_directories(
  state_db_tests
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include)

koinos_add_format(TARGET state_db_tests)

koinos_coverage(
  EXECUTABLE
    state_db_tests
  EXCLUDE
    "tests/*")
