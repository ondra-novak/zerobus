

set(exampleFiles webserver.cpp)

foreach (f ${exampleFiles})
    string(REGEX MATCH "([^\/]+$)" filename ${f})
    string(REGEX MATCH "[^.]*" executable_name ${filename})
    add_executable(${executable_name} ${f})
    target_link_libraries(${executable_name} zerobus_epoll zerobus ${STANDARD_LIBRARIES} )
endforeach ()
