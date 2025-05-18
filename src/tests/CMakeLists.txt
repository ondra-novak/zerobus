set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)

  find_package(cppzmq)

set(testFiles
            local_bus.cpp
            direct_bridge.cpp
            zmq_bridge.cpp
#            tcp_bridge.cpp
#            pipe_bridge.cpp
#            spawn.cpp
#            websocket.cpp
#            stream.cpp
)



foreach (testFile ${testFiles})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name test_${filename})
    add_executable(${executable_name} ${testFile})
    target_link_libraries(${executable_name} zerobus_zmq zerobus  ${ZeroMQ_LIBRARY} ${STANDARD_LIBRARIES} )
    add_test(NAME ${executable_name} COMMAND ${executable_name})
endforeach ()
