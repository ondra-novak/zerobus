#pragma once

#ifndef CPP_OVERLOADED
#define CPP_OVERLOADED

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; }; // (1)
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;  // (2)


#endif
