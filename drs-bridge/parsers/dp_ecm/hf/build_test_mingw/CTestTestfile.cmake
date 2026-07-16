# CMake generated Testfile for 
# Source directory: C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf
# Build directory: C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/build_test_mingw
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[dp_ecm_hf_frames]=] "C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/build_test_mingw/Debug/test_dp_ecm_hf.exe")
  set_tests_properties([=[dp_ecm_hf_frames]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/CMakeLists.txt;35;add_test;C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[dp_ecm_hf_frames]=] "C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/build_test_mingw/Release/test_dp_ecm_hf.exe")
  set_tests_properties([=[dp_ecm_hf_frames]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/CMakeLists.txt;35;add_test;C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[dp_ecm_hf_frames]=] "C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/build_test_mingw/MinSizeRel/test_dp_ecm_hf.exe")
  set_tests_properties([=[dp_ecm_hf_frames]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/CMakeLists.txt;35;add_test;C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[dp_ecm_hf_frames]=] "C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/build_test_mingw/RelWithDebInfo/test_dp_ecm_hf.exe")
  set_tests_properties([=[dp_ecm_hf_frames]=] PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/CMakeLists.txt;35;add_test;C:/Users/Admin/Downloads/ewtss-v2-design-main/drs-bridge/parsers/dp_ecm/hf/CMakeLists.txt;0;")
else()
  add_test([=[dp_ecm_hf_frames]=] NOT_AVAILABLE)
endif()
