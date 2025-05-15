#include "utils/binary.hpp"
#include "utils/stack_alloc.hpp"
#include "utils/dispatch_queue.hpp"
#include "utils/recursive_dispatcher.hpp"
#include "binary_transport.hpp"

template class zerobus::BinaryTransport<decltype([](const auto &){})>;
