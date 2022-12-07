#pragma once

#include <koinos/crypto/multihash.hpp>
#include <koinos/state_db/backends/backend.hpp>
#include <koinos/state_db/backends/rocksdb/object_cache.hpp>
#include <koinos/state_db/backends/rocksdb/rocksdb_iterator.hpp>

#include <rocksdb/db.h>

#include <filesystem>
#include <string>
#include <utility>

namespace koinos::state_db::backends::rocksdb {

class rocksdb_backend final : public abstract_backend {
   public:
      using key_type   = abstract_backend::key_type;
      using value_type = abstract_backend::value_type;
      using size_type  = abstract_backend::size_type;

      rocksdb_backend();
      ~rocksdb_backend();

      void open( const std::filesystem::path& p );
      void close();
      void flush();

      virtual void start_write_batch() override;
      virtual void end_write_batch() override;

      // Iterators
      virtual iterator begin() override;
      virtual iterator end() override;

      // Modifiers
      virtual void put( const key_type& k, const value_type& v ) override;
      virtual const value_type* get( const key_type& ) const override;
      virtual void erase( const key_type& k ) override;
      virtual void clear() override;

      virtual size_type size() const override;

      // Lookup
      virtual iterator find( const key_type& k ) override;
      virtual iterator lower_bound( const key_type& k ) override;

      virtual void store_metadata() override;

   private:
      void load_metadata();

      using column_handles = std::vector< std::shared_ptr< ::rocksdb::ColumnFamilyHandle > >;

      std::shared_ptr< ::rocksdb::DB >          _db;
      std::optional< ::rocksdb::WriteBatch >    _write_batch;
      column_handles                            _handles;
      ::rocksdb::WriteOptions                   _wopts;
      std::shared_ptr< ::rocksdb::ReadOptions > _ropts;
      mutable std::shared_ptr< object_cache >   _cache;
      size_type                                 _size = 0;
};

} // koinos::state_db::backends::rocksdb
