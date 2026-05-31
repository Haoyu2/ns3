# SPDX-License-Identifier: BSD-3-Clause

build_lib_example(
  NAME tcp-aqm-config-validate
  SOURCE_FILES tcp-aqm-config-validate.cc
  LIBRARIES_TO_LINK ${libtcp-aqm-config}
)

build_lib_example(
  NAME tcp-aqm-benchmark
  SOURCE_FILES tcp-aqm-benchmark.cc
  LIBRARIES_TO_LINK
    ${libtcp-aqm-config}
    ${libpoint-to-point}
    ${libinternet}
    ${libapplications}
    ${libtraffic-control}
    ${libnetwork}
    ${libinternet-apps}
)
