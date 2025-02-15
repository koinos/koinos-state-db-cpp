#include <boost/test/unit_test.hpp>

#include <koinos/bigint.hpp>
#include <koinos/crypto/merkle_tree.hpp>
#include <koinos/crypto/multihash.hpp>
#include <koinos/exception.hpp>
#include <koinos/log.hpp>
#include <koinos/state_db/backends/map/map_backend.hpp>
#include <koinos/state_db/backends/rocksdb/rocksdb_backend.hpp>
#include <koinos/state_db/merge_iterator.hpp>
#include <koinos/state_db/state_db.hpp>
#include <koinos/state_db/state_delta.hpp>
#include <koinos/util/conversion.hpp>
#include <koinos/util/random.hpp>

#include <deque>
#include <filesystem>
#include <iostream>

using namespace koinos;
using namespace koinos::state_db;
using state_db::detail::merge_state;
using state_db::detail::state_delta;
using namespace std::string_literals;

struct test_block
{
  std::string previous;
  uint64_t height = 0;
  uint64_t nonce  = 0;

  crypto::multihash get_id() const;
};

crypto::multihash test_block::get_id() const
{
  return crypto::hash( crypto::multicodec::sha2_256,
                       util::converter::to< crypto::multihash >( previous ),
                       height,
                       nonce );
}

struct state_db_fixture
{
  state_db_fixture()
  {
    initialize_logging( "koinos_test", {}, "info" );

    temp = std::filesystem::temp_directory_path() / util::random_alphanumeric( 8 );
    std::filesystem::create_directory( temp );

    db.open( temp, [ & ]( state_db::state_node_ptr root ) {}, fork_resolution_algorithm::fifo, db.get_unique_lock() );
  }

  ~state_db_fixture()
  {
    boost::log::core::get()->remove_all_sinks();
    db.close( db.get_unique_lock() );
    std::filesystem::remove_all( temp );
  }

  database db;
  std::filesystem::path temp;
};

BOOST_FIXTURE_TEST_SUITE( state_db_tests, state_db_fixture )

