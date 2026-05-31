# SPDX-License-Identifier: BSD-3-Clause

# The TCP/AQM benchmark configuration module intentionally keeps its parser
# dependencies in contrib. YAML is the primary human-authored format, JSON is
# accepted because it is valid YAML 1.2 input for yaml-cpp, and fmt keeps the
# validation/normalization output readable without stream-heavy formatting code.
# =============================================================================
# Third-party dependencies
# =============================================================================
#
# 1. yaml-cpp - YAML parser for C++
#    - macOS:   brew install yaml-cpp
#    - Ubuntu:  sudo apt-get install libyaml-cpp-dev
#    - Source:  https://github.com/jbeder/yaml-cpp
#
# 2. fmt - modern C++ formatting library
#    - macOS:   brew install fmt
#    - Ubuntu:  sudo apt-get install libfmt-dev
#    - Source:  https://github.com/fmtlib/fmt
#
# =============================================================================
find_package(yaml-cpp REQUIRED)
find_package(fmt REQUIRED)


build_lib(
  LIBNAME tcp-aqm-config
  SOURCE_FILES
    model/tcp-aqm-experiment-config.cc
    model/tcp-aqm-topology-config.cc
  HEADER_FILES
    model/tcp-aqm-experiment-config.h
    model/tcp-aqm-topology-config.h
  LIBRARIES_TO_LINK ${libcore}
                    ${libnetwork}
                    yaml-cpp::yaml-cpp
                    fmt::fmt
)
