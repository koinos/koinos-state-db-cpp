
#include <koinos/chain/chain.pb.h>
#include <koinos/exception.hpp>
#include <koinos/state_db/state_db.hpp>
#include <koinos/state_db/detail/merge_iterator.hpp>
#include <koinos/state_db/detail/state_delta.hpp>
#include <koinos/util/conversion.hpp>

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_set>
#include <utility>

namespace std {
   template<>
   struct hash< koinos::crypto::multihash >
   {
      std::size_t operator()( const koinos::crypto::multihash& mh ) const
      {
         static const std::hash< std::string > hash_fn;
         return hash_fn( koinos::util::converter::as< std::string >( mh ) );
      }
   };

}

namespace koinos::chain {
   bool operator==( const object_space& lhs, const object_space& rhs )
   {
      return lhs.system() == rhs.system()
         && lhs.zone() == rhs.zone()
         && lhs.id() == rhs.id();
   }

   bool operator<( const object_space& lhs, const object_space& rhs )
   {
      if ( lhs.system() < rhs.system() )
      {
         return true;
      }
      else if ( lhs.system() > rhs.system() )
      {
         return false;
      }

      if ( lhs.zone() < rhs.zone() )
      {
         return true;
      }
      else if ( lhs.system() > rhs.system() )
      {
         return false;
      }

      return lhs.id() < rhs.id();
   }
}

namespace koinos::state_db {

namespace detail {

struct by_id;
struct by_revision;
struct by_parent;

using state_delta_ptr = std::shared_ptr< state_delta >;

using state_multi_index_type = boost::multi_index_container<
   state_delta_ptr,
   boost::multi_index::indexed_by<
      boost::multi_index::ordered_unique<
         boost::multi_index::tag< by_id >,
            boost::multi_index::const_mem_fun< state_delta, const state_node_id&, &state_delta::id >
      >,
      boost::multi_index::ordered_non_unique<
         boost::multi_index::tag< by_parent >,
            boost::multi_index::const_mem_fun< state_delta, const state_node_id&, &state_delta::parent_id >
      >,
      boost::multi_index::ordered_non_unique<
         boost::multi_index::tag< by_revision >,
            boost::multi_index::const_mem_fun< state_delta, uint64_t, &state_delta::revision >
      >
   >
>;

const object_key null_key = object_key();

/**
 * Private implementation of state_node interface.
 *
 * Maintains a pointer to database_impl,
 * only allows reads / writes if the node corresponds to the DB's current state.
 */
class state_node_impl final
{
   public:
      state_node_impl() {}
      ~state_node_impl() {}

      const object_value* get_object( const object_space& space, const object_key& key ) const;
      std::pair< const object_value*, const object_key > get_next_object( const object_space& space, const object_key& key ) const;
      std::pair< const object_value*, const object_key > get_prev_object( const object_space& space, const object_key& key ) const;
      int64_t put_object( const object_space& space, const object_key& key, const object_value* val );
      int64_t remove_object( const object_space& space, const object_key& key );
      crypto::multihash merkle_root() const;

      state_node_ptr create_child( const state_node_id& id, const protocol::block_header& header );
      state_node_ptr clone( const state_node_id& id, const protocol::block_header& header );
      void finalize();
      void discard();
      void commit();

      state_delta_ptr                         _state;
      std::shared_ptr< locked_database_impl > _locked_db;
};

/**
 * Private implementation of database interface.
 *
 * This class relies heavily on using chainbase as a backing store.
 * Only one state_node can be active at a time.  This is enforced by
 * _current_node which is a weak_ptr.  New nodes will throw
 * NodeNotExpired exception if a
 */
class database_impl final
{
   public:
      database_impl() {}
      ~database_impl() { close_lockless(); }

      void open( const std::optional< std::filesystem::path >& p, genesis_init_function init, fork_resolution_algorithm algo );
      void open( const std::optional< std::filesystem::path >& p, genesis_init_function init, state_node_comparator_function comp );
      void open_lockless( const std::optional< std::filesystem::path >& p, genesis_init_function init, state_node_comparator_function comp );
      void close();
      void close_lockless();

