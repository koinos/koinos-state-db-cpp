find_package(Gperftools QUIET)
if(GPERFTOOLS_FOUND)
    message(STATUS "Found gperftools; compiling tests with TCMalloc")
    list(APPEND PLATFORM_SPECIFIC_LIBS tcmalloc)
endif()

include(CTest)

enable_testing()

file(GLOB UNIT_TESTS "tests/*.cpp")
file(GLOB_RECURSE TEST_FIXTURES "include/*.hpp")

koinos_parse_unit_tests(TEST_CASES ${UNIT_TESTS})

koinos_add_test(koinos_state_db_tests
   SOURCES ${UNIT_TESTS} ${TEST_FIXTURES}
   TESTS ${TEST_CASES}
)

target_link_libraries(koinos_state_db_tests Koinos::proto Koinos::crypto Koinos::state_db Koinos::log Koinos::util Koinos::exception ${PLATFORM_SPECIFIC_LIBS})
target_include_directories(koinos_state_db_tests PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>  # <prefix>/include
)
