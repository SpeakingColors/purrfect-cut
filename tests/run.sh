#!/bin/sh
# Builds and runs the pure C++ core tests (no JUCE needed): sh tests/run.sh
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test test_core.cpp
/tmp/purrfect_test
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test_more test_more.cpp
/tmp/purrfect_test_more