      void reset();
      state_node_ptr get_node_at_revision( uint64_t revision, const state_node_id& child ) const;
      state_node_ptr get_node( const state_node_id& node_id ) const;
      state_node_ptr get_node_lockless( const state_node_id& node_id ) const;
      bool add_node( state_delta_ptr delta );
      void finalize_node( const state_node_id& node_id );
      void discard_node( const state_node_id& node_id, const std::unordered_set< state_node_id >& whitelist );
      void discard_node_lockless( const state_node_id& node_id, const std::unordered_set< state_node_id >& whitelist );
      void commit_node( const state_node_id& node_id );

      state_node_ptr get_head() const;
      state_node_ptr get_head_lockless() const;
      std::vector< state_node_ptr > get_fork_heads() const;
      std::vector< state_node_ptr > get_all_nodes() const;
      state_node_ptr get_root() const;
      state_node_ptr get_root_lockless() const;

      bool is_open() const;

      std::optional< std::filesystem::path >     _path;
      genesis_init_function                      _init_func = nullptr;
      state_node_comparator_function             _comp = nullptr;

      state_multi_index_type                     _index;
      state_delta_ptr                            _head;
      std::map< state_node_id, state_delta_ptr > _fork_heads;
      state_delta_ptr                            _root;

      /* Regarding mutexes used for synchronizing state_db...
       *
       * There are four mutexes that can be locked. They are:
       *   - _index_mutex (locks access to _index)
       *   - _fork_heads_mutex (locks access to _fork_heads)
       *   - database::_mutex (locks access to creating new state_node_ptrs)
       *   - state_delta::cv_mutex() (locks access to a state_delta cv)
       *
       * Consequently, _index_mutex must be locked last. All functions in state_db MUST
       * follow this convention or we risk deadlock.
       */
      mutable std::timed_mutex                   _index_mutex;
      mutable std::shared_mutex                  _fork_heads_mutex;
};

class locked_database_impl
{
   public:
      locked_database_impl( std::shared_ptr< detail::database_impl > db ) :
         _db( db )
      {}

      virtual ~locked_database_impl() = default;

      virtual bool is_unique_lock() const = 0;

      std::shared_ptr< detail::database_impl > _db;
};

class shared_locked_database_impl : public locked_database_impl
{
   public:
      shared_locked_database_impl(
         std::shared_ptr< detail::database_impl > db,
         std::shared_lock< std::shared_mutex > lock
      ) :
         locked_database_impl( db ),
         _lock( std::move( lock ) )
      {}

      bool is_unique_lock() const override
      {
         return false;
      }

      std::shared_lock< std::shared_mutex > _lock;
};

class unique_locked_database_impl : public locked_database_impl
{
   public:
      unique_locked_database_impl(
         std::shared_ptr< detail::database_impl > db,
         std::unique_lock< std::shared_mutex > lock
      ) :
         locked_database_impl( db ),
         _lock( std::move( lock ) )
      {}

      bool is_unique_lock() const override
      {
         return _lock.owns_lock();
      }

      std::unique_lock< std::shared_mutex > _lock;
};

void database_impl::reset()
{
   //
   // This method closes, wipes and re-opens the database.
   //
   // So the caller needs to be very careful to only call this method if deleting the database is desirable!
   //
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   std::unique_lock< std::shared_mutex > fork_heads_lock( _fork_heads_mutex );

   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );
   // Wipe and start over from empty database!
   _root->clear();
   close_lockless();
   open_lockless( _path, _init_func, _comp );
}

void database_impl::open( const std::optional< std::filesystem::path >& p, genesis_init_function init, fork_resolution_algorithm algo )
{
   state_node_comparator_function comp;

   switch ( algo )
   {
      case fork_resolution_algorithm::block_time:
         comp = &block_time_comparator;
         break;
      case fork_resolution_algorithm::pob:
         comp = &pob_comparator;
         break;
      case fork_resolution_algorithm::fifo:
         [[fallthrough]];
      default:
         comp = &fifo_comparator;
   }

   open( p, init, comp );
}

void database_impl::open( const std::optional< std::filesystem::path >& p, genesis_init_function init, state_node_comparator_function comp )
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   std::unique_lock< std::shared_mutex > fork_heads_lock( _fork_heads_mutex );
   open_lockless( p, init, comp );
}

