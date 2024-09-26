namespace zerobus {

struct CountingOutputIterator {
    size_t* out;

    explicit CountingOutputIterator(size_t* where) : out(where) {
        // Handled in initializer list
    }
    template<typename T>
    CountingOutputIterator& operator=(T &&value) {
        return *this;
    }
    CountingOutputIterator& operator*() { return *this; }
    CountingOutputIterator& operator++() { (*out)++; return *this;}
    CountingOutputIterator& operator++(int) { (*out)++; return *this;}

};
}

namespace {
///Helper class which converts a lambda to an output iterator
template <typename Func>
class LambdaOutputIterator {
public:
    LambdaOutputIterator(Func func) : _fn(func) {}

    template<typename T>
    LambdaOutputIterator& operator=(T &&value) {
        _fn(std::forward<T>(value));
        return *this;
    }
    LambdaOutputIterator& operator*() { return *this; }
    LambdaOutputIterator& operator++() { return *this; }
    LambdaOutputIterator& operator++(int) { return *this; }

private:
    Func _fn;
};

}
