#pragma once

#include <format>
#include "protocol.hpp"


namespace zerobus {

template<std::invocable<std::string_view> Output>
class TransportOneDirectionLogger: public AbstractTransport {
public:
    TransportOneDirectionLogger(Output &&output, std::string prefix)
        :_output(std::forward<Output>(output)),_prefix(prefix) {}

    virtual void set_target(IProtocol *target) override {
        _target = target;
    }
    virtual void receive(const bmsg::EraseChannels &msg) noexcept override {
        auto t = _prefix;
        t.append("DEL ");
        channel_list(t, msg.lst);
        _output(t);
        _target->receive(msg);
    }
    virtual void receive(const bmsg::GroupEmpty &msg) noexcept override {
        auto t = _prefix;
        t.append("GROUP_IS_EMPTY ").append(msg.group);
        _output(t);
        _target->receive(msg);
    }
    virtual void receive(const bmsg::AddChannels &msg) noexcept override {
        auto t = _prefix;
        t.append("ADD ");
        channel_list(t, msg.lst);
        _output(t);
        _target->receive(msg);

    }
    virtual void receive(const bmsg::Announce &msg) noexcept override {
        auto t = _prefix;
        t.append("ANNOUNCE ( ");
        t.append(std::to_string(msg.request_id));
        t.append(") ").append(msg.sender);
        _output(t);
        _target->receive(msg);

    }

    virtual void receive(const bmsg::ChannelReset &msg) noexcept override {
        auto t = _prefix;
        t.append("RESET");
        _output(t);
        _target->receive(msg);

    }
    virtual void receive(const bmsg::NewSession &msg) noexcept override {
        auto t = _prefix;
        t.append("SESSION ");
        t.append(std::to_string(msg.version));
        _output(t);
        _target->receive(msg);
    }
    virtual void receive(const bmsg::AddToGroup &msg) noexcept override {
        auto t = _prefix;
        t.append("GROUP_ADD ");
        t.append(msg.group);
        t.append(" <= ");
        t.append(msg.target);
        _output(t);
        _target->receive(msg);

    }
    virtual void receive(const bmsg::CloseGroup &msg) noexcept override {
        auto t = _prefix;
        t.append("GROUP_CLOSE ");
        t.append(msg.group);
        _output(t);
        _target->receive(msg);

    }
    virtual void receive(const Message &msg) noexcept override {
        auto t = _prefix;
        t.append("MESSAGE  ");
        t.append(msg.get_sender());
        t.append(" => ");
        t.append(msg.get_channel());
        t.append(": (");
        t.append(std::to_string(msg.get_conversation()));
        t.append(") ");;
        t.append(msg.get_content());
        _output(t);
        _target->receive(msg);

    }
    virtual void receive(const bmsg::NoRoute &msg) noexcept override {
        auto t = _prefix;
        t.append("NO_ROUTE ");
        t.append(msg.sender);
        t.append(" => ");
        t.append(msg.receiver);
        t.append(": (");
        t.append(std::to_string(msg.cid));
        t.append(") ");;
        _output(t);
        _target->receive(msg);

    }
    virtual void receive(const bmsg::UpdateSerial &msg) noexcept override {
        auto t = _prefix;
        t.append("SERIAL ");
        t.append(msg.serial);
        _output(t);
        _target->receive(msg);

    }

protected:
    Output _output;
    std::string _prefix;
    IProtocol *_target = nullptr;


    static void channel_list(std::string &t, const ChannelList &lst) {
        if (lst.empty()) t.append("<empty>");
        else {
            t.append(lst[0]);
            for (std::size_t i = 1; i < lst.size(); ++i) {
                t.append(", ");
                t.append(lst[i]);
            }
        }
    }

};

template<typename X>
struct OutputRedirFn {
    X *ptr;
    void operator()(const std::string_view &txt) {
        ptr->on_output(txt);
    }
};


template<std::invocable<std::string_view> Output>
class TransportLog : public TransportOneDirectionLogger<OutputRedirFn<TransportLog<Output > > >{
public:

    using OutputFn  = OutputRedirFn<TransportLog<Output > >;

    TransportLog(Output &&output, std::unique_ptr<AbstractTransport> transport,
            std::string out_prefix, std::string in_prefix)
        :TransportOneDirectionLogger<OutputFn>(OutputFn{this}, out_prefix)
        ,_output(std::forward<Output>(output))
        ,_transport(std::move(transport))
        ,_incoming(OutputFn{this}, in_prefix)
    {
        TransportOneDirectionLogger<OutputFn>::set_target(_transport.get());
        _transport->set_target(&_incoming);
    }

    virtual void set_target(IProtocol *target) override {
        _incoming.set_target(target);
    }


protected:

    friend struct OutputRedirFn<TransportLog> ;


    Output _output;
    std::unique_ptr<AbstractTransport> _transport;
    TransportOneDirectionLogger<OutputRedirFn<TransportLog> > _incoming;


    void on_output(std::string_view msg) {
        _output(msg);
    }

};


}