void database_impl::open_lockless( const std::optional< std::filesystem::path >& p, genesis_init_function init, state_node_comparator_function comp )
{
   auto root = std::make_shared< state_node >();
   root->_impl->_state = std::make_shared< state_delta >( p );
   _init_func = init;
   _comp = comp;

   if ( !root->revision() && root->_impl->_state->is_empty() && _init_func )
   {
      init( root );
   }
   root->_impl->_state->finalize();
   _index.insert( root->_impl->_state );
   _root = root->_impl->_state;
   _head = root->_impl->_state;
   _fork_heads.insert_or_assign( _head->id(), _head );

   _path = p;
}

void database_impl::close()
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   std::unique_lock< std::shared_mutex > fork_heads_lock( _fork_heads_mutex );
   close_lockless();
}

void database_impl::close_lockless()
{
   _fork_heads.clear();
   _root.reset();
   _head.reset();
   _index.clear();
}

state_node_ptr database_impl::get_node_at_revision( uint64_t revision, const state_node_id& child_id ) const
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );
   KOINOS_ASSERT( revision >= _root->revision(), illegal_argument,
      "cannot ask for node with revision less than root. root rev: ${root}, requested: ${req}",
      ("root", _root->revision())("req", revision) );

   if( revision == _root->revision() )
   {
      return get_root_lockless();
   }

   auto child = get_node_lockless( child_id );
   if( !child )
      child = get_head_lockless();

   state_delta_ptr delta = child->_impl->_state;

   while( delta->revision() > revision )
   {
      delta = delta->parent();
   }

   auto node_itr = _index.find( delta->id() );

   KOINOS_ASSERT( node_itr != _index.end(), internal_error,
      "could not find state node associated with linked state_delta ${id}", ("id", delta->id() ) );

   auto node = std::make_shared< state_node >();
   node->_impl->_state = *node_itr;
   return node;
}

state_node_ptr database_impl::get_node( const state_node_id& node_id ) const
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );

   return get_node_lockless( node_id );
}


state_node_ptr database_impl::get_node_lockless( const state_node_id& node_id ) const
{
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );

   auto node_itr = _index.find( node_id );

   if ( node_itr != _index.end() )
   {
      auto node = std::make_shared< state_node >();
      node->_impl->_state = *node_itr;
      return node;
   }

   return state_node_ptr();
}

bool database_impl::add_node( state_delta_ptr delta )
{
   // Needs to be configurable
   auto timeout = std::chrono::system_clock::now() + std::chrono::seconds( 1 );

   state_node_ptr parent_state = get_node( delta->parent_id() );

   if ( parent_state )
   {
      std::unique_lock< std::timed_mutex > cv_lock( parent_state->_impl->_state->cv_mutex(), timeout );

      // We need to own the lock
      if ( cv_lock.owns_lock() )
      {
         // Check if the node is finalized
         bool is_finalized = parent_state->is_finalized();

         // If the node is finalized, try to wait for the node to be finalized
         if ( !is_finalized && parent_state->_impl->_state->cv().wait_until( cv_lock, timeout ) == std::cv_status::no_timeout )
            is_finalized = parent_state->is_finalized();

         // Finally, if the node is finalized, we can create a new writable node with the desired parent
         if ( is_finalized )
         {
            std::unique_lock< std::timed_mutex > index_lock( _index_mutex, timeout );

            // Ensure the parent node still exists in the index and then insert the new node
            return index_lock.owns_lock() && _index.find( delta->parent_id() ) != _index.end() && _index.insert( delta ).second;
         }
      }
   }

   return false;
}

