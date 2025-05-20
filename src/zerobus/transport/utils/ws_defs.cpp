#include "ws_defs.hpp"
#include <array>
#include <random>
#include "sha1.hpp"
#include "base64.hpp"


namespace ws {


WsAcceptStr calculate_ws_accept(std::string_view key) {
    SHA1 sha1;
    sha1.update(key);
    sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    auto digest = sha1.final();
    WsAcceptStr encoded;
    base64.encode(digest.begin(), digest.end(), encoded.begin());
    return encoded;
}

WsKeyStr generate_ws_key() {

    WsKeyStr out;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 61);

    for (int i = 0; i < 21; ++i) {
        int r = dis(gen);
        char c = r < 10?'0'+r:r<36?'A'+r-10:'a'+r-36;
        out[i] = c;
    }
    out[21] = 'A';
    out[22] = '=';
    out[23] = '=';
    return out;
}


}
