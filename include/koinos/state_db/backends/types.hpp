#pragma once

#include <cstdint>
#include <string>

namespace koinos::state_db::backends::detail {

using key_type   = std::string;
using value_type = std::string;
using size_type  = uint64_t;

} // namespace koinos::state_db::backends::detail
