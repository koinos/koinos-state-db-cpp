#!/bin/bash

set -e
set -x

cd $(dirname "$0")/../build/tests
if [ "$RUN_TYPE" = "test" ]; then
   exec ctest -j3 --output-on-failure && ../libraries/vendor/mira/test/mira_test
elif [ "$RUN_TYPE" = "coverage" ]; then
   exec valgrind --error-exitcode=1 --leak-check=yes ./koinos_tests
fi
