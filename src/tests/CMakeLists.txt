set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)

find_package(OpenSSL)



set(testFiles
            local_bus.cpp
            direct_bridge.cpp
#            zmq_bridge.cpp
            ws_bridge.cpp
#            pipe_bridge.cpp
#            spawn.cpp
#            websocket.cpp
#            stream.cpp
)

message(STATUS "OpenSSL include dir: ${OpenSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL libraries: ${OpenSSL_LIBRARIES}")
message(STATUS "OpenSSL libraries: ${OpenSSL_FOUND}")

foreach (testFile ${testFiles})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name test_${filename})
    add_executable(${executable_name} ${testFile})
    if (OpenSSL_FOUND) 
         Set(OpenSSL_LIBS OpenSSL::SSL OpenSSL::Crypto)
         target_compile_definitions(${executable_name} PRIVATE -DWITH_TLS)
         target_include_directories(${executable_name} PRIVATE ${OpenSSL_INCLUDE_DIR})
    endif()
    target_link_libraries(${executable_name} zerobus_epoll zerobus ${OpenSSL_LIBS} ${STANDARD_LIBRARIES} )
    add_test(NAME ${executable_name} COMMAND ${executable_name})
endforeach ()
