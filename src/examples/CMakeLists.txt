

set(exampleFiles ws_server.cpp)

foreach (f ${exampleFiles})
    string(REGEX MATCH "([^\/]+$)" filename ${f})
    string(REGEX MATCH "[^.]*" executable_name ${filename})
    add_executable(${executable_name} ${f})
    target_link_libraries(${executable_name} zerobus ${STANDARD_LIBRARIES} )
endforeach ()