void database_impl::finalize_node( const state_node_id& node_id )
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );
   auto node = get_node_lockless( node_id );
   KOINOS_ASSERT( node, illegal_argument, "node ${n} not found.", ("n", node_id) );

   {
      std::lock_guard< std::timed_mutex > index_lock( node->_impl->_state->cv_mutex() );

      node->_impl->_state->finalize();
   }

   node->_impl->_state->cv().notify_all();

   if ( node->revision() > _head->revision() )
   {
      _head = node->_impl->_state;
   }
   else if ( node->revision() == _head->revision() )
   {
      std::unique_lock< std::shared_mutex > fork_heads_lock( _fork_heads_mutex );
      fork_list forks;
      forks.reserve( _fork_heads.size() );
      std::transform(
         std::begin( _fork_heads ), std::end( _fork_heads ), std::back_inserter( forks ),
         []( const auto& entry )
         {
            state_node_ptr s = std::make_shared< state_node >();
            s->_impl->_state = entry.second;
            return s;
         }
      );

      auto head = get_head_lockless();
      if ( auto new_head = _comp( forks, head, node ); new_head != nullptr )
      {
         _head = new_head->_impl->_state;
      }
      else
      {
         _head = head->parent()->_impl->_state;
         auto head_itr = _fork_heads.find( head->id() );
         if ( head_itr != std::end( _fork_heads ) )
            _fork_heads.erase( head_itr );
         _fork_heads.insert_or_assign( head->parent()->id(), _head );
      }
   }

   // When node is finalized, parent node needs to be removed from heads, if it exists.
   std::unique_lock< std::shared_mutex > fork_heads_lock( _fork_heads_mutex );
   if ( node->parent_id() != _head->id() )
   {
      auto parent_itr = _fork_heads.find( node->parent_id() );
      if ( parent_itr != std::end( _fork_heads ) )
         _fork_heads.erase( parent_itr );

      _fork_heads.insert_or_assign( node->id(), node->_impl->_state );
   }
}

void database_impl::discard_node( const state_node_id& node_id, const std::unordered_set< state_node_id >& whitelist )
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   std::unique_lock< std::shared_mutex > fork_heads_lock( _fork_heads_mutex );
   discard_node_lockless( node_id, whitelist );
}

void database_impl::discard_node_lockless( const state_node_id& node_id, const std::unordered_set< state_node_id >& whitelist )
{
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );
   auto node = get_node_lockless( node_id );

   if( !node ) return;

   KOINOS_ASSERT( node_id != _root->id(), illegal_argument, "cannot discard root node" );

   std::vector< state_node_id > remove_queue{ node_id };
   const auto& previdx = _index.template get< by_parent >();
   const auto head_id = _head->id();

   for( uint32_t i = 0; i < remove_queue.size(); ++i )
   {
      KOINOS_ASSERT( remove_queue[ i ] != head_id, cannot_discard, "cannot discard a node that would result in discarding of head" );

      auto previtr = previdx.lower_bound( remove_queue[ i ] );
      while ( previtr != previdx.end() && (*previtr)->parent_id() == remove_queue[ i ] )
      {
         // Do not remove nodes on the whitelist
         if ( whitelist.find( (*previtr)->id() ) == whitelist.end() )
         {
            remove_queue.push_back( (*previtr)->id() );
         }

         ++previtr;
      }

      // We may discard one or more fork heads when discarding a minority fork tree
      // For completeness, we'll check every node to see if it is a fork head
      auto head_itr = _fork_heads.find( remove_queue[ i ] );
      if ( head_itr != _fork_heads.end() )
      {
         _fork_heads.erase( head_itr );
      }
   }

   for( const auto& id : remove_queue )
   {
      auto itr = _index.find( id );
      if ( itr != _index.end() )
         _index.erase( itr );
   }

   // When node is discarded, if the parent node is not a parent of other nodes (no forks), add it to heads.
   auto fork_itr = previdx.find( node->parent_id() );
   if ( fork_itr == previdx.end() )
   {
      auto parent_itr = _index.find( node->parent_id() );
      KOINOS_ASSERT( parent_itr != _index.end(), internal_error, "discarded parent node not found in node index" );
      _fork_heads.insert_or_assign( (*parent_itr)->id(), *parent_itr );
   }
}

