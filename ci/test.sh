#!/bin/bash

set -e
set -x

cd $(dirname "$0")/../build/tests
if [ "$RUN_TYPE" = "test" ]; then
   exec ctest -j3 --output-on-failure
elif [ "$RUN_TYPE" = "coverage" ]; then
   exec valgrind --error-exitcode=1 --leak-check=yes ./koinos_state_db_tests
fi
