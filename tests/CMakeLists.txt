include(CTest)

enable_testing()

koinos_add_test(state_db_tests
  SOURCES
    main.cpp
    state_db_test.cpp
)

target_link_libraries(state_db_tests Koinos::proto Koinos::crypto Koinos::state_db Koinos::log Koinos::util Koinos::exception ${PLATFORM_SPECIFIC_LIBS})
target_include_directories(state_db_tests PUBLIC
  ${PROJECT_SOURCE_DIR}/src # Private headers
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>  # <prefix>/include
)

koinos_add_format(TARGET state_db_tests)

koinos_coverage(
  EXECUTABLE
    state_db_tests
  EXCLUDE
    "tests/*"
)
