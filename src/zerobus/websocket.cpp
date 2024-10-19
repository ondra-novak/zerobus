#include "websocket.h"
#include <array>
#include <random>


namespace zerobus {

namespace ws {

bool Parser::push_data(std::string_view data) {
    std::size_t sz = data.size();
    std::size_t i = 0;
    bool fin = false;
    while (i < sz && !fin) {
        char c = data[i];
        switch (_state) {
            case State::first_byte:
                _fin = (c & 0x80) != 0;
                _type = c & 0xF;
                _state = State::second_byte;        //first byte follows second byte
                break;
            case State::second_byte:
                _masked = (c & 0x80) != 0;
                c &= 0x7F;
                if (c == 127) {
                    _state = State::payload_len;
                    _state_len = 8;                 //follows 8 bytes of length
                } else if (c == 126) {
                    _state = State::payload_len;
                    _state_len = 2;                 //follows 2 bytes of length
                } else if (_masked){
                    _payload_len = c;
                    _state = State::masking;
                    _state_len = 4;                 //follows 4 bytes of masking
                } else  if (c) {
                    _state_len = c;
                    _state = State::payload;        //follows c bytes of payload
                } else {
                    fin = true;         //empty frame - finalize
                }
                break;
            case State::payload_len:
                //decode payload length
                _payload_len = (_payload_len << 8) + static_cast<unsigned char>(c);
                if (--_state_len == 0) { //read all bytes
                     if (_masked) {
                         _state = State::masking;
                         _state_len = 4;        //follows masking
                     } else if (_payload_len) { //non-empty frame
                         _state_len = _payload_len;
                         _state = State::payload;   //read payload
                     } else {
                         fin = true;            //empty frame, finalize
                     }
                }
                break;
            case State::masking:
                _masking[4-_state_len] = c;     //read masking
                if (--_state_len == 0) {        //all bytes?
                    if (_payload_len) {
                        _state_len = _payload_len;
                        _state = State::payload;    //read payload
                        _mask_cntr = 0;
                    } else {
                        fin = true;             //empty frame finalize
                    }
                }
                break;
            case State::payload:
                _cur_message.push_back(c ^ _masking[_mask_cntr]);   //read payload
                _mask_cntr = (_mask_cntr + 1) & 0x3;
                if (--_state_len == 0) {        //if read all
                    fin = true;                 //finalize
                }
                break;
            case State::complete:           //in this state, nothing is read
                _unused_data = data;        //all data are unused
                return true;                //frame is complete
        };
        ++i;
    }
    if (fin) {
        _unused_data = data.substr(i);
        return finalize();
    }
    return false;
}

void Parser::reset_state() {
    _state = State::first_byte;
    std::fill(std::begin(_masking), std::end(_masking), 0);
    _fin = false;
    _masked = false;
    _payload_len = 0;
    _state_len = 0;
    _unused_data = {};
}

void Parser::reset() {
    reset_state();
    _cur_message.clear();
}

Message Parser::get_message() const {
    if (_final_type == Type::connClose) {
        std::uint16_t code = 0;
        std::string_view message;
        if (_cur_message.size() >= 2) {
            code = static_cast<unsigned char>(_cur_message[0]) * 256 + static_cast<unsigned char>(_cur_message[1]);
        }
        if (_cur_message.size() > 2) {
            message = std::string_view(_cur_message.data()+2, _cur_message.size() - 3);
        }
        return Message {
            message,
            _final_type,
            code,
            _fin
        };
    } else {
        _cur_message.push_back('\0');
        _cur_message.pop_back();
        return Message {
            {_cur_message.data(), _cur_message.size()},
            _final_type,
            _type,
            _fin
        };
    }
}



bool Parser::finalize() {
    _state = State::complete;
    switch (_type) {
        case opcodeContFrame: break;
        case opcodeConnClose: _final_type = Type::connClose; break;
        case opcodeBinaryFrame:  _final_type = Type::binary;break;
        case opcodeTextFrame:  _final_type = Type::text;break;
        case opcodePing:  _final_type = Type::ping;break;
        case opcodePong:  _final_type = Type::pong;break;
        default: _final_type = Type::unknown;break;
    }

    if (!_fin) {
        if (_need_fragmented) {
            auto tmp = _unused_data;
            reset_state();
            return push_data(tmp);
        }
    }
    return true;
}

Builder::Builder(bool client)
    :_client(client) {
    if (_client) {
        std::random_device dev;
        _rnd.seed(dev());
    }
}

bool Builder::build(const Message &msg, std::vector<char> &output) {
    return build_t(msg, [&](char c){output.push_back(c);});
}
bool Builder::build(const Message &msg, std::string &output) {
    return build_t(msg, [&](char c){output.push_back(c);});
}


template<std::invocable<char> Fn>
bool Builder::build_t(const Message &message, Fn &&output) {
    std::string tmp;
    std::string_view payload = message.payload;

    if (message.type == Type::connClose) {
        tmp.push_back(static_cast<char>(message.code>>8));
        tmp.push_back(static_cast<char>(message.code & 0xFF));
        if (!message.payload.empty()) {
            std::copy(message.payload.begin(), message.payload.end(), std::back_inserter(tmp));
        }
        payload = {tmp.c_str(), tmp.length()+1};
    }

    // opcode and FIN bit
    char opcode = opcodeContFrame;
    bool fin = message.fin;
    if (!_fragmented) {
        switch (message.type) {
            default:
            case Type::unknown: return false;
            case Type::text: opcode = opcodeTextFrame;break;
            case Type::binary: opcode = opcodeBinaryFrame;break;
            case Type::ping: opcode = opcodePing;break;
            case Type::pong: opcode = opcodePong;break;
            case Type::connClose: opcode = opcodeConnClose;break;
        }
    }
    _fragmented = !fin;
    output((fin << 7) | opcode);
    // payload length
    std::uint64_t len = payload.size();

    char mm = _client?0x80:0;
    if (len < 126) {
        output(mm| static_cast<char>(len));
    } else if (len < 65536) {
        output(mm | 126);
        output(static_cast<char>((len >> 8) & 0xFF));
        output(static_cast<char>(len & 0xFF));
    } else {
        output(mm | 127);
        output(static_cast<char>((len >> 56) & 0xFF));
        output(static_cast<char>((len >> 48) & 0xFF));
        output(static_cast<char>((len >> 40) & 0xFF));
        output(static_cast<char>((len >> 32) & 0xFF));
        output(static_cast<char>((len >> 24) & 0xFF));
        output(static_cast<char>((len >> 16) & 0xFF));
        output(static_cast<char>((len >> 8) & 0xFF));
        output(static_cast<char>(len & 0xFF));
    }
    char masking_key[4];

    if (_client) {
        std::uniform_int_distribution<> dist(0, 255);

        for (int i = 0; i < 4; ++i) {
            masking_key[i] = static_cast<char>(dist(_rnd));
            output(masking_key[i]);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            masking_key[i] = 0;
        }
    }

    int idx =0;
    for (char c: payload) {
        c ^= masking_key[idx];
        idx = (idx + 1) & 0x3;
        output(c);
    }
    return true;

}


class SHA1
{
public:
    SHA1();
    void update(std::string_view data);
    std::array<unsigned char, 20> final();

private:
    uint32_t digest[5];
    std::string buffer;
    uint64_t transforms;
};


static const size_t BLOCK_INTS = 16;  /* number of 32bit integers per SHA1 block */
static const size_t BLOCK_BYTES = BLOCK_INTS * 4;


inline static void reset(uint32_t digest[], std::string &buffer, uint64_t &transforms)
{
    /* SHA1 initialization constants */
    digest[0] = 0x67452301;
    digest[1] = 0xefcdab89;
    digest[2] = 0x98badcfe;
    digest[3] = 0x10325476;
    digest[4] = 0xc3d2e1f0;

    /* Reset counters */
    buffer = "";
    transforms = 0;
}


inline static uint32_t rol(const uint32_t value, const size_t bits)
{
    return (value << bits) | (value >> (32 - bits));
}


inline static uint32_t blk(const uint32_t block[BLOCK_INTS], const size_t i)
{
    return rol(block[(i+13)&15] ^ block[(i+8)&15] ^ block[(i+2)&15] ^ block[i], 1);
}


/*
 * (R0+R1), R2, R3, R4 are the different operations used in SHA1
 */

inline static void R0(const uint32_t block[BLOCK_INTS], const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i)
{
    z += ((w&(x^y))^y) + block[i] + 0x5a827999 + rol(v, 5);
    w = rol(w, 30);
}


inline static void R1(uint32_t block[BLOCK_INTS], const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i)
{
    block[i] = blk(block, i);
    z += ((w&(x^y))^y) + block[i] + 0x5a827999 + rol(v, 5);
    w = rol(w, 30);
}


inline static void R2(uint32_t block[BLOCK_INTS], const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i)
{
    block[i] = blk(block, i);
    z += (w^x^y) + block[i] + 0x6ed9eba1 + rol(v, 5);
    w = rol(w, 30);
}


inline static void R3(uint32_t block[BLOCK_INTS], const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i)
{
    block[i] = blk(block, i);
    z += (((w|x)&y)|(w&x)) + block[i] + 0x8f1bbcdc + rol(v, 5);
    w = rol(w, 30);
}


inline static void R4(uint32_t block[BLOCK_INTS], const uint32_t v, uint32_t &w, const uint32_t x, const uint32_t y, uint32_t &z, const size_t i)
{
    block[i] = blk(block, i);
    z += (w^x^y) + block[i] + 0xca62c1d6 + rol(v, 5);
    w = rol(w, 30);
}


/*
 * Hash a single 512-bit block. This is the core of the algorithm.
 */

inline static void transform(uint32_t digest[], uint32_t block[BLOCK_INTS], uint64_t &transforms)
{
    /* Copy digest[] to working vars */
    uint32_t a = digest[0];
    uint32_t b = digest[1];
    uint32_t c = digest[2];
    uint32_t d = digest[3];
    uint32_t e = digest[4];

    /* 4 rounds of 20 operations each. Loop unrolled. */
    R0(block, a, b, c, d, e,  0);
    R0(block, e, a, b, c, d,  1);
    R0(block, d, e, a, b, c,  2);
    R0(block, c, d, e, a, b,  3);
    R0(block, b, c, d, e, a,  4);
    R0(block, a, b, c, d, e,  5);
    R0(block, e, a, b, c, d,  6);
    R0(block, d, e, a, b, c,  7);
    R0(block, c, d, e, a, b,  8);
    R0(block, b, c, d, e, a,  9);
    R0(block, a, b, c, d, e, 10);
    R0(block, e, a, b, c, d, 11);
    R0(block, d, e, a, b, c, 12);
    R0(block, c, d, e, a, b, 13);
    R0(block, b, c, d, e, a, 14);
    R0(block, a, b, c, d, e, 15);
    R1(block, e, a, b, c, d,  0);
    R1(block, d, e, a, b, c,  1);
    R1(block, c, d, e, a, b,  2);
    R1(block, b, c, d, e, a,  3);
    R2(block, a, b, c, d, e,  4);
    R2(block, e, a, b, c, d,  5);
    R2(block, d, e, a, b, c,  6);
    R2(block, c, d, e, a, b,  7);
    R2(block, b, c, d, e, a,  8);
    R2(block, a, b, c, d, e,  9);
    R2(block, e, a, b, c, d, 10);
    R2(block, d, e, a, b, c, 11);
    R2(block, c, d, e, a, b, 12);
    R2(block, b, c, d, e, a, 13);
    R2(block, a, b, c, d, e, 14);
    R2(block, e, a, b, c, d, 15);
    R2(block, d, e, a, b, c,  0);
    R2(block, c, d, e, a, b,  1);
    R2(block, b, c, d, e, a,  2);
    R2(block, a, b, c, d, e,  3);
    R2(block, e, a, b, c, d,  4);
    R2(block, d, e, a, b, c,  5);
    R2(block, c, d, e, a, b,  6);
    R2(block, b, c, d, e, a,  7);
    R3(block, a, b, c, d, e,  8);
    R3(block, e, a, b, c, d,  9);
    R3(block, d, e, a, b, c, 10);
    R3(block, c, d, e, a, b, 11);
    R3(block, b, c, d, e, a, 12);
    R3(block, a, b, c, d, e, 13);
    R3(block, e, a, b, c, d, 14);
    R3(block, d, e, a, b, c, 15);
    R3(block, c, d, e, a, b,  0);
    R3(block, b, c, d, e, a,  1);
    R3(block, a, b, c, d, e,  2);
    R3(block, e, a, b, c, d,  3);
    R3(block, d, e, a, b, c,  4);
    R3(block, c, d, e, a, b,  5);
    R3(block, b, c, d, e, a,  6);
    R3(block, a, b, c, d, e,  7);
    R3(block, e, a, b, c, d,  8);
    R3(block, d, e, a, b, c,  9);
    R3(block, c, d, e, a, b, 10);
    R3(block, b, c, d, e, a, 11);
    R4(block, a, b, c, d, e, 12);
    R4(block, e, a, b, c, d, 13);
    R4(block, d, e, a, b, c, 14);
    R4(block, c, d, e, a, b, 15);
    R4(block, b, c, d, e, a,  0);
    R4(block, a, b, c, d, e,  1);
    R4(block, e, a, b, c, d,  2);
    R4(block, d, e, a, b, c,  3);
    R4(block, c, d, e, a, b,  4);
    R4(block, b, c, d, e, a,  5);
    R4(block, a, b, c, d, e,  6);
    R4(block, e, a, b, c, d,  7);
    R4(block, d, e, a, b, c,  8);
    R4(block, c, d, e, a, b,  9);
    R4(block, b, c, d, e, a, 10);
    R4(block, a, b, c, d, e, 11);
    R4(block, e, a, b, c, d, 12);
    R4(block, d, e, a, b, c, 13);
    R4(block, c, d, e, a, b, 14);
    R4(block, b, c, d, e, a, 15);

    /* Add the working vars back into digest[] */
    digest[0] += a;
    digest[1] += b;
    digest[2] += c;
    digest[3] += d;
    digest[4] += e;

    /* Count the number of transformations */
    transforms++;
}


inline static void buffer_to_block(const std::string &buffer, uint32_t block[BLOCK_INTS])
{
    /* Convert the std::string (byte buffer) to a uint32_t array (MSB) */
    for (size_t i = 0; i < BLOCK_INTS; i++)
    {
        block[i] = (buffer[4*i+3] & 0xff)
                   | (buffer[4*i+2] & 0xff)<<8
                   | (buffer[4*i+1] & 0xff)<<16
                   | (buffer[4*i+0] & 0xff)<<24;
    }
}


inline SHA1::SHA1()
{
    reset(digest, buffer, transforms);
}


inline void SHA1::update(std::string_view data)
{
    while (!data.empty()) {
        auto sbuf = data.substr(0,BLOCK_BYTES-buffer.size());
        data = data.substr(sbuf.size());
        buffer.append(sbuf);
        if (buffer.size() != BLOCK_BYTES) return;
        uint32_t block[BLOCK_INTS];
        buffer_to_block(buffer, block);
        transform(digest, block, transforms);
        buffer.clear();
    }

}


/*
 * Add padding and return the message digest.
 */

inline std::array<unsigned char, 20> SHA1::final()
{
    /* Total number of hashed bits */
    uint64_t total_bits = (transforms*BLOCK_BYTES + buffer.size()) * 8;

    /* Padding */
    buffer += static_cast<char>(0x80);
    size_t orig_size = buffer.size();
    while (buffer.size() < BLOCK_BYTES)
    {
        buffer += static_cast<char>(0x00);
    }

    uint32_t block[BLOCK_INTS];
    buffer_to_block(buffer, block);

    if (orig_size > BLOCK_BYTES - 8)
    {
        transform(digest, block, transforms);
        for (size_t i = 0; i < BLOCK_INTS - 2; i++)
        {
            block[i] = 0;
        }
    }

    /* Append total_bits, split this uint64_t into two uint32_t */
    block[BLOCK_INTS - 1] = static_cast<std::uint32_t>(total_bits);
    block[BLOCK_INTS - 2] = static_cast<std::uint32_t>(total_bits >> 32);
    transform(digest, block, transforms);

    std::array<unsigned char, 20> ret;
    int pos = 0;
    for (auto &d : digest) {
        ret[pos] = (d >> 24) & 0xFF;
        ++pos;
        ret[pos] = (d >> 16) & 0xFF;
        ++pos;
        ret[pos] = (d >> 8) & 0xFF;
        ++pos;
        ret[pos] = d & 0xFF;
        ++pos;
    }
    return ret;
}

constexpr char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

template <std::size_t N>
std::string base64_encode(const std::array<unsigned char, N>& input) {
    std::string encoded;
    int val = 0;
    int valb = -6;

    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }

    while (encoded.size() % 4) {
        encoded.push_back('=');
    }

    return encoded;
}

std::string calculate_ws_accept(std::string_view key) {
    SHA1 sha1;
    sha1.update(key);
    sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    auto digest = sha1.final();
    std::string encoded;
    return base64_encode(digest);
}
std::string generate_ws_key() {
    std::array<unsigned char, 16> buff;
    std::random_device rnd;
    std::uniform_int_distribution<unsigned int> dist(0,255);
    for (auto &c: buff) c = static_cast<unsigned char>(dist(rnd));
    return base64_encode(buff);

}


}

}


