#define CONSTEXPR_TESTABLE constexpr
#include "../zerobus/websocket.cpp"

struct WebSocketTestFrame {
    const uint8_t* header;  // ukazatel na bajty hlavičky
    zerobus::ws::Type t;
    size_t header_length;   // délka hlavičky v bajtech
    size_t payload_length;  // očekávaná délka obsahu rámce
    const uint8_t* mask_key;  // ukazatel na maskovací klíč (nullptr, pokud není maskovaný)
};

constexpr uint8_t frame1[] = { 0x81, 0x7D };  // Finální textový rámec, délka payloadu: 125 bajtů (bez maskování)
constexpr uint8_t frame2[] = { 0x82, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E };  // Binární rámec, délka payloadu: 126 bajtů (bez maskování)
constexpr uint8_t frame3[] = { 0x81, 0x7E, 0x01, 0x00 };  // Finální textový rámec, délka payloadu: 256 bajtů (bez maskování)
constexpr uint8_t frame4[] = { 0x82, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 };  // Binární rámec, délka payloadu: 256 bajtů (bez maskování)
constexpr uint8_t frame5[] = { 0x81, 0x7E, 0x03, 0xE8 };  // Finální textový rámec, délka payloadu: 1000 bajtů (bez maskování)
constexpr uint8_t frame6[] = { 0x82, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xE8 };  // Binární rámec, délka payloadu: 1000 bajtů (bez maskování)

constexpr uint8_t mask_key1[] = { 0x12, 0x34, 0x56, 0x78 };  // Maskovací klíč
constexpr uint8_t frame7[] = { 0x81, 0xFE, 0x00, 0x7D, 0x12, 0x34, 0x56, 0x78 };  // Maskovaný textový rámec, délka payloadu: 125 bajtů
constexpr uint8_t mask_key2[] = { 0xA1, 0xB2, 0xC3, 0xD4 };  // Maskovací klíč
constexpr uint8_t frame8[] = { 0x82, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0xA1, 0xB2, 0xC3, 0xD4 };  // Maskovaný binární rámec, délka payloadu: 126 bajtů
constexpr uint8_t mask_key3[] = { 0x11, 0x22, 0x33, 0x44 };  // Maskovací klíč
constexpr uint8_t frame9[] = { 0x81, 0xFE, 0x01, 0x00, 0x11, 0x22, 0x33, 0x44 };  // Maskovaný textový rámec, délka payloadu: 256 bajtů
constexpr uint8_t mask_key4[] = { 0x99, 0x88, 0x77, 0x66 };  // Maskovací klíč
constexpr uint8_t frame10[] = { 0x82, 0xFF,0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x99, 0x88, 0x77, 0x66 };  // Maskovaný binární rámec, délka payloadu: 256 bajtů

// Frame 11: Prázdný frame (finální textový rámec, velikost 0, bez maskování)
constexpr uint8_t frame11[] = { 0x81, 0x00 };  // Finální textový rámec, bez payloadu

// Frame 12: Prázdný frame s maskováním (finální textový rámec, velikost 0, s maskováním)
constexpr uint8_t mask_key5[] = { 0xAA, 0xBB, 0xCC, 0xDD };  // Maskovací klíč
constexpr uint8_t frame12[] = { 0x81, 0x80, 0xAA, 0xBB, 0xCC, 0xDD };  // Maskovaný, bez payloadu

// Nevalidní sekvence: Prázdný frame s 16bitovou velikostí (0x0000) bez maskování
constexpr uint8_t frame_invalid16bit[] = { 0x81, 0x7E, 0x00, 0x00 };  // Textový rámec, délka 16bit (0x0000)

