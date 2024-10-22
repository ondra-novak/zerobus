#include "bridge_pipe.h"

namespace zerobus {

BridgePipe::BridgePipe(Bus bus, std::shared_ptr<INetContext> ctx,
                        ConnHandle read, ConnHandle write)
:AbstractBridge(std::move(bus))
,_ctx(std::move(ctx))
,_h_read(read)
,_h_write(write) {
    ready_to_send();
    ready_to_receive();
    BridgePipe::send(NewSession{});
    register_monitor(this);
}

BridgePipe::~BridgePipe() {
    unregister_monitor(this);
    _ctx->destroy(_h_write);
    _ctx->destroy(_h_read);
}

void BridgePipe::receive_complete(std::string_view data) noexcept {
    if (data.empty()) {
        on_disconnect();
        return;
    }
    data = combine_input_before_parse(data);
    data = parse_messages(data);
    combine_input_after_parse(data);
    ready_to_receive();
}

void BridgePipe::send(const AddToGroup &msg) noexcept {send_gen(msg);}
void BridgePipe::send(const ChannelReset &msg) noexcept {send_gen(msg);}
void BridgePipe::send(const GroupEmpty &msg) noexcept {send_gen(msg);}
void BridgePipe::send(const ChannelUpdate &msg) noexcept {send_gen(msg);}
void BridgePipe::send(const NewSession &msg) noexcept {send_gen(msg);}
void BridgePipe::send(const CloseGroup &msg) noexcept {send_gen(msg);}
void BridgePipe::send(const NoRoute &msg) noexcept  {send_gen(msg);}
void BridgePipe::send(const Message &msg) noexcept  {send_gen(msg);}
void BridgePipe::send(const UpdateSerial &msg) noexcept  {send_gen(msg);}

BridgePipe BridgePipe::connect_stdinout(Bus bus, std::shared_ptr<INetContext> ctx) {
    ConnHandle rd = ctx->connect(SpecialConnection::stdinput);
    ConnHandle wr = ctx->connect(SpecialConnection::stdoutput);
    return BridgePipe(std::move(bus), std::move(ctx), rd, wr);
}

BridgePipe BridgePipe::connect_stdinout(Bus bus) {
    return connect_stdinout(std::move(bus), make_network_context());
}

BridgePipe BridgePipe::connect_process(Bus bus, std::shared_ptr<INetContext> ctx,
        std::string_view command_line, std::stop_token tkn,
        std::function<void(int)> exit_action) {

    auto h = spawn_process(ctx, command_line, std::move(tkn), std::move(exit_action));
    return BridgePipe(std::move(bus), std::move(ctx), h.read, h.write);

}

BridgePipe BridgePipe::connect_process(Bus bus, std::string_view command_line,
        std::stop_token tkn, std::function<void(int)> exit_action) {
    return connect_process(std::move(bus), make_network_context(1), command_line, std::move(tkn), std::move(exit_action));
}

std::string_view BridgePipe::parse_messages(const std::string_view &data) {
    std::string_view out = data;
    while (Deserialization::can_read_uint(out)) {
        std::string_view msg = out;
        auto sz = Deserialization::read_uint(msg);
        if (msg.size() < sz) break;
        out = msg.substr(sz);
        msg = msg.substr(0, sz);
        std::visit([&](const auto &x){
            receive(x);
        }, _deser(msg));
    }
    return out;
}

void BridgePipe::clear_to_send() noexcept {
    bool ok = false;
    {
        std::lock_guard _(_mx);
        _clear_to_send = true;
        ok = flush_output();
    }
    if (!ok) on_disconnect();
}

void BridgePipe::on_timeout() noexcept {
}

void BridgePipe::ready_to_send() {
    _ctx->ready_to_send(_h_write, this);
}

void BridgePipe::ready_to_receive() {
    _ctx->receive(_h_read, _input_buffer, this);
}

void BridgePipe::on_channels_update() noexcept {
    _ctx->set_timeout(_h_read, std::chrono::system_clock::now(), this);
}

std::string_view BridgePipe::combine_input_before_parse(const std::string_view &data) {
    if (_msg_tmp_buffer.empty()) {
        return data;
    } else {
        auto cur_size = _msg_tmp_buffer.size();
        _msg_tmp_buffer.resize(cur_size+data.size());
        std::copy(data.begin(), data.end(), _msg_tmp_buffer.begin()+cur_size);
        return {_msg_tmp_buffer.data(), _msg_tmp_buffer.size()};
    }
}

bool BridgePipe::flush_output() {
    if (_output_buffer.empty()) return true;
    if (!_clear_to_send) return false;
    auto r = _ctx->send(_h_write, {_output_buffer.data(), _output_buffer.size()});
    if (r == _output_buffer.size()) {
        _output_buffer.clear();
    } else if (r > 0){
        _output_buffer.erase(_output_buffer.begin(), _output_buffer.begin()+r);
    } else {
        return false;
    }
    _clear_to_send = false;
    ready_to_send();
    return true;
}

void BridgePipe::combine_input_after_parse(const std::string_view &data) {
    if (_msg_tmp_buffer.size() < data.size()) {
        if (!data.empty()) {
            _msg_tmp_buffer.resize(data.size());
            std::copy(data.begin(), data.end(), _msg_tmp_buffer.begin());
        }
    } else {
        std::move(data.begin(), data.end(), _msg_tmp_buffer.begin());
        _msg_tmp_buffer.resize(data.size());
    }
}

template<typename T> void BridgePipe::send_gen(const T &msg) {
    std::lock_guard _(_mx);
    auto str = _ser(msg);
    Serialization::write_uint(std::back_inserter(_output_buffer), str.size());
    std::copy(str.begin(), str.end(), std::back_inserter(_output_buffer));
    flush_output();
}
}
