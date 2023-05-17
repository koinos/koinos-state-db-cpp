#include <koinos/state_db/detail/state_delta.hpp>

#include <koinos/crypto/merkle_tree.hpp>

#include <koinos/protocol/protocol.pb.h>

namespace koinos::state_db::detail {

using backend_type = state_delta::backend_type;
using value_type   = state_delta::value_type;

state_delta::state_delta( const std::optional< std::filesystem::path >& p )
{
   if ( p )
   {
      auto backend = std::make_shared< backends::rocksdb::rocksdb_backend >();
      backend->open( *p );
      _backend = backend;
   }
   else
   {
      _backend = std::make_shared< backends::map::map_backend >();
   }

   _revision = _backend->revision();
   _id = _backend->id();
   _merkle_root =  _backend->merkle_root();
}

void state_delta::put( const key_type& k, const value_type& v )
{
   _backend->put( k, v );
}

void state_delta::erase( const key_type& k )
{
   if ( find( k ) )
   {
      _backend->erase( k );
      _removed_objects.insert( k );
   }
}

const value_type* state_delta::find( const key_type& key ) const
{
   if ( auto val_ptr = _backend->get( key ); val_ptr )
      return val_ptr;

   if ( is_removed( key ) )
      return nullptr;

   return is_root() ? nullptr : _parent->find( key );
}

void state_delta::squash()
{
   if ( is_root() )
      return;

   // If an object is removed here and exists in the parent, it needs to only be removed in the parent
   // If an object is modified here, but removed in the parent, it needs to only be modified in the parent
   // These are O(m log n) operations. Because of this, squash should only be called from anonymouse state
   // nodes, whose modifications are much smaller
   for ( const key_type& r_key : _removed_objects )
   {
      _parent->_backend->erase( r_key );

      if ( !_parent->is_root() )
      {
         _parent->_removed_objects.insert( r_key );
      }
   }

   for ( auto itr = _backend->begin(); itr != _backend->end(); ++itr )
   {
      _parent->_backend->put( itr.key(), *itr );

      if ( !_parent->is_root() )
      {
         _parent->_removed_objects.erase( itr.key() );
      }
   }
}

void state_delta::commit()
{
   /**
    * commit works in two distinct phases. The first is head recursion until we are at the root
    * delta. At the root, we grab the backend and begin a write batch that will encompass all
    * state writes and the final write of the metadata.
    *
    * The second phase is popping off the stack, writing state to the backend. After all deltas
    * have been written to the backend, we write metadata to the backend and end the write batch.
    *
    * The result is this delta becomes the new root delta and state is written to the root backend
    * atomically.
    */
   KOINOS_ASSERT( !is_root(), internal_error, "cannot commit root" );

   std::vector< std::shared_ptr< state_delta > > node_stack;
   auto current_node = shared_from_this();

   while ( current_node )
   {
      node_stack.push_back( current_node );
      current_node = current_node->_parent;
   }

   // Because we already asserted we were not root, there will always exist a minimum of two nodes in the stack,
   // this and root.
   auto backend = node_stack.back()->_backend;
   node_stack.back()->_backend.reset();
   node_stack.pop_back();

   // Start the write batch
   backend->start_write_batch();

   // While there are nodes on the stack, write them to the backend
   while ( node_stack.size() )
   {
      auto& node = node_stack.back();

      for ( const key_type& r_key : node->_removed_objects )
      {
         backend->erase( r_key );
      }

      for ( auto itr = node->_backend->begin(); itr != node->_backend->end(); ++itr )
      {
         backend->put( itr.key(), *itr );
      }

      node_stack.pop_back();
   }

   // Update metadata on the backend
   backend->set_block_header( block_header() );
   backend->set_revision( _revision );
   backend->set_id( _id );
   backend->set_merkle_root( merkle_root() );
   backend->store_metadata();

   // End the write batch making the entire merge atomic
   backend->end_write_batch();

   // Reset local variables to match new status as root delta
   _removed_objects.clear();
   _backend = backend;
   _parent.reset();
}

void state_delta::clear()
{
   _backend->clear();
   _removed_objects.clear();

   _revision = 0;
   _id = crypto::multihash::zero( crypto::multicodec::sha2_256 );
}

bool state_delta::is_modified( const key_type& k ) const
{
   return _backend->get( k ) || _removed_objects.find( k ) != _removed_objects.end();
}

bool state_delta::is_removed( const key_type& k ) const
{
   return _removed_objects.find( k ) != _removed_objects.end();
}

bool state_delta::is_root() const
{
   return !_parent;
}

uint64_t state_delta::revision() const
{
   return _revision;
}

void state_delta::set_revision( uint64_t revision )
{
   _revision = revision;
   if ( is_root() )
   {
      _backend->set_revision( revision );
   }
}

bool state_delta::is_finalized() const
{
   return _finalized;
}

void state_delta::finalize()
{
   _finalized = true;
}

std::condition_variable_any& state_delta::cv()
{
   return _cv;
}

std::timed_mutex& state_delta::cv_mutex()
{
   return _cv_mutex;
}

crypto::multihash state_delta::merkle_root() const
{
   if ( !_merkle_root )
   {
      std::vector< std::string > object_keys;
      object_keys.reserve( _backend->size() + _removed_objects.size() );
      for ( auto itr = _backend->begin(); itr != _backend->end(); ++itr )
      {
         object_keys.push_back( itr.key() );
      }

      for ( const auto& removed : _removed_objects )
      {
         object_keys.push_back( removed );
      }

      std::sort(
         object_keys.begin(),
         object_keys.end()
      );

      std::vector< crypto::multihash > merkle_leafs;
      merkle_leafs.reserve( object_keys.size() * 2 );

      for ( const auto& key : object_keys )
      {
         merkle_leafs.emplace_back( crypto::hash( crypto::multicodec::sha2_256, key ) );
         auto val_ptr = _backend->get( key );
         merkle_leafs.emplace_back( crypto::hash( crypto::multicodec::sha2_256, val_ptr ? *val_ptr : std::string() ) );
      }

      _merkle_root = crypto::merkle_tree( crypto::multicodec::sha2_256, merkle_leafs ).root()->hash();
   }

   return *_merkle_root;
}

const protocol::block_header& state_delta::block_header() const
{
   return _backend->block_header();
}

std::shared_ptr< state_delta > state_delta::make_child( const state_node_id& id, const protocol::block_header& header )
{
   auto child = std::make_shared< state_delta >();
   child->_parent = shared_from_this();
   child->_id = id;
   child->_revision = _revision + 1;
   child->_backend = std::make_shared< backends::map::map_backend >();
   child->_backend->set_block_header( header );

   return child;
}

std::shared_ptr< state_delta > state_delta::clone( const state_node_id& id, const protocol::block_header& header )
{
   auto new_node = std::make_shared< state_delta >();
   new_node->_parent = _parent;
   new_node->_backend = _backend->clone();
   new_node->_removed_objects = _removed_objects;

   new_node->_id = id;
   new_node->_revision = _revision;
   new_node->_merkle_root = _merkle_root;

   new_node->_finalized = _finalized;

   new_node->_backend->set_id( id );
   new_node->_backend->set_revision( _revision );
   new_node->_backend->set_block_header( header );

   if ( _merkle_root )
   {
      new_node->_backend->set_merkle_root( *_merkle_root );
   }

   return new_node;
}

const std::shared_ptr< backend_type > state_delta::backend() const
{
   return _backend;
}

const state_node_id& state_delta::id() const
{
   return _id;
}

const state_node_id& state_delta::parent_id() const
{
   static const state_node_id null_id;
   return _parent ? _parent->_id : null_id;
}

std::shared_ptr< state_delta > state_delta::parent() const
{
   return _parent;
}

bool state_delta::is_empty() const
{
   if ( _backend->size() )
      return false;
   else if ( _parent )
      return _parent->is_empty();

   return true;
}

std::shared_ptr< state_delta > state_delta::get_root()
{
   if ( !is_root() )
   {
      if ( _parent->is_root() )
         return _parent;
      else
         return _parent->get_root();
   }

   return std::shared_ptr< state_delta >();
}

std::vector< protocol::state_delta_entry > state_delta::get_delta_entries() const {
   std::vector< std::string > object_keys;
   object_keys.reserve( _backend->size() + _removed_objects.size() );
   for ( auto itr = _backend->begin(); itr != _backend->end(); ++itr ) {
      object_keys.push_back( itr.key() );
   }

   for ( const auto &removed : _removed_objects ) {
      object_keys.push_back( removed );
   }

   std::sort( object_keys.begin(), object_keys.end() );

   std::vector< protocol::state_delta_entry > deltas;
   deltas.reserve( object_keys.size() );

   for ( const auto &key : object_keys ) {
      protocol::state_delta_entry entry;

      // Deserialize the key into a database_key object
      koinos::chain::database_key db_key;
      if (db_key.ParseFromString( key ))
      {
         entry.mutable_object_space()->set_system( db_key.space().system() );
         entry.mutable_object_space()->set_zone( db_key.space().zone() );
         entry.mutable_object_space()->set_id( db_key.space().id() );
         
         entry.set_key( db_key.key() );
         auto value = _backend->get( key );
         
         // Set the optional field if not null
         if (value != nullptr)
            entry.set_value(*value);
         
         deltas.push_back( entry );
      }
   }

   return deltas;
}

} // koinos::state_db::detail