void database_impl::commit_node( const state_node_id& node_id )
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   std::unique_lock< std::shared_mutex > fork_heads_lock( _fork_heads_mutex );
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );

   // If the node_id to commit is the root id, return. It is already committed.
   if ( node_id == _root->id() )
      return;

   auto node = get_node_lockless( node_id );
   KOINOS_ASSERT( node, illegal_argument, "node ${n} not found", ("n", node_id) );

   auto old_root = _root;
   _root = node->_impl->_state;

   _index.modify( _index.find( node_id ), []( state_delta_ptr& n ){ n->commit(); } );

   std::unordered_set< state_node_id > whitelist{ node_id };
   discard_node_lockless( old_root->id(), whitelist );
}

state_node_ptr database_impl::get_head() const
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );

   return get_head_lockless();
}

state_node_ptr database_impl::get_head_lockless() const
{
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );
   auto head = std::make_shared< state_node >();
   head->_impl->_state = _head;
   return head;
}

std::vector< state_node_ptr > database_impl::get_fork_heads() const
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   std::shared_lock< std::shared_mutex > fork_heads_lock( _fork_heads_mutex );
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );
   std::vector< state_node_ptr > fork_heads;
   fork_heads.reserve( _fork_heads.size() );

   for( auto& head : _fork_heads )
   {
      auto fork_head = std::make_shared< state_node >();
      fork_head->_impl->_state = head.second;
      fork_heads.push_back( fork_head );
   }

   return fork_heads;
}

std::vector< state_node_ptr > database_impl::get_all_nodes() const
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );
   std::vector< state_node_ptr > nodes;
   nodes.reserve( _index.size() );

   for ( const auto& delta : _index )
   {
      auto node = std::make_shared< state_node >();
      node->_impl->_state = delta;
      nodes.push_back( node );
   }

   return nodes;
}

state_node_ptr database_impl::get_root() const
{
   std::lock_guard< std::timed_mutex > index_lock( _index_mutex );

   return get_root_lockless();
}

state_node_ptr database_impl::get_root_lockless() const
{
   KOINOS_ASSERT( is_open(), database_not_open, "database is not open" );
   auto root = std::make_shared< state_node >();
   root->_impl->_state = _root;
   return root;
}

bool database_impl::is_open() const
{
   return (bool)_root && (bool)_head;
}

const object_value* state_node_impl::get_object( const object_space& space, const object_key& key ) const
{
   chain::database_key db_key;
   *db_key.mutable_space() = space;
   db_key.set_key( key );
   auto key_string = util::converter::as< std::string >( db_key );

   auto pobj = merge_state( _state ).find( key_string );

   if( pobj != nullptr )
   {
      return pobj;
   }

   return nullptr;
}

std::pair< const object_value*, const object_key > state_node_impl::get_next_object( const object_space& space, const object_key& key ) const
{
   chain::database_key db_key;
   *db_key.mutable_space() = space;
   db_key.set_key( key );
   auto key_string = util::converter::as< std::string >( db_key );

   auto state = merge_state( _state );
   auto it = state.lower_bound( key_string );

   if ( it != state.end() && it.key() == key_string )
   {
      it++;
   }

   if( it != state.end() )
   {
      chain::database_key next_key = util::converter::to< chain::database_key >( it.key() );

      if ( next_key.space() == space )
      {
         return { &*it, next_key.key() };
      }
   }

   return { nullptr, null_key };
}

