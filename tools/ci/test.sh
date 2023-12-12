#!/bin/bash

set -e
set -x

cd $TRAVIS_BUILD_DIR/build/tests
if [ "$RUN_TYPE" = "test" ]; then
   exec ctest -j3 --output-on-failure

   cd $TRAVIS_BUILD_DIR/build
   cmake .. build . --config Release --parallel 3 --target format.check
elif [ "$RUN_TYPE" = "coverage" ]; then
   exec valgrind --error-exitcode=1 --leak-check=yes ./koinos_state_db_tests
fi
