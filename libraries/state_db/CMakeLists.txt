file(GLOB HEADERS
     "include/koinos/state_db/*.hpp"
     "include/koinos/state_db/detail/*.hpp"
     "include/koinos/state_db/backends/*.hpp"
     "include/koinos/state_db/backends/rocksdb/*.hpp")
add_library(koinos_state_db
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
            ${HEADERS} )

target_link_libraries(koinos_state_db Koinos::exception Koinos::proto Koinos::crypto RocksDB::rocksdb)
target_include_directories(koinos_state_db PUBLIC
   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
   $<INSTALL_INTERFACE:include>
)

add_library(Koinos::state_db ALIAS koinos_state_db)

install(FILES ${HEADERS} DESTINATION "include/koinos/state_db")

export(
   TARGETS ${KOINOS_LIB_TARGET_NAME}
   NAMESPACE Koinos::
   FILE ${KOINOS_LIB_CMAKE_PROJECT_TARGETS_FILE}
)

install(
   TARGETS ${KOINOS_LIB_TARGET_NAME}
   EXPORT ${KOINOS_LIB_TARGETS_EXPORT_NAME}
   INCLUDES DESTINATION ${KOINOS_LIB_INCLUDE_INSTALL_DIR}
   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)

install(
   EXPORT ${KOINOS_LIB_TARGETS_EXPORT_NAME}
   NAMESPACE Koinos::
   DESTINATION ${KOINOS_LIB_CONFIG_INSTALL_DIR}
)
