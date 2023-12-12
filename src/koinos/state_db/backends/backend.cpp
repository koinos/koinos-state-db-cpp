#include <koinos/state_db/backends/backend.hpp>

namespace koinos::state_db::backends {

abstract_backend::abstract_backend():
    _id( crypto::multihash::zero( crypto::multicodec::sha2_256 ) )
{}

bool abstract_backend::empty() const
{
  return size() == 0;
}

abstract_backend::size_type abstract_backend::revision() const
{
  return _revision;
}

void abstract_backend::set_revision( abstract_backend::size_type revision )
{
  _revision = revision;
}

const crypto::multihash& abstract_backend::id() const
{
  return _id;
}

void abstract_backend::set_id( const crypto::multihash& id )
{
  _id = id;
}

const crypto::multihash& abstract_backend::merkle_root() const
{
  return _merkle_root;
}

void abstract_backend::set_merkle_root( const crypto::multihash& merkle_root )
{
  _merkle_root = merkle_root;
}

const protocol::block_header& abstract_backend::block_header() const
{
  return _header;
}

void abstract_backend::set_block_header( const protocol::block_header& header )
{
  _header = header;
}

} // namespace koinos::state_db::backends
