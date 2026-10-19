#!/bin/sh
# Builds and runs the pure C++ core tests (no JUCE needed): sh tests/run.sh
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test test_core.cpp
/tmp/purrfect_test
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test_more test_more.cpp
/tmp/purrfect_test_more
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test_pitch test_pitch.cpp
/tmp/purrfect_test_pitch
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test_track test_track.cpp
/tmp/purrfect_test_track
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test_poly test_poly.cpp
/tmp/purrfect_test_poly
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test_spectral test_spectral.cpp
/tmp/purrfect_test_spectral
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test_notes test_notes.cpp
/tmp/purrfect_test_notes
g++ -std=c++17 -O2 -Wall -Wextra -Wno-misleading-indentation -o /tmp/purrfect_test_session test_session.cpp
/tmp/purrfect_test_session
