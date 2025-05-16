#pragma once
#include "bridge.hpp"
#include "transport_log.hpp"

namespace zerobus {


class NullBridge {
public:

    NullBridge(Bus bus1, Bus bus2)
    :_master(std::move(bus1), std::make_unique<Bridge>(std::move(bus2)))
    {
        _master.send_reset();
        _master.refresh(true);

    }


protected:

    Bridge _master;


};


template<std::invocable<std::string_view> Output>
class DebugNullBridge {
public:


    DebugNullBridge(Bus bus1, Bus bus2, Output &&output, std::string bus1_name, std::string bus2_name)
        :_master(std::move(bus1),
                std::make_unique<TransportLog<Output> >(
                        std::forward<Output>(output),
                        std::make_unique<Bridge>(std::move(bus2)),
                        bus1_name+"=>"+bus2_name+": ",
                        bus2_name+"=>"+bus1_name+": "
                )){
        _master.send_reset();
        _master.refresh(true);

    }



protected:
    Bridge _master;

};

}
