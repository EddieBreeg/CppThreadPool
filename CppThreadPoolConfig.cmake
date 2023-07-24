
if(TARGET CppThreadPool::thread_pool)
    return()
endif(TARGET CppThreadPool::thread_pool)


add_library(CppThreadPool::thread_pool INTERFACE IMPORTED)
target_include_directories(CppThreadPool::thread_pool INTERFACE ../../include)

target_compile_features(CppThreadPool::thread_pool INTERFACE cxx_std_17)

if(NOT DEFINED CppThreadPool_FIND_QUIETLY)
    message("Found CppThreadPool: ${CMAKE_CURRENT_LIST_DIR}")    
endif(NOT DEFINED CppThreadPool_FIND_QUIETLY)
