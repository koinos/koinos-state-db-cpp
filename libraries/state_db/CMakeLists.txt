set(HEADERS
  include/koinos/state_db/state_db_types.hpp
  include/koinos/state_db/state_db.hpp
  include/koinos/state_db/backends/backend.hpp
  include/koinos/state_db/backends/exceptions.hpp
  include/koinos/state_db/backends/iterator.hpp
  include/koinos/state_db/backends/types.hpp
  include/koinos/state_db/backends/map/map_backend.hpp
  include/koinos/state_db/backends/map/map_iterator.hpp
  include/koinos/state_db/backends/rocksdb/exceptions.hpp
  include/koinos/state_db/backends/rocksdb/object_cache.hpp
  include/koinos/state_db/backends/rocksdb/rocksdb_backend.hpp
  include/koinos/state_db/backends/rocksdb/rocksdb_iterator.hpp
  include/koinos/state_db/detail/merge_iterator.hpp
  include/koinos/state_db/detail/state_delta.hpp)

add_library(state_db
  state_db.cpp
  detail/state_delta.cpp
  detail/merge_iterator.cpp
  backends/backend.cpp
  backends/iterator.cpp
  backends/map/map_backend.cpp
  backends/map/map_iterator.cpp
  backends/rocksdb/rocksdb_backend.cpp
  backends/rocksdb/rocksdb_iterator.cpp
  backends/rocksdb/object_cache.cpp
  ${HEADERS})

target_link_libraries(
  state_db
    PUBLIC
      Koinos::exception
      Koinos::proto
      Koinos::crypto
      RocksDB::rocksdb)

koinos_add_format_target(state_db)

target_include_directories(
  state_db
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
)

add_library(Koinos::state_db ALIAS state_db)

install(
  FILES
    ${HEADERS}
  DESTINATION
    "include/koinos/state_db")
