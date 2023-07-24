cmake_minimum_required(VERSION 3.0.0)
project(CppThreadPool VERSION 0.1.0 LANGUAGES C CXX)


add_library(thread_pool INTERFACE)
target_compile_features(thread_pool INTERFACE cxx_std_17)
target_include_directories(thread_pool INTERFACE .)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE thread_pool)

install(FILES ThreadPool.hpp DESTINATION include)
install(FILES CppThreadPoolConfig.cmake DESTINATION lib/cmake)