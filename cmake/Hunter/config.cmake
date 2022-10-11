hunter_config(Boost
   VERSION ${HUNTER_Boost_VERSION}
   CMAKE_ARGS
      USE_CONFIG_FROM_BOOST=ON
      Boost_USE_STATIC_LIBS=ON
      Boost_NO_BOOST_CMAKE=ON
)

hunter_config(Protobuf
   URL  "https://github.com/koinos/protobuf/archive/e1b1477875a8b022903b548eb144f2c7bf4d9561.tar.gz"
   SHA1 "5796707a98eec15ffb3ad86ff50e8eec5fa65e68"
   CMAKE_ARGS
      CMAKE_CXX_FLAGS=-fvisibility=hidden
      CMAKE_C_FLAGS=-fvisibility=hidden
)

hunter_config(rocksdb
   URL "https://github.com/facebook/rocksdb/archive/v6.15.2.tar.gz"
   SHA1 "daf7ef3946fd39c910acaaa57789af8515b39251"
   CMAKE_ARGS
      WITH_TESTS=OFF
      WITH_TOOLS=OFF
      WITH_JNI=OFF
      WITH_BENCHMARK_TOOLS=OFF
      WITH_CORE_TOOLS=OFF
      WITH_GFLAGS=OFF
      PORTABLE=ON
      FAIL_ON_WARNINGS=OFF
      ROCKSDB_BUILD_SHARED=OFF
      CMAKE_CXX_FLAGS=-fvisibility=hidden
      CMAKE_C_FLAGS=-fvisibility=hidden
)

hunter_config(yaml-cpp
   VERSION "0.6.3"
   CMAKE_ARGS
      CMAKE_CXX_FLAGS=-fvisibility=hidden
      CMAKE_C_FLAGS=-fvisibility=hidden
)

hunter_config(libsecp256k1
   URL "https://github.com/soramitsu/soramitsu-libsecp256k1/archive/c7630e1bac638c0f16ee66d4dce7b5c49eecbaa5.tar.gz"
   SHA1 "0534fa8948f279b26fd102905215a56f0ad7fa18"
)

hunter_config(libsecp256k1-vrf
   URL "https://github.com/koinos/secp256k1-vrf/archive/db479e83be5685f652a9bafefaef77246fdf3bbe.tar.gz"
   SHA1 "62df75e061c4afd6f0548f1e8267cc3da6abee15"
)

hunter_config(ethash
   URL "https://github.com/chfast/ethash/archive/refs/tags/v0.8.0.tar.gz"
   SHA1 "41fd440f70b6a8dfc3fd29b20f471dcbd1345ad0"
   CMAKE_ARGS
      CMAKE_CXX_STANDARD=17
      CMAKE_CXX_STANDARD_REQUIRED=ON
)

hunter_config(koinos_util
   URL  "https://github.com/koinos/koinos-util-cpp/archive/cd4fcfeab477eda86edab83b99c47ec959543e6c.tar.gz"
   SHA1 "f660dfad4d57f8b6cd184116024aba0781c99f35"
   CMAKE_ARGS
      BUILD_TESTS=OFF
)

hunter_config(koinos_log
   URL  "https://github.com/koinos/koinos-log-cpp/archive/6820a4a334d7b166d288543f472b52cb5f60645f.tar.gz"
   SHA1 "2ae5ee71829c09f620585a5c16e5498f03cfc69b"
   CMAKE_ARGS
      BUILD_TESTS=OFF
)

hunter_config(koinos_exception
   URL  "https://github.com/koinos/koinos-exception-cpp/archive/c979228d82dc8773f54c22f2c4b9e0f26e65b8ea.tar.gz"
   SHA1 "56aa1cdd8ceee828f00fb4742208932ea6cf3eec"
   CMAKE_ARGS
      BUILD_TESTS=OFF
)

hunter_config(koinos_proto
   URL  "https://github.com/koinos/koinos-proto-cpp/archive/79ded4ac393574528712e9a81b95c249fd74e677.tar.gz"
   SHA1 "fe374bb19baa1cd2cac5280a98e7c08e05f21f30"
)

hunter_config(koinos_crypto
   URL  "https://github.com/koinos/koinos-crypto-cpp/archive/c51f4e2c4e10ec2f57e1c155d549874b0afecbfc.tar.gz"
   SHA1 "9ef00311317fc61df8b3882a70c4a9304c133e39"
)

