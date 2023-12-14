add_library(state_db
  koinos/state_db/state_db.cpp
  koinos/state_db/state_delta.cpp
  koinos/state_db/merge_iterator.cpp
  koinos/state_db/backends/backend.cpp
  koinos/state_db/backends/iterator.cpp
  koinos/state_db/backends/map/map_backend.cpp
  koinos/state_db/backends/map/map_iterator.cpp
  koinos/state_db/backends/rocksdb/rocksdb_backend.cpp
  koinos/state_db/backends/rocksdb/rocksdb_iterator.cpp
  koinos/state_db/backends/rocksdb/object_cache.cpp

  koinos/state_db/merge_iterator.hpp
  koinos/state_db/state_delta.hpp

  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/state_db_types.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/state_db.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/backend.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/exceptions.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/iterator.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/types.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/map/map_backend.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/map/map_iterator.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/rocksdb/exceptions.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/rocksdb/object_cache.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/rocksdb/rocksdb_backend.hpp
  ${PROJECT_SOURCE_DIR}/include/koinos/state_db/backends/rocksdb/rocksdb_iterator.hpp)

target_link_libraries(
  state_db
    PUBLIC
      Koinos::exception
      Koinos::proto
      Koinos::crypto
      RocksDB::rocksdb)

koinos_add_format(TARGET state_db)

target_include_directories(
  state_db
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>)

koinos_install(TARGETS state_db)

install(
  DIRECTORY
    ${PROJECT_SOURCE_DIR}/include
  DESTINATION
    ${CMAKE_INSTALL_PREFIX})
