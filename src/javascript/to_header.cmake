set(JS_FILE ${CMAKE_CURRENT_LIST_DIR}/client.js)
set(HTML_FILE ${CMAKE_CURRENT_LIST_DIR}/index.html)

set(HEADER_TEMPLATE ${CMAKE_CURRENT_LIST_DIR}/embedded_js.h.in)

file(READ ${JS_FILE} JS_CONTENT)
string(SUBSTRING "${JS_CONTENT}" 0  16000 JS_CONTENT1)
string(SUBSTRING "${JS_CONTENT}" 16000 -1 JS_CONTENT2)
file(READ ${HTML_FILE} HTML_CONTENT)

configure_file(${HEADER_TEMPLATE} ${OUT} @ONLY)
