#include "utils/binary.hpp"
#include "utils/stack_alloc.hpp"
#include "utils/dispatch_queue.hpp"
#include "utils/recursive_dispatcher.hpp"
#include "binary_transport.hpp"
#include "transport_log.hpp"
#include "null_bridge.hpp"

template class zerobus::BinaryTransport<zerobus::OutputTypeTest>;
template class zerobus::TransportLog<decltype([](const std::string_view &){})>;
template class zerobus::DebugNullBridge<decltype([](const std::string_view &){})>;
