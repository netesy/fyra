set(PARSER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Lexer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Parser.cpp
)

target_sources(fyra PRIVATE ${PARSER_SOURCES})
