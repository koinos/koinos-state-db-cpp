
#pragma once
#include <koinos/state_db/state_db_types.hpp>

#include <koinos/protocol/protocol.pb.h>

#include <boost/multiprecision/cpp_int.hpp>

#include <any>
#include <cstddef>
#include <filesystem>
#include <shared_mutex>
#include <memory>
#include <vector>

namespace koinos::state_db {

namespace detail {

class database_impl;
class locked_database_impl;
class state_node_impl;
class anonymous_state_node_impl;

} // detail

class locked_database;
class abstract_state_node;
class anonymous_state_node;

using abstract_state_node_ptr = std::shared_ptr< abstract_state_node >;
using anonymous_state_node_ptr = std::shared_ptr< anonymous_state_node >;

enum class fork_resolution_algorithm
{
   fifo,
   block_time,
   pob
};

/**
 * Allows querying the database at a particular checkpoint.
 */
class abstract_state_node
{
   public:
      abstract_state_node();
      virtual ~abstract_state_node();

      /**
       * Fetch an object if one exists.
       *
       * - Size of the object is written into result.size
       * - Object's value is copied into args.buf, provided buf != nullptr and buf_size is large enough
       * - If buf is too small, buf is unchanged, however result is still updated
       * - args.key is copied into result.key
       */
      const object_value* get_object( const object_space& space, const object_key& key ) const;

      /**
       * Get the next object.
       *
       * - Size of the object is written into result.size
       * - Object's value is copied into args.buf, provided buf != nullptr and buf_size is large enough
       * - If buf is too small, buf is unchanged, however result is still updated
       * - Found key is written into result
       */
      std::pair< const object_value*, const object_key > get_next_object( const object_space& space, const object_key& key ) const;

      /**
       * Get the previous object.
       *
       * - Size of the object is written into result.size
       * - Object's value is copied into args.buf, provided buf != nullptr and buf_size is large enough
       * - If buf is too small, buf is unchanged, however result is still updated
       * - Found key is written into result
       */
      std::pair< const object_value*, const object_key > get_prev_object( const object_space& space, const object_key& key ) const;

      /**
       * Write an object into the state_node.
       *
       * - Fail if node is not writable.
       * - If object exists, object is overwritten.
       */
      int64_t put_object( const object_space& space, const object_key& key, const object_value* val );

      /**
       * Remove an object from the state_node
       */
      int64_t remove_object( const object_space& space, const object_key& key );

      /**
       * Return true if the node is writable.
       */
      bool is_finalized() const;

      /**
       * Return the merkle root of writes on this state node
       */
      crypto::multihash merkle_root() const;

      /**
       * Returns an anonymous state node with this node as its parent.
       */
      anonymous_state_node_ptr create_anonymous_node();

      virtual const state_node_id&          id() const = 0;
      virtual const state_node_id&          parent_id() const = 0;
      virtual uint64_t                      revision() const = 0;
      virtual abstract_state_node_ptr       parent() const = 0;
      virtual const protocol::block_header& block_header() const = 0;

      friend class locked_database;
      friend class detail::database_impl;
      friend class detail::state_node_impl;

   protected:
      virtual std::shared_ptr< abstract_state_node > shared_from_derived() = 0;

      std::unique_ptr< detail::state_node_impl > _impl;
};

class anonymous_state_node final : public abstract_state_node, public std::enable_shared_from_this< anonymous_state_node >
{
   public:
      anonymous_state_node();
      ~anonymous_state_node();

      const state_node_id&          id() const override;
      const state_node_id&          parent_id() const override;
      uint64_t                      revision() const override;
      abstract_state_node_ptr       parent() const override;
      const protocol::block_header& block_header() const override;

      /**
       * Merge the node in to its parent
       */
      void merge();

      /**
       * Reset the node
       */
      void reset();

      friend class abstract_state_node;

   protected:
      std::shared_ptr< abstract_state_node > shared_from_derived() override;

   private:
      abstract_state_node_ptr _parent;
};

class state_node;

using state_node_ptr = std::shared_ptr< state_node >;

/**
 * Allows querying the database at a particular checkpoint.
 */
class state_node final : public abstract_state_node, public std::enable_shared_from_this< state_node >
{
   public:
      state_node();
      ~state_node();

      const state_node_id&          id() const override;
      const state_node_id&          parent_id() const override;
      uint64_t                      revision() const override;
      abstract_state_node_ptr       parent() const override;
      const protocol::block_header& block_header() const override;

      /**
       * Create a new state_node and add it to the database.
       *
       * Writing to the returned node will not modify the parent node.
       *
       * If this node parent is subsequently discarded, the database preserves
       * as much of the parent's state storage as necessary to continue
       * to serve queries on any (non-discarded) children.  A discarded
       * parent node's state may internally be merged into a child's
       * state storage area, allowing the parent's state storage area
       * to be freed. This merge may occur immediately, or it may be
       * deferred or parallelized.
       */
      state_node_ptr create_child( const state_node_id& id, const protocol::block_header& header );

