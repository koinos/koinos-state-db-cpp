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

hunter_config(gRPC
   VERSION 1.31.0-p0
   CMAKE_ARGS
      CMAKE_POSITION_INDEPENDENT_CODE=ON
      CMAKE_CXX_STANDARD=17
      CMAKE_CXX_STANDARD_REQUIRED=ON
)

hunter_config(abseil
   VERSION ${HUNTER_abseil_VERSION}
   CMAKE_ARGS
      CMAKE_POSITION_INDEPENDENT_CODE=ON
      CMAKE_CXX_STANDARD=17
      CMAKE_CXX_STANDARD_REQUIRED=ON
)

hunter_config(re2
   VERSION ${HUNTER_re2_VERSION}
   CMAKE_ARGS
      CMAKE_POSITION_INDEPENDENT_CODE=ON
      CMAKE_CXX_STANDARD=17
      CMAKE_CXX_STANDARD_REQUIRED=ON
)

hunter_config(c-ares
   VERSION ${HUNTER_c-ares_VERSION}
   CMAKE_ARGS
      CMAKE_POSITION_INDEPENDENT_CODE=ON
      CMAKE_CXX_STANDARD=17
      CMAKE_CXX_STANDARD_REQUIRED=ON
)

hunter_config(ZLIB
   VERSION ${HUNTER_ZLIB_VERSION}
   CMAKE_ARGS
      CMAKE_POSITION_INDEPENDENT_CODE=ON
      CMAKE_CXX_STANDARD=17
      CMAKE_CXX_STANDARD_REQUIRED=ON
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
   URL  "https://github.com/koinos/koinos-util-cpp/archive/191ef0228e6c05a8a678b9de73c0e1d52b769371.tar.gz"
   SHA1 "4d289dd00c20f23be7970f9bc933e79e08b6e933"
   CMAKE_ARGS
      BUILD_TESTS=OFF
)

hunter_config(koinos_log
   URL  "https://github.com/koinos/koinos-log-cpp/archive/84d5707bdd71331203c3bd0a96b1bd0b9c7d0751.tar.gz"
   SHA1 "d67af4848f92c4b19422858424b512de7eacbadc"
   CMAKE_ARGS
      BUILD_TESTS=OFF
)

hunter_config(koinos_exception
   URL  "https://github.com/koinos/koinos-exception-cpp/archive/60100f7d5e2e2c22fb64ed5054b7dc2012234726.tar.gz"
   SHA1 "e5918ca856d8b3b0b4890d6abe7d428f6359906f"
   CMAKE_ARGS
      BUILD_TESTS=OFF
)

hunter_config(koinos_proto
   URL  "https://github.com/koinos/koinos-proto-cpp/archive/db90e99d6f8997861db6083efb8d0805bcd51d48.tar.gz"
   SHA1 "366952245578b37bc9991e0994b7585334de7d95"
)

hunter_config(koinos_crypto
   URL  "https://github.com/koinos/koinos-crypto-cpp/archive/95eb11c0d18561bfa26d3df53ce7d7f5a1d6bf9b.tar.gz"
   SHA1 "9e5653a5b9d0f00855b7a909aa5229d6ac57912f"
)