BOOST_AUTO_TEST_CASE( basic_test )
{
  try
  {
    BOOST_TEST_MESSAGE( "Creating object" );
    object_space space;
    std::string a_key = "a";
    std::string a_val = "alice";

    auto shared_db_lock = db.get_shared_lock();

    chain::database_key db_key;
    *db_key.mutable_space() = space;
    db_key.set_key( a_key );
    auto key_size = util::converter::as< std::string >( db_key ).size();

    crypto::multihash state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    auto state_1               = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                            state_id,
                                            protocol::block_header(),
                                            shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK_EQUAL( state_1->put_object( space, a_key, &a_val ), a_val.size() + key_size );

    // Object should not exist on older state node
    BOOST_CHECK_EQUAL( db.get_root( shared_db_lock )->get_object( space, a_key ), nullptr );

    auto ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, a_val );

    BOOST_TEST_MESSAGE( "Modifying object" );

    a_val = "alicia";
    BOOST_CHECK_EQUAL( state_1->put_object( space, a_key, &a_val ), 1 );

    ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, a_val );

    state_id     = crypto::hash( crypto::multicodec::sha2_256, 2 );
    auto state_2 = db.create_writable_node( state_1->id(), state_id, protocol::block_header(), shared_db_lock );
    BOOST_CHECK( !state_2 );

    db.finalize_node( state_1->id(), shared_db_lock );

    BOOST_REQUIRE_THROW( state_1->put_object( space, a_key, &a_val ), node_finalized );

    state_2 = db.create_writable_node( state_1->id(), state_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_2 );
    a_val = "alex";
    BOOST_CHECK_EQUAL( state_2->put_object( space, a_key, &a_val ), -2 );

    ptr = state_2->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, a_val );

    ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, "alicia" );

    BOOST_TEST_MESSAGE( "Erasing object" );
    state_2->remove_object( space, a_key );

    BOOST_CHECK( !state_2->get_object( space, a_key ) );

    db.discard_node( state_2->id(), shared_db_lock );
    state_2 = db.get_node( state_2->id(), shared_db_lock );
    BOOST_CHECK( !state_2 );

    ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, "alicia" );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( fork_tests )
{
  try
  {
    BOOST_TEST_MESSAGE( "Basic fork tests on state_db" );
    crypto::multihash id, prev_id, block_1000_id;
    test_block b;

    auto shared_db_lock = db.get_shared_lock();

    prev_id = db.get_root( shared_db_lock )->id();

    for( uint64_t i = 1; i <= 2'000; ++i )
    {
      b.previous = util::converter::as< std::string >( prev_id );
      b.height   = i;
      id         = b.get_id();

      auto new_block = db.create_writable_node( prev_id, id, protocol::block_header(), shared_db_lock );
      BOOST_CHECK_EQUAL( b.height, new_block->revision() );
      db.finalize_node( id, shared_db_lock );

      prev_id = id;

      if( i == 1'000 )
        block_1000_id = id;
    }

    BOOST_REQUIRE( db.get_root( shared_db_lock )->id() == crypto::multihash::zero( crypto::multicodec::sha2_256 ) );
    BOOST_REQUIRE( db.get_root( shared_db_lock )->revision() == 0 );

    BOOST_REQUIRE( db.get_head( shared_db_lock )->id() == prev_id );
    BOOST_REQUIRE( db.get_head( shared_db_lock )->revision() == 2'000 );

    BOOST_REQUIRE( db.get_node( block_1000_id, shared_db_lock )->id() == block_1000_id );
    BOOST_REQUIRE( db.get_node( block_1000_id, shared_db_lock )->revision() == 1'000 );

    auto fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE_EQUAL( fork_heads.size(), 1 );
    BOOST_REQUIRE( fork_heads[ 0 ]->id() == db.get_head( shared_db_lock )->id() );
    fork_heads.clear();

    BOOST_TEST_MESSAGE( "Test commit" );
    shared_db_lock.reset();
    db.commit_node( block_1000_id, db.get_unique_lock() );
    shared_db_lock = db.get_shared_lock();
    BOOST_REQUIRE( db.get_root( shared_db_lock )->id() == block_1000_id );
    BOOST_REQUIRE( db.get_root( shared_db_lock )->revision() == 1'000 );

    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE_EQUAL( fork_heads.size(), 1 );
    BOOST_REQUIRE( fork_heads[ 0 ]->id() == db.get_head( shared_db_lock )->id() );

    crypto::multihash block_2000_id = id;

    BOOST_TEST_MESSAGE( "Test discard" );
    b.previous = util::converter::as< std::string >( db.get_head( shared_db_lock )->id() );
    b.height   = db.get_head( shared_db_lock )->revision() + 1;
    id         = b.get_id();
    db.create_writable_node( util::converter::to< crypto::multihash >( b.previous ),
                             id,
                             protocol::block_header(),
                             shared_db_lock );
    auto new_block = db.get_node( id, shared_db_lock );
    BOOST_REQUIRE( new_block );

    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE_EQUAL( fork_heads.size(), 1 );
    BOOST_REQUIRE( fork_heads[ 0 ]->id() == prev_id );

    db.discard_node( id, shared_db_lock );

    BOOST_REQUIRE( db.get_head( shared_db_lock )->id() == prev_id );
    BOOST_REQUIRE( db.get_head( shared_db_lock )->revision() == 2'000 );

    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE_EQUAL( fork_heads.size(), 1 );
    BOOST_REQUIRE( fork_heads[ 0 ]->id() == prev_id );

    // Shared ptr should still exist, but not be returned with get_node
    BOOST_REQUIRE( new_block );
    BOOST_REQUIRE( !db.get_node( id, shared_db_lock ) );
    new_block.reset();

    // Cannot discard head
    BOOST_REQUIRE_THROW( db.discard_node( prev_id, shared_db_lock ), cannot_discard );

    BOOST_TEST_MESSAGE( "Check duplicate node creation" );
    BOOST_REQUIRE( !db.create_writable_node( db.get_head( shared_db_lock )->parent_id(),
                                             db.get_head( shared_db_lock )->id(),
                                             protocol::block_header(),
                                             shared_db_lock ) );

    BOOST_TEST_MESSAGE( "Check failed linking" );
    crypto::multihash zero = crypto::multihash::zero( crypto::multicodec::sha2_256 );
    BOOST_REQUIRE( !db.create_writable_node( zero, id, protocol::block_header(), shared_db_lock ) );

    crypto::multihash head_id = db.get_head( shared_db_lock )->id();
    uint64_t head_rev         = db.get_head( shared_db_lock )->revision();

    BOOST_TEST_MESSAGE( "Test minority fork" );
    auto fork_node = db.get_node_at_revision( 1'995, shared_db_lock );
    prev_id        = fork_node->id();
    b.nonce        = 1;

    auto old_block_1996_id = db.get_node_at_revision( 1'996, shared_db_lock )->id();
    auto old_block_1997_id = db.get_node_at_revision( 1'997, shared_db_lock )->id();

    for( uint64_t i = 1; i <= 5; ++i )
    {
      b.previous = util::converter::as< std::string >( prev_id );
      b.height   = fork_node->revision() + i;
      id         = b.get_id();

      auto new_block = db.create_writable_node( prev_id, id, protocol::block_header(), shared_db_lock );
      BOOST_CHECK_EQUAL( b.height, new_block->revision() );
      db.finalize_node( id, shared_db_lock );

      BOOST_CHECK( db.get_head( shared_db_lock )->id() == head_id );
      BOOST_CHECK( db.get_head( shared_db_lock )->revision() == head_rev );

      prev_id = id;
    }

    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE_EQUAL( fork_heads.size(), 2 );
    BOOST_REQUIRE( ( fork_heads[ 0 ]->id() == db.get_head( shared_db_lock )->id() && fork_heads[ 1 ]->id() == id )
                   || ( fork_heads[ 1 ]->id() == db.get_head( shared_db_lock )->id() && fork_heads[ 0 ]->id() == id ) );
    auto old_head_id = db.get_head( shared_db_lock )->id();

    b.previous = util::converter::as< std::string >( prev_id );
    b.height   = head_rev + 1;
    id         = b.get_id();

    // When this node finalizes, it will be the longest path and should become head
    new_block = db.create_writable_node( prev_id, id, protocol::block_header(), shared_db_lock );
    BOOST_CHECK_EQUAL( b.height, new_block->revision() );

    BOOST_CHECK( db.get_head( shared_db_lock )->id() == head_id );
    BOOST_CHECK( db.get_head( shared_db_lock )->revision() == head_rev );

    db.finalize_node( id, shared_db_lock );

    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE_EQUAL( fork_heads.size(), 2 );
    BOOST_REQUIRE( ( fork_heads[ 0 ]->id() == id && fork_heads[ 1 ]->id() == old_head_id )
                   || ( fork_heads[ 1 ]->id() == id && fork_heads[ 0 ]->id() == old_head_id ) );

    BOOST_CHECK( db.get_head( shared_db_lock )->id() == id );
    BOOST_CHECK( db.get_head( shared_db_lock )->revision() == b.height );

    db.discard_node( old_block_1997_id, shared_db_lock );
    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE_EQUAL( fork_heads.size(), 2 );
    BOOST_REQUIRE( ( fork_heads[ 0 ]->id() == id && fork_heads[ 1 ]->id() == old_block_1996_id )
                   || ( fork_heads[ 1 ]->id() == id && fork_heads[ 0 ]->id() == old_block_1996_id ) );

    db.discard_node( old_block_1996_id, shared_db_lock );
    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE_EQUAL( fork_heads.size(), 1 );
    BOOST_REQUIRE( fork_heads[ 0 ]->id() == id );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( merge_iterator )
{
  try
  {
    std::filesystem::path temp = std::filesystem::temp_directory_path() / koinos::util::random_alphanumeric( 8 );
    std::filesystem::create_directory( temp );

    using state_delta_ptr = std::shared_ptr< state_delta >;
    std::deque< state_delta_ptr > delta_queue;
    delta_queue.emplace_back( std::make_shared< state_delta >( temp ) );

    // alice: 1
    // bob: 2
    // charlie: 3
    delta_queue.back()->put( "alice", "1" );
    delta_queue.back()->put( "bob", "2" );
    delta_queue.back()->put( "charlie", "3" );

    {
      merge_state m_state( delta_queue.back() );
      auto itr = m_state.begin();

      BOOST_REQUIRE( itr != m_state.end() );
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "1" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "2" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      ++itr;
      BOOST_REQUIRE( itr == m_state.end() );
      BOOST_CHECK_THROW( *itr, koinos::exception );
      BOOST_CHECK_THROW( ++itr, koinos::exception );
      BOOST_CHECK_THROW( itr.key(), koinos::exception );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "2" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "1" );
    }

    // alice: 4
    // bob: 5
    // charlie: 3 (not changed)
    delta_queue.emplace_back( delta_queue.back()->make_child( delta_queue.back()->id() ) );
    delta_queue.back()->put( "alice", "4" );
    delta_queue.back()->put( "bob", "5" );

    {
      merge_state m_state( delta_queue.back() );
      auto itr = m_state.begin();

      BOOST_REQUIRE( itr != m_state.end() );
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "5" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      ++itr;
      BOOST_REQUIRE( itr == m_state.end() );
      BOOST_CHECK_THROW( *itr, koinos::exception );
      BOOST_CHECK_THROW( ++itr, koinos::exception );
      BOOST_CHECK_THROW( itr.key(), koinos::exception );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "5" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
    }

    // alice: 4 (not changed)
    // bob: 6
    // charlie: 3 (not changed)
    delta_queue.emplace_back( delta_queue.back()->make_child( delta_queue.back()->id() ) );
    delta_queue.back()->put( "bob", "6" );

    {
      merge_state m_state( delta_queue.back() );
      auto itr = m_state.begin();

      BOOST_REQUIRE( itr != m_state.end() );
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      ++itr;
      BOOST_REQUIRE( itr == m_state.end() );
      BOOST_CHECK_THROW( *itr, koinos::exception );
      BOOST_CHECK_THROW( ++itr, koinos::exception );
      BOOST_CHECK_THROW( itr.key(), koinos::exception );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
    }

    // alice: (removed)
    // bob: 6 (not changed)
    // charlie: 3 (not changed)
    delta_queue.emplace_back( delta_queue.back()->make_child( delta_queue.back()->id() ) );
    delta_queue.back()->erase( "alice" );

    {
      merge_state m_state( delta_queue.back() );
      auto itr = m_state.begin();

      BOOST_REQUIRE( itr != m_state.end() );
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      ++itr;
      BOOST_REQUIRE( itr == m_state.end() );
      BOOST_CHECK_THROW( *itr, koinos::exception );
      BOOST_CHECK_THROW( ++itr, koinos::exception );
      BOOST_CHECK_THROW( itr.key(), koinos::exception );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
    }

    // alice: 4 (restored)
    // bob: 6 (not changed)
    // charlie: 3 (not changed)
    delta_queue.emplace_back( delta_queue.back()->make_child( delta_queue.back()->id() ) );
    delta_queue.back()->put( "alice", "4" );

    {
      merge_state m_state( delta_queue.back() );
      auto itr = m_state.begin();

      BOOST_REQUIRE( itr != m_state.end() );
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      ++itr;
      BOOST_REQUIRE( itr == m_state.end() );
      BOOST_CHECK_THROW( *itr, koinos::exception );
      BOOST_CHECK_THROW( ++itr, koinos::exception );
      BOOST_CHECK_THROW( itr.key(), koinos::exception );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
    }

    delta_queue.pop_front();
    delta_queue.pop_front();
    delta_queue.front()->commit();

    {
      merge_state m_state( delta_queue.back() );
      auto itr = m_state.begin();

      BOOST_REQUIRE( itr != m_state.end() );
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      ++itr;
      BOOST_REQUIRE( itr == m_state.end() );
      BOOST_CHECK_THROW( *itr, koinos::exception );
      BOOST_CHECK_THROW( ++itr, koinos::exception );
      BOOST_CHECK_THROW( itr.key(), koinos::exception );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
    }

    while( delta_queue.size() > 1 )
    {
      delta_queue.pop_front();
      delta_queue.front()->commit();

      merge_state m_state( delta_queue.back() );
      auto itr = m_state.begin();

      BOOST_REQUIRE( itr != m_state.end() );
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      ++itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      ++itr;
      BOOST_REQUIRE( itr == m_state.end() );
      BOOST_CHECK_THROW( *itr, koinos::exception );
      BOOST_CHECK_THROW( ++itr, koinos::exception );
      BOOST_CHECK_THROW( itr.key(), koinos::exception );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "charlie" );
      BOOST_CHECK_EQUAL( *itr, "3" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "bob" );
      BOOST_CHECK_EQUAL( *itr, "6" );
      --itr;
      BOOST_CHECK_EQUAL( itr.key(), "alice" );
      BOOST_CHECK_EQUAL( *itr, "4" );
    }
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( reset_test )
{
  try
  {
    BOOST_TEST_MESSAGE( "Creating object on transient state node" );

    auto shared_db_lock = db.get_shared_lock();

    crypto::multihash state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    auto state_1               = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                            state_id,
                                            protocol::block_header(),
                                            shared_db_lock );
    object_space space;
    std::string a_key = "a";
    std::string a_val = "alice";

    chain::database_key db_key;
    *db_key.mutable_space() = space;
    db_key.set_key( a_key );
    auto key_size = util::converter::as< std::string >( db_key ).size();

    BOOST_CHECK_EQUAL( state_1->put_object( space, a_key, &a_val ), a_val.size() + key_size );
    db.finalize_node( state_1->id(), shared_db_lock );

    auto val_ptr = db.get_head( shared_db_lock )->get_object( space, a_key );
    BOOST_REQUIRE( val_ptr );
    BOOST_CHECK_EQUAL( *val_ptr, a_val );

    BOOST_TEST_MESSAGE( "Closing and opening database" );
    shared_db_lock.reset();
    state_1.reset();
    db.close( db.get_unique_lock() );

    BOOST_CHECK_THROW( db.reset( db.get_unique_lock() ), koinos::exception );

    shared_db_lock = db.get_shared_lock();
    BOOST_CHECK_THROW( db.get_node_at_revision( 1, shared_db_lock ), koinos::exception );
    BOOST_CHECK_THROW( db.get_node_at_revision( 1, crypto::hash( crypto::multicodec::sha2_256, 1 ), shared_db_lock ),
                       koinos::exception );
    BOOST_CHECK_THROW( db.get_node( crypto::hash( crypto::multicodec::sha2_256, 1 ), shared_db_lock ),
                       koinos::exception );
    BOOST_CHECK_THROW( db.create_writable_node( crypto::multihash::zero( crypto::multicodec::sha2_256 ),
                                                crypto::hash( crypto::multicodec::sha2_256, 1 ),
                                                protocol::block_header(),
                                                shared_db_lock ),
                       koinos::exception );
    BOOST_CHECK_THROW( db.finalize_node( crypto::hash( crypto::multicodec::sha2_256, 1 ), shared_db_lock ),
                       koinos::exception );
    BOOST_CHECK_THROW( db.discard_node( crypto::hash( crypto::multicodec::sha2_256, 1 ), shared_db_lock ),
                       koinos::exception );
    BOOST_CHECK_THROW( db.get_head( shared_db_lock ), koinos::exception );
    BOOST_CHECK_THROW( db.get_fork_heads( shared_db_lock ), koinos::exception );
    BOOST_CHECK_THROW( db.get_root( shared_db_lock ), koinos::exception );
    shared_db_lock.reset();

    BOOST_CHECK_THROW( db.commit_node( crypto::hash( crypto::multicodec::sha2_256, 1 ), db.get_unique_lock() ),
                       koinos::exception );

    db.open( temp, []( state_db::state_node_ptr root ) {}, &state_db::fifo_comparator, db.get_unique_lock() );

    shared_db_lock = db.get_shared_lock();

    // Object should not exist on persistent database (state node was not committed)
    BOOST_CHECK( !db.get_head( shared_db_lock )->get_object( space, a_key ) );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == crypto::multihash::zero( crypto::multicodec::sha2_256 ) );
    BOOST_CHECK( db.get_head( shared_db_lock )->revision() == 0 );

    BOOST_TEST_MESSAGE( "Creating object on committed state node" );

    state_1 = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                       state_id,
                                       protocol::block_header(),
                                       shared_db_lock );
    BOOST_CHECK_EQUAL( state_1->put_object( space, a_key, &a_val ), a_val.size() + key_size );
    db.finalize_node( state_1->id(), shared_db_lock );
    auto state_1_id = state_1->id();
    state_1.reset();
    shared_db_lock.reset();
    db.commit_node( state_1_id, db.get_unique_lock() );

    shared_db_lock = db.get_shared_lock();
    val_ptr        = db.get_head( shared_db_lock )->get_object( space, a_key );
    BOOST_REQUIRE( val_ptr );
    BOOST_CHECK_EQUAL( *val_ptr, a_val );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == crypto::hash( crypto::multicodec::sha2_256, 1 ) );

    BOOST_TEST_MESSAGE( "Closing and opening database" );
    shared_db_lock.reset();
    state_1.reset();
    db.close( db.get_unique_lock() );
    db.open( temp, []( state_db::state_node_ptr root ) {}, &state_db::fifo_comparator, db.get_unique_lock() );

    // State node was committed and should exist on open
    shared_db_lock = db.get_shared_lock();
    val_ptr        = db.get_head( shared_db_lock )->get_object( space, a_key );
    BOOST_REQUIRE( val_ptr );
    BOOST_CHECK_EQUAL( *val_ptr, a_val );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == crypto::hash( crypto::multicodec::sha2_256, 1 ) );
    BOOST_CHECK( db.get_head( shared_db_lock )->revision() == 1 );

    BOOST_TEST_MESSAGE( "Resetting database" );
    shared_db_lock.reset();
    db.reset( db.get_unique_lock() );

    // Object should not exist on reset db
    shared_db_lock = db.get_shared_lock();
    BOOST_CHECK( !db.get_head( shared_db_lock )->get_object( space, a_key ) );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == crypto::multihash::zero( crypto::multicodec::sha2_256 ) );
    BOOST_CHECK( db.get_head( shared_db_lock )->revision() == 0 );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( anonymous_node_test )
{
  try
  {
    BOOST_TEST_MESSAGE( "Creating object" );
    object_space space;

    auto shared_db_lock = db.get_shared_lock();

    crypto::multihash state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    auto state_1               = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                            state_id,
                                            protocol::block_header(),
                                            shared_db_lock );
    std::string a_key          = "a";
    std::string a_val          = "alice";

    chain::database_key db_key;
    *db_key.mutable_space() = space;
    db_key.set_key( a_key );
    auto key_size = util::converter::as< std::string >( db_key ).size();

    BOOST_CHECK( state_1->put_object( space, a_key, &a_val ) == a_val.size() + key_size );

    auto ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, a_val );

    {
      BOOST_TEST_MESSAGE( "Creating anonymous state node" );
      auto anon_state = state_1->create_anonymous_node();

      BOOST_REQUIRE( anon_state->id() == state_1->id() );
      BOOST_REQUIRE( anon_state->revision() == state_1->revision() );
      BOOST_REQUIRE( anon_state->parent_id() == state_1->parent_id() );

      BOOST_TEST_MESSAGE( "Modifying object" );
      a_val = "alicia";

      BOOST_CHECK( anon_state->put_object( space, a_key, &a_val ) == 1 );

      ptr = anon_state->get_object( space, a_key );
      BOOST_REQUIRE( ptr );
      BOOST_CHECK_EQUAL( *ptr, a_val );

      ptr = state_1->get_object( space, a_key );
      BOOST_REQUIRE( ptr );
      BOOST_CHECK_EQUAL( *ptr, "alice" );

      BOOST_TEST_MESSAGE( "Deleting anonymous node" );
    }

    {
      BOOST_TEST_MESSAGE( "Creating anonymous state node" );
      auto anon_state = state_1->create_anonymous_node();

      BOOST_TEST_MESSAGE( "Modifying object" );

      BOOST_CHECK( anon_state->put_object( space, a_key, &a_val ) == 1 );

      ptr = anon_state->get_object( space, a_key );
      BOOST_REQUIRE( ptr );
      BOOST_CHECK_EQUAL( *ptr, a_val );

      ptr = state_1->get_object( space, a_key );
      BOOST_REQUIRE( ptr );
      BOOST_CHECK_EQUAL( *ptr, "alice" );

      BOOST_TEST_MESSAGE( "Committing anonymous node" );
      anon_state->commit();

      ptr = state_1->get_object( space, a_key );
      BOOST_REQUIRE( ptr );
      BOOST_CHECK_EQUAL( *ptr, a_val );
    }

    ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, a_val );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( merkle_root_test )
{
  try
  {
    auto shared_db_lock = db.get_shared_lock();

    auto state_1_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    auto state_1    = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                            state_1_id,
                                            protocol::block_header(),
                                            shared_db_lock );

    object_space space;
    std::string a_key = "a";
    std::string a_val = "alice";
    std::string b_key = "b";
    std::string b_val = "bob";
    std::string c_key = "c";
    std::string c_val = "charlie";

    state_1->put_object( space, c_key, &c_val );
    state_1->put_object( space, b_key, &b_val );
    state_1->put_object( space, a_key, &a_val );

    chain::database_key a_db_key;
    *a_db_key.mutable_space() = space;
    a_db_key.set_key( a_key );

    chain::database_key b_db_key;
    *b_db_key.mutable_space() = space;
    b_db_key.set_key( b_key );

    chain::database_key c_db_key;
    *c_db_key.mutable_space() = space;
    c_db_key.set_key( c_key );

    std::vector< std::string > merkle_leafs;
    merkle_leafs.emplace_back( koinos::util::converter::as< std::string >( a_db_key ) );
    merkle_leafs.push_back( a_val );
    merkle_leafs.emplace_back( koinos::util::converter::as< std::string >( b_db_key ) );
    merkle_leafs.push_back( b_val );
    merkle_leafs.emplace_back( koinos::util::converter::as< std::string >( c_db_key ) );
    merkle_leafs.push_back( c_val );

    BOOST_CHECK_THROW( state_1->merkle_root(), koinos::exception );
    db.finalize_node( state_1_id, shared_db_lock );

    auto merkle_root =
      koinos::crypto::merkle_tree< std::string >( koinos::crypto::multicodec::sha2_256, merkle_leafs ).root()->hash();
    BOOST_CHECK_EQUAL( merkle_root, state_1->merkle_root() );

    auto state_2_id = crypto::hash( crypto::multicodec::sha2_256, 2 );
    auto state_2    = db.create_writable_node( state_1_id, state_2_id, protocol::block_header(), shared_db_lock );

    std::string d_key = "d";
    std::string d_val = "dave";
    a_val             = "alicia";

    state_2->put_object( space, a_key, &a_val );
    state_2->put_object( space, d_key, &d_val );
    state_2->remove_object( space, b_key );

    chain::database_key d_db_key;
    *d_db_key.mutable_space() = space;
    d_db_key.set_key( d_key );

    merkle_leafs.clear();
    merkle_leafs.emplace_back( koinos::util::converter::as< std::string >( a_db_key ) );
    merkle_leafs.push_back( a_val );
    merkle_leafs.emplace_back( koinos::util::converter::as< std::string >( b_db_key ) );
    merkle_leafs.push_back( "" );
    merkle_leafs.emplace_back( koinos::util::converter::as< std::string >( d_db_key ) );
    merkle_leafs.push_back( d_val );

    db.finalize_node( state_2_id, shared_db_lock );
    merkle_root =
      koinos::crypto::merkle_tree< std::string >( koinos::crypto::multicodec::sha2_256, merkle_leafs ).root()->hash();
    BOOST_CHECK_EQUAL( merkle_root, state_2->merkle_root() );

    shared_db_lock.reset();
    state_1.reset();
    state_2.reset();
    db.commit_node( state_2_id, db.get_unique_lock() );
    state_2 = db.get_node( state_2_id, db.get_shared_lock() );
    BOOST_CHECK_EQUAL( merkle_root, state_2->merkle_root() );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( get_delta_entries_test )
{
  try
  {
    auto shared_db_lock = db.get_shared_lock();

    auto state_1_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    auto state_1    = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                            state_1_id,
                                            protocol::block_header(),
                                            shared_db_lock );

    object_space space;
    std::string a_key = "a";
    std::string a_val = "alice";
    std::string b_key = "b";
    std::string b_val = "bob";
    std::string c_key = "c";
    std::string c_val = "charlie";

    state_1->put_object( space, c_key, &c_val );
    state_1->put_object( space, b_key, &b_val );
    state_1->put_object( space, a_key, &a_val );

    chain::database_key a_db_key;
    *a_db_key.mutable_space() = space;
    a_db_key.set_key( a_key );

    chain::database_key b_db_key;
    *b_db_key.mutable_space() = space;
    b_db_key.set_key( b_key );

    chain::database_key c_db_key;
    *c_db_key.mutable_space() = space;
    c_db_key.set_key( c_key );

    auto entries = state_1->get_delta_entries();

    BOOST_CHECK_EQUAL( 3, entries.size() );

    BOOST_CHECK_EQUAL( a_key, entries[ 0 ].key() );
    BOOST_CHECK_EQUAL( space.DebugString(), entries[ 0 ].object_space().DebugString() );
    BOOST_CHECK_EQUAL( a_val, entries[ 0 ].value() );

    BOOST_CHECK_EQUAL( b_key, entries[ 1 ].key() );
    BOOST_CHECK_EQUAL( space.DebugString(), entries[ 1 ].object_space().DebugString() );
    BOOST_CHECK_EQUAL( b_val, entries[ 1 ].value() );

    BOOST_CHECK_EQUAL( c_key, entries[ 2 ].key() );
    BOOST_CHECK_EQUAL( space.DebugString(), entries[ 2 ].object_space().DebugString() );
    BOOST_CHECK_EQUAL( c_val, entries[ 2 ].value() );

    db.finalize_node( state_1_id, shared_db_lock );

    auto state_2_id = crypto::hash( crypto::multicodec::sha2_256, 2 );
    auto state_2    = db.create_writable_node( state_1_id, state_2_id, protocol::block_header(), shared_db_lock );

    std::string d_key = "d";
    std::string d_val = "dave";
    a_val             = "alicia";

    state_2->put_object( space, a_key, &a_val );
    state_2->put_object( space, d_key, &d_val );
    state_2->remove_object( space, b_key );

    chain::database_key d_db_key;
    *d_db_key.mutable_space() = space;
    d_db_key.set_key( d_key );

    auto entries2 = state_2->get_delta_entries();
    BOOST_CHECK_EQUAL( 3, entries.size() );

    BOOST_CHECK_EQUAL( a_key, entries2[ 0 ].key() );
    BOOST_CHECK_EQUAL( space.DebugString(), entries2[ 0 ].object_space().DebugString() );
    BOOST_CHECK_EQUAL( a_val, entries2[ 0 ].value() );

    BOOST_CHECK_EQUAL( b_key, entries2[ 1 ].key() );
    BOOST_CHECK_EQUAL( space.DebugString(), entries2[ 1 ].object_space().DebugString() );
    BOOST_CHECK_EQUAL( false, entries2[ 1 ].has_value() ); // Deleted value

    BOOST_CHECK_EQUAL( d_key, entries2[ 2 ].key() );
    BOOST_CHECK_EQUAL( space.DebugString(), entries2[ 2 ].object_space().DebugString() );
    BOOST_CHECK_EQUAL( d_val, entries2[ 2 ].value() );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( rocksdb_backend_test )
{
  try
  {
    koinos::state_db::backends::rocksdb::rocksdb_backend backend;
    auto temp = std::filesystem::temp_directory_path() / util::random_alphanumeric( 8 );

    BOOST_REQUIRE_THROW( backend.open( temp ), koinos::exception );

    BOOST_CHECK_THROW( backend.begin(), koinos::exception );
    BOOST_CHECK_THROW( backend.end(), koinos::exception );
    BOOST_CHECK_THROW( backend.put( "foo", "bar" ), koinos::exception );
    BOOST_CHECK_THROW( backend.get( "foo" ), koinos::exception );
    BOOST_CHECK_THROW( backend.erase( "foo" ), koinos::exception );
    BOOST_CHECK_THROW( backend.clear(), koinos::exception );
    BOOST_CHECK_THROW( backend.size(), koinos::exception );
    BOOST_CHECK_THROW( backend.empty(), koinos::exception );
    BOOST_CHECK_THROW( backend.find( "foo" ), koinos::exception );
    BOOST_CHECK_THROW( backend.lower_bound( "foo" ), koinos::exception );
    BOOST_CHECK_THROW( backend.flush(), koinos::exception );
    BOOST_CHECK( backend.revision() == 0 );
    BOOST_CHECK( backend.id() == koinos::crypto::multihash::zero( koinos::crypto::multicodec::sha2_256 ) );

    std::filesystem::create_directory( temp );
    backend.open( temp );

    auto itr = backend.begin();
    BOOST_CHECK( itr == backend.end() );

    backend.put( "foo", "bar" );
    itr = backend.begin();
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bar" );

    backend.put( "alice", "bob" );

    itr = backend.begin();
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bob" );

    ++itr;
    BOOST_CHECK( *itr == "bar" );

    ++itr;
    BOOST_CHECK( itr == backend.end() );

    --itr;
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bar" );

    itr = backend.lower_bound( "charlie" );
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bar" );

    itr = backend.lower_bound( "foo" );
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bar" );

    backend.put( "foo", "blob" );
    itr = backend.find( "foo" );
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "blob" );

    --itr;
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bob" );

    backend.erase( "foo" );

    itr = backend.begin();
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bob" );

    itr = backend.find( "foo" );
    BOOST_CHECK( itr == backend.end() );

    backend.erase( "foo" );

    backend.erase( "alice" );
    itr = backend.end();
    BOOST_CHECK( itr == backend.end() );

    std::filesystem::remove_all( temp );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( rocksdb_object_cache_test )
{
  try
  {
    std::size_t cache_size = 1'024;
    koinos::state_db::backends::rocksdb::object_cache cache( cache_size );
    using value_type = koinos::state_db::backends::rocksdb::object_cache::value_type;

    std::string a_key = "a";
    std::string a_val = "alice";
    auto a_ptr        = std::make_shared< const value_type >( a_val );

    {
      auto [ cache_hit, val ] = cache.get( a_key );
      BOOST_CHECK( !cache_hit );
      BOOST_CHECK( !val );
    }

    BOOST_CHECK( cache.put( a_key, a_ptr ) );

    {
      auto [ cache_hit, val_ptr ] = cache.get( a_key );
      BOOST_CHECK( cache_hit );
      BOOST_REQUIRE( val_ptr );
      BOOST_CHECK_EQUAL( *val_ptr, a_val );
    }

    std::string b_key = "b";
    std::string b_val = "bob";
    auto b_ptr        = std::make_shared< const value_type >( b_val );

    cache.put( b_key, b_ptr );

    {
      auto [ cache_hit, val_ptr ] = cache.get( b_key );
      BOOST_CHECK( cache_hit );
      BOOST_REQUIRE( val_ptr );
      BOOST_CHECK_EQUAL( *val_ptr, b_val );
    }

    // Will put 'a' first in the cache to evict 'b'
    cache.get( a_key );

    std::string fill_key = "f";
    std::string fill_val( cache_size - a_val.size() - b_val.size() + 1, 'f' );
    auto fill_ptr = std::make_shared< const value_type >( fill_val );
    BOOST_CHECK( cache.put( fill_key, fill_ptr ) );

    {
      auto [ cache_hit, val_ptr ] = cache.get( b_key );
      BOOST_CHECK( !cache_hit );
      BOOST_CHECK( !val_ptr );
    }

    {
      auto [ cache_hit, val_ptr ] = cache.get( a_key );
      BOOST_CHECK( cache_hit );
      BOOST_REQUIRE( val_ptr );
      BOOST_CHECK_EQUAL( *val_ptr, a_val );
    }

    BOOST_CHECK( cache.put( fill_key, fill_ptr ) );
    {
      auto [ cache_hit, val_ptr ] = cache.get( b_key );
      BOOST_CHECK( !cache_hit );
      BOOST_CHECK( !val_ptr );
    }

    std::string null_key = "n";
    std::shared_ptr< const value_type > null_ptr;
    BOOST_CHECK( !cache.put( null_key, null_ptr ) );

    {
      auto [ cache_hit, val_ptr ] = cache.get( null_key );
      BOOST_CHECK( cache_hit );
      BOOST_REQUIRE( !val_ptr );
    }
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( map_backend_test )
{
  try
  {
    koinos::state_db::backends::map::map_backend backend;

    auto itr = backend.begin();
    BOOST_CHECK( itr == backend.end() );

    backend.put( "foo", "bar" );
    itr = backend.begin();
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bar" );

    backend.put( "alice", "bob" );

    itr = backend.begin();
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bob" );

    ++itr;
    BOOST_CHECK( *itr == "bar" );

    ++itr;
    BOOST_CHECK( itr == backend.end() );

    --itr;
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bar" );

    itr = backend.lower_bound( "charlie" );
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bar" );

    itr = backend.lower_bound( "foo" );
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bar" );

    backend.put( "foo", "blob" );
    itr = backend.find( "foo" );
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "blob" );

    --itr;
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bob" );

    backend.erase( "foo" );

    itr = backend.begin();
    BOOST_CHECK( itr != backend.end() );
    BOOST_CHECK( *itr == "bob" );

    itr = backend.find( "foo" );
    BOOST_CHECK( itr == backend.end() );

    backend.erase( "foo" );

    backend.erase( "alice" );
    itr = backend.end();
    BOOST_CHECK( itr == backend.end() );

    backend.put( "foo", "bar" );
    BOOST_REQUIRE( backend.get( "foo" ) );
    BOOST_CHECK_EQUAL( *backend.get( "foo" ), "bar" );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( fork_resolution )
{
  try
  {
    /**
     * The final fork graph looks like the following:
     *
     *           / state_1 (100) --- state_4 (110)
     *          /                 \
     * genesis --- state_2 (99)    \ state_5 (110)
     *          \
     *           \ state_3 (101)
     */

    BOOST_TEST_MESSAGE( "Test default FIFO fork resolution" );

    auto shared_db_lock = db.get_shared_lock();
    auto genesis_id     = db.get_head( shared_db_lock )->id();

    protocol::block_header header;
    header.set_timestamp( 100 );

    crypto::multihash state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    auto state_1 = db.create_writable_node( db.get_head( shared_db_lock )->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == genesis_id );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 99 );
    state_id     = crypto::hash( crypto::multicodec::sha2_256, 2 );
    auto state_2 = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_2 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 101 );
    state_id     = crypto::hash( crypto::multicodec::sha2_256, 3 );
    auto state_3 = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_3 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 110 );
    state_id     = crypto::hash( crypto::multicodec::sha2_256, 4 );
    auto state_4 = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_4 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );

    state_id     = crypto::hash( crypto::multicodec::sha2_256, 5 );
    auto state_5 = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_5 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );

    shared_db_lock.reset();
    state_1.reset();
    state_2.reset();
    state_3.reset();
    state_4.reset();
    state_5.reset();

    BOOST_TEST_MESSAGE( "Test block time fork resolution" );

    db.close( db.get_unique_lock() );
    db.open( temp, [ & ]( state_node_ptr ) {}, &state_db::block_time_comparator, db.get_unique_lock() );
    shared_db_lock = db.get_shared_lock();

    header.set_timestamp( 100 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    state_1  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == genesis_id );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 99 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 2 );
    state_2  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_2 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );

    header.set_timestamp( 101 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 3 );
    state_3  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_3 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );

    header.set_timestamp( 110 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 4 );
    state_4  = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_4 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );

    state_id = crypto::hash( crypto::multicodec::sha2_256, 5 );
    state_5  = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_5 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );

    shared_db_lock.reset();
    state_1.reset();
    state_2.reset();
    state_3.reset();
    state_4.reset();
    state_5.reset();

    BOOST_TEST_MESSAGE( "Test pob fork resolution" );

    db.close( db.get_unique_lock() );
    db.open( temp, [ & ]( state_node_ptr ) {}, &state_db::pob_comparator, db.get_unique_lock() );
    shared_db_lock = db.get_shared_lock();

    std::string signer1 = "signer1";
    std::string signer2 = "signer2";
    std::string signer3 = "signer3";
    std::string signer4 = "signer4";
    std::string signer5 = "signer5";

    // BEGIN: Mimic block time behavior (as long as signers are different)

    header.set_timestamp( 100 );
    header.set_signer( signer1 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    state_1  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == genesis_id );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 99 );
    header.set_signer( signer2 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 2 );
    state_2  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_2 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );

    header.set_timestamp( 101 );
    header.set_signer( signer3 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 3 );
    state_3  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_3 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );

    header.set_timestamp( 110 );
    header.set_signer( signer4 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 4 );
    state_4  = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_4 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );

    header.set_signer( signer5 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 5 );
    state_5  = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_5 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_4->id() );

    // END: Mimic block time behavior (as long as signers are different)

    shared_db_lock.reset();
    state_1.reset();
    state_2.reset();
    state_3.reset();
    state_4.reset();
    state_5.reset();

    db.close( db.get_unique_lock() );
    db.open( temp, [ & ]( state_node_ptr ) {}, &state_db::pob_comparator, db.get_unique_lock() );
    shared_db_lock = db.get_shared_lock();

    // BEGIN: Create two forks, then double produce on the newer fork

    /**
     *                                            / state_3 (height: 2, time: 101, signer: signer3) <-- Double
     * production
     *                                           /
     *           / state_1 (height: 1, time: 100) - state_4 (height: 2, time: 102, signer: signer3) <-- Double
     * production
     *          /
     * genesis --- state_2 (height: 1, time: 99) <-- Resulting head
     *
     *
     */

    header.set_timestamp( 100 );
    header.set_signer( signer1 );
    header.set_height( 1 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    state_1  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == genesis_id );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 99 );
    header.set_signer( signer2 );
    header.set_height( 1 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 2 );
    state_2  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_2 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );

    header.set_timestamp( 101 );
    header.set_signer( signer3 );
    header.set_height( 2 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 3 );
    state_3  = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_3 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_3->id() );

    header.set_timestamp( 102 );
    header.set_signer( signer3 );
    header.set_height( 2 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 4 );
    state_4  = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_4 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_3->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_2->id() );

    /**
     * Fork heads
     *
     *                                            / state_3 (height: 2, time: 101)
     *                                           /
     *           / state_1 (height: 1, time: 100) - state_4 (height: 2, time: 102)
     *          /
     * genesis --- state_2 (height: 1, time: 99)
     *
     *
     */

    auto fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE( fork_heads.size() == 3 );
    auto it = std::find_if( std::begin( fork_heads ),
                            std::end( fork_heads ),
                            [ & ]( state_node_ptr p )
                            {
                              return p->id() == state_2->id();
                            } );
    BOOST_REQUIRE( it != std::end( fork_heads ) );
    it = std::find_if( std::begin( fork_heads ),
                       std::end( fork_heads ),
                       [ & ]( state_node_ptr p )
                       {
                         return p->id() == state_3->id();
                       } );
    BOOST_REQUIRE( it != std::end( fork_heads ) );
    it = std::find_if( std::begin( fork_heads ),
                       std::end( fork_heads ),
                       [ & ]( state_node_ptr p )
                       {
                         return p->id() == state_4->id();
                       } );
    BOOST_REQUIRE( it != std::end( fork_heads ) );
    fork_heads.clear();

    // END: Create two forks, then double produce on the newer fork

    shared_db_lock.reset();
    state_1.reset();
    state_2.reset();
    state_3.reset();
    state_4.reset();
    state_5.reset();

    db.close( db.get_unique_lock() );
    db.open( temp, [ & ]( state_node_ptr ) {}, &state_db::pob_comparator, db.get_unique_lock() );
    shared_db_lock = db.get_shared_lock();

    // BEGIN: Create two forks, then double produce on the older fork

    /**
     *                 Resulting head              / state_3 (height: 2, time: 101, signer: signer3) <-- Double
     * production V                    / / state_1 (height: 1, time: 99) --- state_4 (height: 2, time: 102, signer:
     * signer3) <-- Double production
     *          /
     * genesis --- state_2 (height: 1, time: 100)
     *
     *
     */

    header.set_timestamp( 99 );
    header.set_signer( signer1 );
    header.set_height( 1 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    state_1  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == genesis_id );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 100 );
    header.set_signer( signer2 );
    header.set_height( 1 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 2 );
    state_2  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_2 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 101 );
    header.set_signer( signer3 );
    header.set_height( 2 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 3 );
    state_3  = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_3 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_3->id() );

    header.set_timestamp( 102 );
    header.set_signer( signer3 );
    header.set_height( 2 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 4 );
    state_4  = db.create_writable_node( state_1->id(), state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_4 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_3->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    /**
     * Fork heads
     *
     *           / state_1 (height: 1, time: 99)
     *          /
     * genesis --- state_2 (height: 1, time: 100)
     *
     *
     */

    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE( fork_heads.size() == 2 );
    it = std::find_if( std::begin( fork_heads ),
                       std::end( fork_heads ),
                       [ & ]( state_node_ptr p )
                       {
                         return p->id() == state_1->id();
                       } );
    BOOST_REQUIRE( it != std::end( fork_heads ) );
    it = std::find_if( std::begin( fork_heads ),
                       std::end( fork_heads ),
                       [ & ]( state_node_ptr p )
                       {
                         return p->id() == state_2->id();
                       } );
    BOOST_REQUIRE( it != std::end( fork_heads ) );
    fork_heads.clear();

    // END: Create two forks, then double produce on the older fork

    shared_db_lock.reset();
    state_1.reset();
    state_2.reset();
    state_3.reset();
    state_4.reset();
    state_5.reset();

    db.close( db.get_unique_lock() );
    db.open( temp, [ & ]( state_node_ptr ) {}, &state_db::pob_comparator, db.get_unique_lock() );
    shared_db_lock = db.get_shared_lock();

    // BEGIN: Edge case when double production is the first block

    /**
     *
     *
     *           / state_1 (height: 1, time: 99, signer: signer1)  <--- Double production
     *          /
     * genesis --- state_2 (height: 1, time: 100, signer: signer1) <--- Double production
     *
     *
     */

    header.set_timestamp( 99 );
    header.set_signer( signer1 );
    header.set_height( 1 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    state_1  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == genesis_id );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );

    header.set_timestamp( 100 );
    header.set_signer( signer1 );
    header.set_height( 1 );
    state_id = crypto::hash( crypto::multicodec::sha2_256, 2 );
    state_2  = db.create_writable_node( genesis_id, state_id, header, shared_db_lock );
    BOOST_REQUIRE( state_2 );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == state_1->id() );
    db.finalize_node( state_id, shared_db_lock );
    BOOST_CHECK( db.get_head( shared_db_lock )->id() == genesis_id );

    /**
     * Fork heads
     *
     * genesis
     *
     */

    fork_heads = db.get_fork_heads( shared_db_lock );
    BOOST_REQUIRE( fork_heads.size() == 1 );
    it = std::find_if( std::begin( fork_heads ),
                       std::end( fork_heads ),
                       [ & ]( state_node_ptr p )
                       {
                         return p->id() == genesis_id;
                       } );
    BOOST_REQUIRE( it != std::end( fork_heads ) );
    fork_heads.clear();

    // END: Edge case when double production is the first block
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( restart_cache )
{
  try
  {
    auto shared_db_lock        = db.get_shared_lock();
    crypto::multihash state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    auto state_1               = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                            state_id,
                                            protocol::block_header(),
                                            shared_db_lock );
    BOOST_REQUIRE( state_1 );

    object_space space;
    std::string a_key = "a";
    std::string a_val = "alice";

    chain::database_key db_key;
    *db_key.mutable_space() = space;
    db_key.set_key( a_key );

    state_1->put_object( space, a_key, &a_val );

    {
      auto [ ptr, key ] = state_1->get_next_object( space, std::string() );

      BOOST_REQUIRE( ptr );
      BOOST_CHECK_EQUAL( *ptr, a_val );
      BOOST_CHECK_EQUAL( key, a_key );
    }

    db.finalize_node( state_id, shared_db_lock );
    state_1.reset();
    shared_db_lock.reset();

    db.commit_node( state_id, db.get_unique_lock() );

    db.close( db.get_unique_lock() );
    db.open( temp, [ & ]( state_db::state_node_ptr root ) {}, &state_db::fifo_comparator, db.get_unique_lock() );
    shared_db_lock = db.get_shared_lock();

    state_1 = db.get_root( shared_db_lock );
    {
      auto [ ptr, key ] = state_1->get_next_object( space, std::string() );

      BOOST_REQUIRE( ptr );
      BOOST_CHECK_EQUAL( *ptr, a_val );
      BOOST_CHECK_EQUAL( key, a_key );
    }
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( persistence )
{
  try
  {
    BOOST_TEST_MESSAGE( "Checking persistence when backed by rocksdb" );
    object_space space;
    std::string a_key = "a";
    std::string a_val = "alice";

    auto shared_db_lock = db.get_shared_lock();

    chain::database_key db_key;
    *db_key.mutable_space() = space;
    db_key.set_key( a_key );
    auto key_size = util::converter::as< std::string >( db_key ).size();

    crypto::multihash state_id = crypto::hash( crypto::multicodec::sha2_256, 1 );
    auto state_1               = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                            state_id,
                                            protocol::block_header(),
                                            shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK_EQUAL( state_1->put_object( space, a_key, &a_val ), a_val.size() + key_size );

    db.finalize_node( state_id, shared_db_lock );

    auto ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, a_val );

    state_1.reset();
    shared_db_lock.reset();
    db.commit_node( state_id, db.get_unique_lock() );

    db.close( db.get_unique_lock() );
    db.open( temp, [ & ]( state_db::state_node_ptr root ) {}, &state_db::fifo_comparator, db.get_unique_lock() );

    shared_db_lock = db.get_shared_lock();
    state_1        = db.get_node( state_id, shared_db_lock );
    BOOST_REQUIRE( state_1 );

    ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, a_val );

    state_1.reset();
    shared_db_lock.reset();
    db.close( db.get_unique_lock() );

    BOOST_TEST_MESSAGE( "Checking transience when backed by std::map" );
    db.open( {}, [ & ]( state_db::state_node_ptr root ) {}, &state_db::fifo_comparator, db.get_unique_lock() );

    shared_db_lock = db.get_shared_lock();
    state_1        = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                       state_id,
                                       protocol::block_header(),
                                       shared_db_lock );
    BOOST_REQUIRE( state_1 );
    BOOST_CHECK_EQUAL( state_1->put_object( space, a_key, &a_val ), a_val.size() + key_size );

    db.finalize_node( state_id, shared_db_lock );
    ptr = state_1->get_object( space, a_key );
    BOOST_REQUIRE( ptr );
    BOOST_CHECK_EQUAL( *ptr, a_val );

    state_1.reset();
    shared_db_lock.reset();
    db.commit_node( state_id, db.get_unique_lock() );

    db.close( db.get_unique_lock() );
    db.open( {}, [ & ]( state_db::state_node_ptr root ) {}, &state_db::fifo_comparator, db.get_unique_lock() );

    shared_db_lock = db.get_shared_lock();
    state_1        = db.get_node( state_id, shared_db_lock );
    BOOST_REQUIRE( !state_1 );

    ptr = db.get_head( shared_db_lock )->get_object( space, a_key );
    BOOST_REQUIRE( !ptr );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( clone_node )
{
  try
  {
    BOOST_TEST_MESSAGE( "Check clone of un-finalized node" );

    object_space space;
    std::string a_key = "a";
    std::string a_val = "alice";
    std::string b_key = "bob";
    std::string b_val = "bob";
    std::string c_key = "charlie";
    std::string c_val = "charlie";
    std::string d_key = "dave";
    std::string d_val = "dave";

    auto shared_db_lock = db.get_shared_lock();

    chain::database_key db_key;
    *db_key.mutable_space() = space;
    db_key.set_key( a_key );

    crypto::multihash state_1a_id = crypto::hash( crypto::multicodec::sha2_256, 0x1a );
    auto state_1a                 = db.create_writable_node( db.get_head( shared_db_lock )->id(),
                                             state_1a_id,
                                             protocol::block_header(),
                                             shared_db_lock );
    BOOST_REQUIRE( state_1a );
    state_1a->put_object( space, a_key, &a_val );
    state_1a->put_object( space, b_key, &b_val );
    db.finalize_node( state_1a_id, shared_db_lock );

    crypto::multihash state_2a_id = crypto::hash( crypto::multicodec::sha2_256, 0x2a );
    auto state_2a = db.create_writable_node( state_1a_id, state_2a_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_2a );
    state_2a->put_object( space, c_key, &c_val );
    state_2a->remove_object( space, a_key );

    crypto::multihash state_2b_id = crypto::hash( crypto::multicodec::sha2_256, 0x2b );
    auto state_2b                 = db.clone_node( state_2a_id, state_2b_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_2b );
    BOOST_CHECK( !state_2b->is_finalized() );
    BOOST_CHECK( !state_2b->get_object( space, a_key ) );
    BOOST_REQUIRE( state_2b->get_object( space, b_key ) );
    BOOST_CHECK_EQUAL( *state_2b->get_object( space, b_key ), b_val );
    BOOST_REQUIRE( state_2b->get_object( space, c_key ) );
    BOOST_CHECK_EQUAL( *state_2b->get_object( space, c_key ), c_val );

    state_2b->remove_object( space, b_key );
    state_2b->put_object( space, d_key, &d_val );

    BOOST_REQUIRE( state_2a->get_object( space, b_key ) );
    BOOST_CHECK_EQUAL( *state_2a->get_object( space, b_key ), b_val );
    BOOST_CHECK( !state_2a->get_object( space, d_key ) );

    BOOST_TEST_MESSAGE( "Checking clone of a finalized node" );

    crypto::multihash state_1b_id = crypto::hash( crypto::multicodec::sha2_256, 0x1b );
    BOOST_REQUIRE_THROW( db.clone_node( state_1a_id, state_1b_id, protocol::block_header(), shared_db_lock ),
                         illegal_argument );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( get_all_nodes )
{
  try
  {
    BOOST_TEST_MESSAGE( "Create state nodes" );

    auto shared_db_lock = db.get_shared_lock();
    auto root_id        = db.get_root( shared_db_lock )->id();

    crypto::multihash state_1a_id = crypto::hash( crypto::multicodec::sha2_256, 0x1a );
    auto state_1a = db.create_writable_node( root_id, state_1a_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_1a );
    db.finalize_node( state_1a_id, shared_db_lock );

    crypto::multihash state_1b_id = crypto::hash( crypto::multicodec::sha2_256, 0x1b );
    auto state_1b = db.create_writable_node( root_id, state_1b_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_1b );

    crypto::multihash state_2a_id = crypto::hash( crypto::multicodec::sha2_256, 0x2a );
    auto state_2a = db.create_writable_node( state_1a_id, state_2a_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_2a );

    crypto::multihash state_2b_id = crypto::hash( crypto::multicodec::sha2_256, 0x2b );
    auto state_2b = db.create_writable_node( state_1a_id, state_2b_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_2b );

    BOOST_TEST_MESSAGE( "Check all state nodes" );

    auto nodes = db.get_all_nodes( shared_db_lock );
    BOOST_REQUIRE_EQUAL( nodes.size(), 5 );
    BOOST_CHECK( nodes[ 0 ]->id() == root_id );
    BOOST_CHECK( nodes[ 1 ]->id() == state_1b_id );
    BOOST_CHECK( nodes[ 2 ]->id() == state_2a_id );
    BOOST_CHECK( nodes[ 3 ]->id() == state_1a_id );
    BOOST_CHECK( nodes[ 4 ]->id() == state_2b_id );

    BOOST_TEST_MESSAGE( "Commit 1a" );

    nodes.clear();
    state_1a.reset();
    state_1b.reset();
    state_2a.reset();
    state_2b.reset();
    shared_db_lock.reset();

    auto unique_db_lock = db.get_unique_lock();
    db.commit_node( state_1a_id, unique_db_lock );

    BOOST_TEST_MESSAGE( "Check all state nodes" );

    nodes = db.get_all_nodes( unique_db_lock );
    BOOST_REQUIRE_EQUAL( nodes.size(), 3 );
    BOOST_CHECK( nodes[ 0 ]->id() == state_2a_id );
    BOOST_CHECK( nodes[ 1 ]->id() == state_1a_id );
    BOOST_CHECK( nodes[ 2 ]->id() == state_2b_id );
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

template< typename Reference, typename ReferenceValues, typename Test, typename Initializer >
void test_container_iterators( Reference& reference, ReferenceValues& reference_values, Test& test, Initializer&& init )
{
  // Test to end, to begin, and to end
  auto ref_itr  = init( reference );
  auto test_itr = init( test );

  if( ref_itr == reference.end() )
    BOOST_REQUIRE( test_itr == test.end() );
  else
  {
    BOOST_REQUIRE( test_itr != test.end() );
    BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
    BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
  }

  while( ref_itr != reference.end() )
  {
    ++ref_itr;
    ++test_itr;

    if( ref_itr == reference.end() )
      BOOST_CHECK( test_itr == test.end() );
    else
    {
      BOOST_REQUIRE( test_itr != test.end() );
      BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
      BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
    }
  }

  BOOST_CHECK( test_itr == test.end() );

  while( ref_itr != reference.begin() )
  {
    BOOST_REQUIRE( test_itr != test.begin() );

    --ref_itr;
    --test_itr;

    BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
    BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
  }

  BOOST_CHECK( test_itr == test.begin() );

  while( ref_itr != reference.end() )
  {
    ++ref_itr;
    ++test_itr;

    if( ref_itr == reference.end() )
      BOOST_CHECK( test_itr == test.end() );
    else
    {
      BOOST_REQUIRE( test_itr != test.end() );
      BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
      BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
    }
  }

  BOOST_CHECK( test_itr == test.end() );

  // Test to begin, to end, and to begin
  ref_itr  = init( reference );
  test_itr = init( test );

  while( ref_itr != reference.begin() )
  {
    BOOST_REQUIRE( test_itr != test.begin() );

    --ref_itr;
    --test_itr;

    BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
    BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
  }

  BOOST_CHECK( test_itr == test.begin() );

  while( ref_itr != reference.end() )
  {
    ++ref_itr;
    ++test_itr;

    if( ref_itr == reference.end() )
      BOOST_CHECK( test_itr == test.end() );
    else
    {
      BOOST_REQUIRE( test_itr != test.end() );
      BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
      BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
    }
  }

  BOOST_CHECK( test_itr == test.end() );

  while( ref_itr != reference.begin() )
  {
    BOOST_REQUIRE( test_itr != test.begin() );

    --ref_itr;
    --test_itr;

    BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
    BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
  }

  BOOST_CHECK( test_itr == test.begin() );

  // Test zig zag up (2 forward, 1 back)
  ref_itr  = init( reference );
  test_itr = init( test );

  while( ref_itr != reference.end() )
  {
    ++ref_itr;
    ++test_itr;

    if( ref_itr == reference.end() )
    {
      BOOST_CHECK( test_itr == test.end() );
      break;
    }
    else
    {
      BOOST_REQUIRE( test_itr != test.end() );
      BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
      BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
    }

    ++ref_itr;
    ++test_itr;

    if( ref_itr == reference.end() )
      BOOST_CHECK( test_itr == test.end() );
    else
    {
      BOOST_REQUIRE( test_itr != test.end() );
      BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
      BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
    }

    --ref_itr;
    --test_itr;

    BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
    BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
  }

  BOOST_CHECK( test_itr == test.end() );

  // Test zig zag down (2 back, 1 forward)
  ref_itr  = init( reference );
  test_itr = init( test );

  while( ref_itr != reference.begin() )
  {
    BOOST_REQUIRE( test_itr != test.begin() );

    --ref_itr;
    --test_itr;

    BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
    BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );

    if( ref_itr == reference.begin() )
      break;

    BOOST_REQUIRE( test_itr != test.begin() );

    --ref_itr;
    --test_itr;

    BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
    BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );

    ++ref_itr;
    ++test_itr;

    if( ref_itr == reference.end() )
      BOOST_CHECK( test_itr == test.end() );
    else
    {
      BOOST_REQUIRE( test_itr != test.end() );
      BOOST_CHECK_EQUAL( *ref_itr, test_itr.key() );
      BOOST_CHECK_EQUAL( reference_values.at( *ref_itr ), *test_itr );
    }
  }

  BOOST_CHECK( test_itr == test.begin() );
}

BOOST_AUTO_TEST_CASE( complex_merge_iterator )
{
  try
  {
    std::filesystem::path temp = std::filesystem::temp_directory_path() / koinos::util::random_alphanumeric( 8 );
    std::filesystem::create_directory( temp );

    using state_delta_ptr = std::shared_ptr< state_delta >;
    std::deque< state_delta_ptr > delta_queue;
    delta_queue.emplace_back( std::make_shared< state_delta >( temp ) );

    /*
    This is the test case, using the notation in docs/merge_iterator.md

    1: |a|b|c|d|e|f|g|h|i|j|k|l|m|n|o|
    2: |c|d|g|h|j|n|                    RM: b, f
    3: |b|d|e|k|o|                      RM: g, j, m
    4: |a|b|f|j|                        RM: c, o
    5: |b|d|i|                          RM: j

    The ordered keys are:
    a, b, d, e, f, h, i, k, l ,n

    The ordered values are:
    a4, b5, d5, e3, f4, h2, i5, k3, l1, n2
    */

    std::set< std::string > reference = { "a", "b", "d", "e", "f", "h", "i", "k", "l", "n" };
    std::map< std::string, std::string > reference_values{
      {"a", "a4"},
      {"b", "b5"},
      {"d", "d5"},
      {"e", "e3"},
      {"f", "f4"},
      {"h", "h2"},
      {"i", "i5"},
      {"k", "k3"},
      {"l", "l1"},
      {"n", "n2"}
    };

    for( char c = 'a'; c <= 'o'; c++ )
    {
      std::string key( 1, c );
      delta_queue.back()->put( key, key + "1" );
    }

    delta_queue.emplace_back( delta_queue.back()->make_child( delta_queue.back()->id() ) );
    delta_queue.back()->put( "c", "c2" );
    delta_queue.back()->put( "d", "d2" );
    delta_queue.back()->put( "g", "g2" );
    delta_queue.back()->put( "h", "h2" );
    delta_queue.back()->put( "j", "j2" );
    delta_queue.back()->put( "n", "n2" );
    delta_queue.back()->erase( "b" );
    delta_queue.back()->erase( "f" );

    delta_queue.emplace_back( delta_queue.back()->make_child( delta_queue.back()->id() ) );
    delta_queue.back()->put( "b", "b3" );
    delta_queue.back()->put( "d", "d3" );
    delta_queue.back()->put( "e", "e3" );
    delta_queue.back()->put( "k", "k3" );
    delta_queue.back()->put( "o", "o3" );
    delta_queue.back()->erase( "g" );
    delta_queue.back()->erase( "j" );
    delta_queue.back()->erase( "m" );

    delta_queue.emplace_back( delta_queue.back()->make_child( delta_queue.back()->id() ) );
    delta_queue.back()->put( "a", "a4" );
    delta_queue.back()->put( "b", "b4" );
    delta_queue.back()->put( "f", "f4" );
    delta_queue.back()->put( "j", "j4" );
    delta_queue.back()->erase( "c" );
    delta_queue.back()->erase( "o" );

    delta_queue.emplace_back( delta_queue.back()->make_child( delta_queue.back()->id() ) );
    delta_queue.back()->put( "b", "b5" );
    delta_queue.back()->put( "d", "d5" );
    delta_queue.back()->put( "i", "i5" );
    delta_queue.back()->erase( "j" );

    merge_state m_state( delta_queue.back() );

    test_container_iterators( reference,
                              reference_values,
                              m_state,
                              []( auto& m_state )
                              {
                                return m_state.begin();
                              } );

    test_container_iterators( reference,
                              reference_values,
                              m_state,
                              []( auto& m_state )
                              {
                                return m_state.end();
                              } );

    for( char c = 'a'; c <= 'o'; c++ )
    {
      std::string key( 1, c );

      test_container_iterators( reference,
                                reference_values,
                                m_state,
                                [ & ]( auto& m_state )
                                {
                                  return m_state.lower_bound( key );
                                } );
      /*
      test_container_iterators( reference, reference_values, m_state, [&]( merge_state& m_state )
                                                                      {
                                                                        return m_state.upper_bound( key );
                                                                      } );
      */
    }
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( next_and_prev_objects )
{
  try
  {
    BOOST_TEST_MESSAGE( "Create state nodes" );

    auto shared_db_lock = db.get_shared_lock();
    auto root_id        = db.get_root( shared_db_lock )->id();
    object_space space;

    crypto::multihash state_1_id = crypto::hash( crypto::multicodec::sha2_256, 0x01 );
    auto state_1 = db.create_writable_node( root_id, state_1_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_1 );

    std::string a1_val = "a1", b1_val = "b1", c1_val = "c1", d1_val = "d1", e1_val = "e1", f1_val = "f1", g1_val = "g1",
                h1_val = "h1", i1_val = "i1", j1_val = "j1", k1_val = "k1", l1_val = "l1", m1_val = "m1", n1_val = "n1",
                o1_val = "o1";

    // Add buffer objects before the space
    space.set_id( 1 );
    state_1->put_object( space, "a", &a1_val );

    space.set_id( 2 );
    state_1->put_object( space, "a", &a1_val );
    state_1->put_object( space, "b", &b1_val );
    state_1->put_object( space, "c", &c1_val );
    state_1->put_object( space, "d", &d1_val );
    state_1->put_object( space, "e", &e1_val );
    state_1->put_object( space, "f", &f1_val );
    state_1->put_object( space, "g", &g1_val );
    state_1->put_object( space, "h", &h1_val );
    state_1->put_object( space, "i", &i1_val );
    state_1->put_object( space, "j", &j1_val );
    state_1->put_object( space, "k", &k1_val );
    state_1->put_object( space, "l", &l1_val );
    state_1->put_object( space, "m", &m1_val );
    state_1->put_object( space, "n", &n1_val );
    state_1->put_object( space, "o", &o1_val );

    db.finalize_node( state_1_id, shared_db_lock );

    crypto::multihash state_2_id = crypto::hash( crypto::multicodec::sha2_256, 0x02 );
    auto state_2 = db.create_writable_node( state_1_id, state_2_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_2 );

    std::string c2_val = "c2", d2_val = "d2", g2_val = "g2", h2_val = "h2", j2_val = "j2", n2_val = "n2";

    state_2->put_object( space, "c", &c2_val );
    state_2->put_object( space, "d", &d2_val );
    state_2->put_object( space, "g", &g2_val );
    state_2->put_object( space, "h", &h2_val );
    state_2->put_object( space, "j", &j2_val );
    state_2->put_object( space, "n", &n2_val );
    state_2->remove_object( space, "b" );
    state_2->remove_object( space, "f" );

    db.finalize_node( state_2_id, shared_db_lock );

    crypto::multihash state_3_id = crypto::hash( crypto::multicodec::sha2_256, 0x03 );
    auto state_3 = db.create_writable_node( state_2_id, state_3_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_3 );

    std::string b3_val = "b3", d3_val = "d3", e3_val = "e3", k3_val = "k3", o3_val = "o3";

    state_3->put_object( space, "b", &b3_val );
    state_3->put_object( space, "d", &d3_val );
    state_3->put_object( space, "e", &e3_val );
    state_3->put_object( space, "k", &k3_val );
    state_3->put_object( space, "o", &o3_val );
    state_3->remove_object( space, "g" );
    state_3->remove_object( space, "j" );
    state_3->remove_object( space, "m" );

    db.finalize_node( state_3_id, shared_db_lock );

    crypto::multihash state_4_id = crypto::hash( crypto::multicodec::sha2_256, 0x04 );
    auto state_4 = db.create_writable_node( state_3_id, state_4_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_4 );

    std::string a4_val = "a4", b4_val = "b4", f4_val = "f4", j4_val = "j4";

    state_4->put_object( space, "a", &a4_val );
    state_4->put_object( space, "b", &b4_val );
    state_4->put_object( space, "f", &f4_val );
    state_4->put_object( space, "j", &j4_val );
    state_4->remove_object( space, "c" );
    state_4->remove_object( space, "o" );

    db.finalize_node( state_4_id, shared_db_lock );

    crypto::multihash state_5_id = crypto::hash( crypto::multicodec::sha2_256, 0x05 );
    auto state_5 = db.create_writable_node( state_4_id, state_5_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_5 );

    std::string b5_val = "b5", d5_val = "d5", i5_val = "i5";
    state_5->put_object( space, "b", &b5_val );
    state_5->put_object( space, "d", &d5_val );
    state_5->put_object( space, "i", &i5_val );
    state_5->remove_object( space, "j" );

    db.finalize_node( state_5_id, shared_db_lock );

    std::map< std::string, std::string > reference_values{
      {"a", "a4"},
      {"b", "b5"},
      {"d", "d5"},
      {"e", "e3"},
      {"f", "f4"},
      {"h", "h2"},
      {"i", "i5"},
      {"k", "k3"},
      {"l", "l1"},
      {"n", "n2"}
    };

    std::string key = "";

    for( auto itr = reference_values.begin(); itr != reference_values.end(); ++itr )
    {
      auto [ next_value, next_key ] = state_5->get_next_object( space, key );

      BOOST_REQUIRE( next_value );
      BOOST_CHECK_EQUAL( *next_value, itr->second );
      BOOST_CHECK_EQUAL( next_key, itr->first );

      key = itr->first;
    }

    auto [ next_value, next_key ] = state_5->get_next_object( space, "n" );
    BOOST_CHECK( !next_value );
    BOOST_CHECK_EQUAL( next_key, "" );

    key = "z";
    for( auto itr = reference_values.rbegin(); itr != reference_values.rend(); ++itr )
    {
      auto [ prev_value, prev_key ] = state_5->get_prev_object( space, key );

      BOOST_REQUIRE( prev_value );
      BOOST_CHECK_EQUAL( *prev_value, itr->second );
      BOOST_CHECK_EQUAL( prev_key, itr->first );

      key = itr->first;
    }

    {
      auto [ prev_value, prev_key ] = state_5->get_prev_object( space, key );

      BOOST_CHECK( !prev_value );
      BOOST_CHECK_EQUAL( prev_key, "" );
    }

    crypto::multihash state_6_id = crypto::hash( crypto::multicodec::sha2_256, 0x06 );
    auto state_6 = db.create_writable_node( state_5_id, state_6_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_6 );

    state_6->remove_object( space, "a" );
    state_6->remove_object( space, "b" );
    state_6->remove_object( space, "d" );
    state_6->remove_object( space, "e" );
    state_6->remove_object( space, "f" );
    state_6->remove_object( space, "h" );
    state_6->remove_object( space, "i" );
    state_6->remove_object( space, "k" );
    state_6->remove_object( space, "l" );
    state_6->remove_object( space, "n" );

    db.finalize_node( state_6_id, shared_db_lock );

    {
      auto [ prev_value, prev_key ] = state_6->get_prev_object( space, "z" );

      BOOST_CHECK( !prev_value );
      BOOST_CHECK_EQUAL( prev_key, "" );
    }
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_CASE( prev_object_exception )
{
  try
  {
    auto shared_db_lock = db.get_shared_lock();
    auto root_id        = db.get_root( shared_db_lock )->id();
    object_space space;

    crypto::multihash state_1_id = crypto::hash( crypto::multicodec::sha2_256, 0x01 );
    auto state_1 = db.create_writable_node( root_id, state_1_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_1 );

    std::string a_val = "a", b_val = "b";

    space.set_id( 1 );
    state_1->put_object( space, "a", &a_val );

    space.set_id( 2 );
    state_1->put_object( space, "a", &a_val );
    state_1->put_object( space, "b", &b_val );

    db.finalize_node( state_1_id, shared_db_lock );

    {
      auto [ prev_value, prev_key ] = state_1->get_prev_object( space, "z" );

      BOOST_CHECK( prev_value );
      BOOST_CHECK_EQUAL( prev_key, "b" );
    }

    crypto::multihash state_2_id = crypto::hash( crypto::multicodec::sha2_256, 0x02 );
    auto state_2 = db.create_writable_node( state_1_id, state_2_id, protocol::block_header(), shared_db_lock );
    BOOST_REQUIRE( state_2 );

    state_2->remove_object( space, "a" );
    state_2->remove_object( space, "b" );

    db.finalize_node( state_2_id, shared_db_lock );

    {
      auto [ prev_value, prev_key ] = state_2->get_prev_object( space, "z" );

      BOOST_CHECK( !prev_value );
      BOOST_CHECK_EQUAL( prev_key, "" );
    }
  }
  KOINOS_CATCH_LOG_AND_RETHROW( info )
}

BOOST_AUTO_TEST_SUITE_END()
