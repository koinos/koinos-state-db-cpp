#pragma once

#include <koinos/state_db/backends/iterator.hpp>

#include <koinos/crypto/multihash.hpp>
#include <koinos/protocol/protocol.pb.h>

namespace koinos::state_db::backends {

class abstract_backend
{
public:
  using key_type   = detail::key_type;
  using value_type = detail::value_type;
  using size_type  = detail::size_type;

  abstract_backend();
  virtual ~abstract_backend() {};

  virtual iterator begin() = 0;
  virtual iterator end()   = 0;

  virtual void put( const key_type& k, const value_type& v ) = 0;
  virtual const value_type* get( const key_type& ) const     = 0;
  virtual void erase( const key_type& k )                    = 0;
  virtual void clear()                                       = 0;

  virtual size_type size() const = 0;
  bool empty() const;

  virtual iterator find( const key_type& k )        = 0;
  virtual iterator lower_bound( const key_type& k ) = 0;

  size_type revision() const;
  void set_revision( size_type );

  const crypto::multihash& id() const;
  void set_id( const crypto::multihash& );

  const crypto::multihash& merkle_root() const;
  void set_merkle_root( const crypto::multihash& );

  const protocol::block_header& block_header() const;
  void set_block_header( const protocol::block_header& );

  virtual void start_write_batch() = 0;
  virtual void end_write_batch()   = 0;

  virtual void store_metadata() = 0;

  virtual std::shared_ptr< abstract_backend > clone() const = 0;

private:
  size_type _revision = 0;
  crypto::multihash _id;
  crypto::multihash _merkle_root;
  protocol::block_header _header;
};

} // namespace koinos::state_db::backends
