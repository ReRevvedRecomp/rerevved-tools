# Writes a C++ source that embeds INPUT as a byte array named SYMBOL.
file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" length)
math(EXPR size "${length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "#include <cstddef>\n\nextern const unsigned char ${SYMBOL}[];\nextern const std::size_t ${SYMBOL}Size;\nalignas(4) const unsigned char ${SYMBOL}[] = {\n${bytes}};\nconst std::size_t ${SYMBOL}Size = ${size};\n")
