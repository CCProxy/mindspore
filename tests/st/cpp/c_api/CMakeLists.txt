include_directories(${PYTHON_INCLUDE_DIRS})
include_directories(${MS_CCSRC_PATH})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/stub/runtime/)
include_directories(${CMAKE_SOURCE_DIR}/mindspore/ccsrc)
include_directories(${CMAKE_SOURCE_DIR}/mindspore/ccsrc/minddata/dataset)
include_directories(${CMAKE_SOURCE_DIR}/mindspore/core)
include_directories(${CMAKE_BINARY_DIR})
include_directories(${CUDA_INCLUDE_DIRS})

# graphengine include directories
if(ENABLE_D OR ENABLE_TESTCASES)
    include(${CMAKE_SOURCE_DIR}/cmake/graphengine_variables.cmake)
endif()

file(GLOB_RECURSE C_ST_SRC RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *.cc)
add_executable(c_st_tests ${C_ST_SRC})
target_link_libraries(c_st_tests PRIVATE mindspore_c_api mindspore::gtest)
target_link_options(c_st_tests PRIVATE -Wl,--unresolved-symbols=ignore-all)
set_target_properties(c_st_tests PROPERTIES SKIP_BUILD_RPATH TRUE RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
file(COPY ${gtest_LIBPATH}/libgtest${CMAKE_SHARED_LIBRARY_SUFFIX} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR})

# compile custom op so
add_compile_options(-fvisibility=default)
add_library(custom_add SHARED ${CMAKE_CURRENT_SOURCE_DIR}/custom_impl/add_with_attr.cc)
set_target_properties(custom_add PROPERTIES SKIP_BUILD_RPATH TRUE LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