std::pair< const object_value*, const object_key > state_node_impl::get_prev_object( const object_space& space, const object_key& key ) const
{
   chain::database_key db_key;
   *db_key.mutable_space() = space;
   db_key.set_key( key );
   auto key_string = util::converter::as< std::string >( db_key );

   auto state = merge_state( _state );
   auto it = state.lower_bound( key_string );

   if( it != state.begin() )
   {
      --it;
      chain::database_key next_key = util::converter::to< chain::database_key >( it.key() );

      if ( next_key.space() == space )
      {
         return { &*it, next_key.key() };
      }
   }

   return { nullptr, null_key };
}

int64_t state_node_impl::put_object( const object_space& space, const object_key& key, const object_value* val )
{
   KOINOS_ASSERT( !_state->is_finalized(), node_finalized, "cannot write to a finalized node" );

   chain::database_key db_key;
   *db_key.mutable_space() = space;
   db_key.set_key( key );
   auto key_string = util::converter::as< std::string >( db_key );

   int64_t bytes_used = 0;
   auto pobj = merge_state( _state ).find( key_string );

   if ( pobj != nullptr )
      bytes_used -= pobj->size();
   else
      bytes_used += key_string.size();

   bytes_used += val->size();
   _state->put( key_string, *val );

   return bytes_used;
}

int64_t state_node_impl::remove_object( const object_space& space, const object_key& key )
{
   KOINOS_ASSERT( !_state->is_finalized(), node_finalized, "cannot write to a finalized node" );

   chain::database_key db_key;
   *db_key.mutable_space() = space;
   db_key.set_key( key );
   auto key_string = util::converter::as< std::string >( db_key );

   int64_t bytes_used = 0;
   auto pobj = merge_state( _state ).find( key_string );

   if ( pobj != nullptr )
   {
      bytes_used -= pobj->size();
      bytes_used -= key_string.size();
   }

   _state->erase( key_string );

   return bytes_used;
}

crypto::multihash state_node_impl::merkle_root() const
{
   return _state->merkle_root();
}

state_node_ptr state_node_impl::create_child( const state_node_id& id, const protocol::block_header& header )
{
   auto node = std::make_shared< state_node >();
   node->_impl = std::make_unique< state_node_impl >();
   node->_impl->_state = _state->make_child( id, header );
   node->_impl->_locked_db = _locked_db;

   if ( _locked_db->_db->add_node( node->_impl->_state ) )
      return node;

   return state_node_ptr();
}

state_node_ptr state_node_impl::clone( const state_node_id& id, const protocol::block_header& header )
{
   auto node = std::make_shared< state_node >();
   node->_impl = std::make_unique< state_node_impl >();
   node->_impl->_state = _state->clone( id, header );
   node->_impl->_locked_db = _locked_db;

   if ( _locked_db->_db->add_node( node->_impl->_state ) )
      return node;

   return state_node_ptr();
}

void state_node_impl::finalize()
{
   _locked_db->_db->finalize_node( _state->id() );
}

void state_node_impl::discard()
{
   static const std::unordered_set< state_node_id > whitelist;
   _locked_db->_db->discard_node( _state->id(), whitelist );
}

void state_node_impl::commit()
{
   KOINOS_ASSERT( _locked_db->is_unique_lock(), illegal_argument, "committing a node requires a unique lock" );
   _locked_db->_db->commit_node( _state->id() );
}

} // detail

abstract_state_node::abstract_state_node() : _impl( new detail::state_node_impl() ) {}
abstract_state_node::~abstract_state_node() {}

const object_value* abstract_state_node::get_object( const object_space& space, const object_key& key ) const
{
   return _impl->get_object( space, key );
}

std::pair< const object_value*, const object_key > abstract_state_node::get_next_object( const object_space& space, const object_key& key ) const
{
   return _impl->get_next_object( space, key );
}

std::pair< const object_value*, const object_key > abstract_state_node::get_prev_object( const object_space& space, const object_key& key ) const
{
   return _impl->get_prev_object( space, key );
}

