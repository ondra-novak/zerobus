
set(JS_FILE ${CMAKE_SOURCE_DIR}/src/javascript/client.js)
set(HTML_FILE ${CMAKE_SOURCE_DIR}/src/javascript/index.html)
set(HEADER_FILE ${CMAKE_BINARY_DIR}/includes/embedded_js.h)

# Nastav cesty k souborům
set(HEADER_TEMPLATE embedded_js.h.in)
set(GENERATED_HEADER ${CMAKE_BINARY_DIR}/includes/embedded_js.h)

# Čtení obsahu JS souboru a vložení do šablony
file(READ ${JS_FILE} JS_CONTENT)
string(SUBSTRING "${JS_CONTENT}" 0  16000 JS_CONTENT1)
string(SUBSTRING "${JS_CONTENT}" 16000 -1 JS_CONTENT2)
file(READ ${HTML_FILE} HTML_CONTENT)

# Nahrazení @JS_CONTENT@ ve šabloně skutečným obsahem JS souboru
configure_file(${HEADER_TEMPLATE} ${GENERATED_HEADER} @ONLY)




add_executable(zerobus_server server.cpp)
target_include_directories(zerobus_server PRIVATE ${CMAKE_BINARY_DIR}/includes)
target_link_libraries(zerobus_server zerobus ${STANDARD_LIBRARIES} )
