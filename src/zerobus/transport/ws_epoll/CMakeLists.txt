
find_package(OpenSSL)



add_library(zerobus_epoll 
        ws_bridge.cpp 
        ../utils/ws_defs.cpp 
        )

if (OpenSSL_FOUND) 
     target_compile_definitions(zerobus_epoll PRIVATE -DWITH_TLS)
endif()

target_include_directories(zerobus_epoll PRIVATE ${OpenSSL_INCLUDE_DIR})