// Nevalidní sekvence: Prázdný frame s 64bitovou velikostí (0x0000000000000000) bez maskování
constexpr uint8_t frame_invalid64bit[] = { 0x81, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  // Textový rámec, délka 64bit (0x0000000000000000)

constexpr WebSocketTestFrame test_frames[] = {
    { frame1, zerobus::ws::Type::text, sizeof(frame1), 125, nullptr },  // Bez maskování
    { frame2, zerobus::ws::Type::binary, sizeof(frame2), 126, nullptr },  // Bez maskování
    { frame3, zerobus::ws::Type::text, sizeof(frame3), 256, nullptr },  // Bez maskování
    { frame4, zerobus::ws::Type::binary, sizeof(frame4), 256, nullptr },  // Bez maskování
    { frame5, zerobus::ws::Type::text, sizeof(frame5), 1000, nullptr }, // Bez maskování
    { frame6, zerobus::ws::Type::binary, sizeof(frame6), 1000, nullptr }, // Bez maskování
    { frame7, zerobus::ws::Type::text, sizeof(frame7), 125, mask_key1 }, // Maskovaný
    { frame8, zerobus::ws::Type::binary, sizeof(frame8), 126, mask_key2 }, // Maskovaný
    { frame9, zerobus::ws::Type::text, sizeof(frame9), 256, mask_key3 }, // Maskovaný
    { frame10, zerobus::ws::Type::binary, sizeof(frame10), 256, mask_key4 }, // Maskovaný
    { frame11, zerobus::ws::Type::text, sizeof(frame11), 0, nullptr },  // Bez maskování, velikost 0
    { frame12, zerobus::ws::Type::text, sizeof(frame12), 0, mask_key5 },  // S maskováním, velikost 0
    { frame_invalid16bit, zerobus::ws::Type::text, sizeof(frame_invalid16bit), 0, nullptr },  // Nevalidní 16bit délka
    { frame_invalid64bit, zerobus::ws::Type::text, sizeof(frame_invalid64bit), 0, nullptr },  // Nevalidní 64bit délka
};

constexpr void append_some_extra(std::vector<char> &data) {
    auto l = data.size();
    for (std::size_t i = 0; i < l/2; ++i) {
        data.push_back(static_cast<char>(i & 0xFF));
    }

}

constexpr void generate_frame(const WebSocketTestFrame &frame, std::vector<char> &data) {
    data.clear();
    for (std::size_t i = 0; i < frame.header_length; ++i) {
        data.push_back(static_cast<char>(frame.header[i]));
    }
    if (frame.mask_key) {
        for (std::size_t i = 0; i < frame.payload_length; ++i) {
            auto m = frame.mask_key[i & 0x3] ^ static_cast<std::uint8_t>(i & 0xFF);
            data.push_back(static_cast<char>(m));
        }
    } else {
        for (std::size_t i = 0; i < frame.payload_length; ++i) {
            data.push_back(static_cast<char>(i & 0xFF));
        }
    }
}


constexpr void check_payload(std::string_view data) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i] != (static_cast<char>(i & 0xFF))) {
            char test_failed_at_pos[1];
            test_failed_at_pos[i] = 1;
            throw "test failed";
        }
    }
}

constexpr void test_frame(const WebSocketTestFrame &frame, zerobus::ws::Parser &p) {
    std::vector<char> fdata;
    generate_frame(frame, fdata);
    append_some_extra(fdata);
    std::string_view snip(fdata.data(), fdata.size());
    while (!snip.empty()) {
        auto m = snip.substr(0,20);
        snip = snip.substr(m.size());
        if (p.push_data(m)) break;
    }
    if (!p.is_complete()) {
        throw "message must be complete";
    }
    auto msg = p.get_message();
    if (msg.type != frame.t) {
        if (msg.type == zerobus::ws::Type::unknown) {
            throw "Unknown type returned";
        }
        char bad_type[1];
        bad_type[static_cast<int>(msg.type)] = 1; //    (type offset);
        throw "test failed";
    }
    if (msg.payload.size() != msg.payload.size()) throw "bad payload";
    check_payload(msg.payload);
    auto u = p.get_unused_data();
    check_payload(u);
    p.reset();
}

constexpr bool run_test() {

    std::vector<char> buffer;
    zerobus::ws::Parser p(buffer);

    test_frame(test_frames[0], p);
    test_frame(test_frames[1], p);
    test_frame(test_frames[2], p);
    test_frame(test_frames[3], p);
    test_frame(test_frames[4], p);
    test_frame(test_frames[5], p);
    test_frame(test_frames[6], p);
    test_frame(test_frames[7], p);
    test_frame(test_frames[8], p);
    test_frame(test_frames[9], p);
    test_frame(test_frames[10], p);
    test_frame(test_frames[11], p);
    test_frame(test_frames[12], p);
    test_frame(test_frames[13], p);





    return true;
}

constexpr bool test_result = run_test();

int main() {
    return !test_result; //not actual test - all done during complile time
}

