#pragma once
#include <koinos/state_db/state_db_types.hpp>

namespace koinos::state_db::backends {

KOINOS_DECLARE_DERIVED_EXCEPTION( backend_exception, state_db_exception );
KOINOS_DECLARE_DERIVED_EXCEPTION( iterator_exception, state_db_exception );
KOINOS_DECLARE_DERIVED_EXCEPTION( internal_exception, state_db_exception );

} // namespace koinos::state_db::backends
