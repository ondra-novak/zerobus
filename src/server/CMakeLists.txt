
ADD_CUSTOM_COMMAND(
    OUTPUT ${CMAKE_BINARY_DIR}/includes/embedded_js.h
    COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_BINARY_DIR}/includes/embedded_js.h -P ${CMAKE_SOURCE_DIR}/src/javascript/to_header.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/src/javascript/client.js 
            ${CMAKE_SOURCE_DIR}/src/javascript/index.html
)

SET_SOURCE_FILES_PROPERTIES(${CMAKE_BINARY_DIR}/includes/embedded_js.h PROPERTIES GENERATED TRUE)

add_custom_target(zerobus_embedded_js DEPENDS ${CMAKE_BINARY_DIR}/includes/embedded_js.h)



add_executable(zerobus_server server.cpp)
add_dependencies(zerobus_server zerobus_embedded_js)
target_include_directories(zerobus_server PRIVATE ${CMAKE_BINARY_DIR}/includes)
target_link_libraries(zerobus_server zerobus ${STANDARD_LIBRARIES} )