      /**
       * Clone a node with a new id and block header, adding it to the database.
       *
       * Cannot clone a finalized node.
       */
      state_node_ptr clone( const state_node_id& id, const protocol::block_header& header );

      /**
       * Finalize the node. It will no longer be writable.
       */
      void finalize();

      /**
       * Discard the node and its children from the database.
       *
       * The node will remain valid until released by the user, but will no longer
       * be retrievable from the database.
       */
      void discard();

      /**
       * Squash the node in to the root state, committing it.
       * Branching state between this node and its ancestor will be discarded
       * and no longer accesible.
       *
       * Commit requires a unique lock. If the state node was not returned
       * from a uniquely locked database, this call will throw.
       */
      void commit();

   protected:
      std::shared_ptr< abstract_state_node > shared_from_derived() override;
};

using genesis_init_function = std::function< void( state_node_ptr ) >;
using fork_list = std::vector< state_node_ptr >;
using state_node_comparator_function = std::function< state_node_ptr( fork_list&, state_node_ptr, state_node_ptr ) >;
using shared_lock_ptr = std::shared_ptr< const std::shared_lock< std::shared_mutex > >;
using unique_lock_ptr = std::shared_ptr< const std::unique_lock< std::shared_mutex > >;

state_node_ptr fifo_comparator( fork_list& forks, state_node_ptr current_head, state_node_ptr new_head );
state_node_ptr block_time_comparator( fork_list& forks, state_node_ptr current_head, state_node_ptr new_head );
state_node_ptr pob_comparator( fork_list& forks, state_node_ptr current_head, state_node_ptr new_head );

class locked_database;

/**
 * database is designed to provide parallel access to the database across
 * different states.
 *
 * It does by tracking positive state deltas, which can be merged on the fly
 * at read time to return the correct state of the database. A database
 * checkpoint is represented by the state_node class. Reads and writes happen
 * against a state_node.
 *
 * States are organized as a tree with the assumption that one path wins out
 * over time and cousin paths are discarded as the root is advanced.
 *
 * Currently, database is not thread safe. That is, calls directly on database
 * are not thread safe. (i.e. deleting a node concurrently to creating a new
 * node can leave database in an undefined state)
 *
 * Conccurrency across state nodes is supported native to the implementation
 * without locks. Writes on a single state node need to be serialized, but
 * reads are implicitly parallel.
 */
class database final
{
   public:
      database();
      ~database();

      /**
       * Open the database.
       */
      void open( const std::optional< std::filesystem::path >& p, genesis_init_function init, fork_resolution_algorithm algo );

      /**
       * Open the database.
       */
      void open( const std::optional< std::filesystem::path >& p, genesis_init_function init, state_node_comparator_function comp );

      /**
       * Close the database.
       */
      void close();

      /**
       * Reset the database.
       */
      void reset();

      /**
       * Returns a shared pointer to a locked instance of the database.
       * This instance is locked with a shared lock.
       */
      std::shared_ptr< locked_database > lock_database_shared();

      /**
       * Returns a shared pointer to a locked instance of the database.
       * This instance is locked with a unique lock.
       */
      std::shared_ptr< locked_database > lock_database_unique();

   private:
      std::shared_ptr< detail::database_impl > _impl;
      std::shared_mutex                        _mutex;
};

class locked_database
{
   public:
      locked_database( std::shared_ptr< detail::locked_database_impl > );

      /**
       * Get an ancestor of a node at a particular revision
       */
      state_node_ptr get_node_at_revision( uint64_t revision, const state_node_id& child_id ) const;
      state_node_ptr get_node_at_revision( uint64_t revision ) const;

      /**
       * Get the state_node for the given state_node_id.
       *
       * Return an empty pointer if no node for the given id exists.
       */
      state_node_ptr get_node( const state_node_id& node_id ) const;

      /**
       * Get and return the current "head" node.
       *
       * Head is determined by longest chain. Oldest
       * chain wins in a tie of length. Only finalized
       * nodes are eligible to become head.
       */
      state_node_ptr get_head() const;

      /**
       * Get and return the current "root" node.
       *
       * All state nodes are guaranteed to a descendant of root.
       */
      state_node_ptr get_root() const;

      /**
       * Get and return a vector of all fork heads.
       *
       * Fork heads are any finalized nodes that do
       * not have finalized children.
       */
      std::vector< state_node_ptr > get_fork_heads() const;

      /**
       * Get and return a vector of all nodes.
       */
      std::vector< state_node_ptr > get_all_nodes() const;

   private:
      std::shared_ptr< detail::locked_database_impl > _impl;
      std::shared_mutex                               _mutex;

      friend class database;
};

} // koinos::state_db
