#!/bin/bash

set -e
set -x

if [ "$RUN_TYPE" = "test" ]; then
   cd $TRAVIS_BUILD_DIR/build
   cmake --build . --config Release --parallel 3 --target format.check

   cd $TRAVIS_BUILD_DIR/build/tests
   exec ctest -j3 --output-on-failure
elif [ "$RUN_TYPE" = "coverage" ]; then
   cd $TRAVIS_BUILD_DIR/build/tests
   exec valgrind --error-exitcode=1 --leak-check=yes ./state_db_tests
fi