int64_t abstract_state_node::put_object( const object_space& space, const object_key& key, const object_value* val )
{
   return _impl->put_object( space, key, val );
}

int64_t abstract_state_node::remove_object( const object_space& space, const object_key& key )
{
   return _impl->remove_object( space, key );
}

bool abstract_state_node::is_finalized() const
{
   return _impl->_state->is_finalized();
}

crypto::multihash abstract_state_node::merkle_root() const
{
   KOINOS_ASSERT( is_finalized(), koinos::exception, "node must be finalized to calculation merkle root" );
   return _impl->merkle_root();
}

anonymous_state_node_ptr abstract_state_node::create_anonymous_node()
{
   auto anonymous_node = std::make_shared< anonymous_state_node >();
   anonymous_node->_parent = shared_from_derived();
   anonymous_node->_impl->_state = _impl->_state->make_child();
   anonymous_node->_impl->_locked_db = _impl->_locked_db;
   return anonymous_node;
}

state_node::state_node() : abstract_state_node() {}
state_node::~state_node() {}

const state_node_id& state_node::id() const
{
   return _impl->_state->id();
}

const state_node_id& state_node::parent_id() const
{
   return _impl->_state->parent_id();
}

uint64_t state_node::revision() const
{
   return _impl->_state->revision();
}

abstract_state_node_ptr state_node::parent() const
{
   auto parent_delta = _impl->_state->parent();
   if ( parent_delta )
   {
      auto parent_node = std::make_shared< state_node >();
      parent_node->_impl->_state = parent_delta;
      parent_node->_impl->_locked_db = _impl->_locked_db;
      return parent_node;
   }

   return abstract_state_node_ptr();
}

const protocol::block_header& state_node::block_header() const
{
   return _impl->_state->block_header();
}

state_node_ptr state_node::create_child( const state_node_id& id, const protocol::block_header& header )
{
   return _impl->create_child( id, header );
}

state_node_ptr state_node::clone( const state_node_id& id, const protocol::block_header& header )
{
   return _impl->clone( id, header );
}

void state_node::finalize()
{
   _impl->finalize();
}

void state_node::discard()
{
   _impl->discard();
}

void state_node::commit()
{
   _impl->commit();
}

abstract_state_node_ptr state_node::shared_from_derived()
{
   return shared_from_this();
}

anonymous_state_node::anonymous_state_node() : abstract_state_node() {}
anonymous_state_node::anonymous_state_node::~anonymous_state_node() {}

const state_node_id& anonymous_state_node::id() const
{
   return _parent->id();
}

const state_node_id& anonymous_state_node::parent_id() const
{
   return _parent->parent_id();
}

uint64_t anonymous_state_node::revision() const
{
   return _parent->revision();
}

abstract_state_node_ptr anonymous_state_node::parent() const
{
   return _parent;
}

const protocol::block_header& anonymous_state_node::block_header() const
{
   return _parent->block_header();
}

void anonymous_state_node::merge()
{
   KOINOS_ASSERT( !_parent->is_finalized(), node_finalized, "cannot merge to a finalized node" );
   _impl->_state->squash();
   reset();
}

void anonymous_state_node::reset()
{
   _impl->_state = _impl->_state->make_child();
}

abstract_state_node_ptr anonymous_state_node::shared_from_derived()
{
   return shared_from_this();
}


state_node_ptr fifo_comparator( fork_list& forks, state_node_ptr current_head, state_node_ptr new_head )
{
   return current_head;
}

state_node_ptr block_time_comparator( fork_list& forks, state_node_ptr head_block, state_node_ptr new_block )
{
   return new_block->block_header().timestamp() < head_block->block_header().timestamp() ? new_block : head_block;
}

state_node_ptr pob_comparator( fork_list& forks, state_node_ptr head_block, state_node_ptr new_block )
{
   if ( head_block->block_header().signer() != new_block->block_header().signer() )
      return new_block->block_header().timestamp() < head_block->block_header().timestamp() ? new_block : head_block;

   auto it = std::find_if( std::begin( forks ), std::end( forks ), [&]( state_node_ptr p ) { return p->id() == head_block->id(); } );
   if ( it != std::end( forks ) )
      forks.erase( it );

   struct {
      bool operator()( abstract_state_node_ptr a, abstract_state_node_ptr b ) const
      {
         if ( a->revision() > b->revision() )
            return true;
         else if ( a->revision() < b->revision() )
            return false;

         if ( a->block_header().timestamp() < b->block_header().timestamp() )
            return true;
         else if ( a->block_header().timestamp() > b->block_header().timestamp() )
            return false;

         if ( a->id() < b->id() )
            return true;

         return false;
      }
   } priority_algorithm;

   if ( std::size( forks ) )
   {
      std::sort( std::begin( forks ), std::end( forks ), priority_algorithm );
      it = std::begin( forks );
      return priority_algorithm( head_block->parent(), *it ) ? state_node_ptr() : *it;
   }

   return state_node_ptr();
}

database::database() : _impl( new detail::database_impl() ) {}
database::~database() {}

void database::open( const std::optional< std::filesystem::path >& p, genesis_init_function init, fork_resolution_algorithm algo )
{
   std::unique_lock< std::shared_mutex > lock( _mutex );
   _impl->open( p, init, algo );
}

void database::open( const std::optional< std::filesystem::path >& p, genesis_init_function init, state_node_comparator_function comp )
{
   std::unique_lock< std::shared_mutex > lock( _mutex );
   _impl->open( p, init, comp );
}

void database::close()
{
   std::unique_lock< std::shared_mutex > lock( _mutex );
   _impl->close();
}

void database::reset()
{
   std::unique_lock< std::shared_mutex > lock( _mutex );
   _impl->reset();
}

std::shared_ptr< locked_database > database::lock_database_shared()
{
   return std::make_shared< locked_database >(
      std::make_shared< detail::shared_locked_database_impl >(
         _impl,
         std::shared_lock< std::shared_mutex >( _mutex ) )
   );
}

std::shared_ptr< locked_database > database::lock_database_unique()
{
   return std::make_shared< locked_database >(
      std::make_shared< detail::unique_locked_database_impl >(
         _impl,
         std::unique_lock< std::shared_mutex >( _mutex ) )
   );
}

locked_database::locked_database( std::shared_ptr< detail::locked_database_impl > impl ) : _impl( impl ) {}

state_node_ptr locked_database::get_node_at_revision( uint64_t revision, const state_node_id& child_id ) const
{
   auto node = _impl->_db->get_node_at_revision( revision, child_id );
   node->_impl->_locked_db = _impl;
   return node;
}

state_node_ptr locked_database::get_node_at_revision( uint64_t revision ) const
{
   static const state_node_id null_id;

   auto node = _impl->_db->get_node_at_revision( revision, null_id );
   node->_impl->_locked_db = _impl;
   return node;
}

state_node_ptr locked_database::get_node( const state_node_id& id ) const
{
   auto node = _impl->_db->get_node( id );
   node->_impl->_locked_db = _impl;
   return node;
}

state_node_ptr locked_database::get_head() const
{
   auto head = _impl->_db->get_head();
   head->_impl->_locked_db = _impl;
   return head;
}

state_node_ptr locked_database::get_root() const
{
   auto root = _impl->_db->get_root();
   root->_impl->_locked_db = _impl;
   return root;
}

std::vector< state_node_ptr > locked_database::get_fork_heads() const
{
   auto fork_heads = _impl->_db->get_fork_heads();
   for ( auto& head : fork_heads )
   {
      head->_impl->_locked_db = _impl;
   }

   return fork_heads;
}

std::vector< state_node_ptr > locked_database::get_all_nodes() const
{
   auto nodes = _impl->_db->get_all_nodes();
   for ( auto& node : nodes )
   {
      node->_impl->_locked_db = _impl;
   }

   return nodes;
}

} // koinos::state_db
