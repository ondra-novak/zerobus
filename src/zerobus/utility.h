namespace zerobus {


template<typename _Container, typename _Cmp>
void heapify_down(_Container &cont, std::size_t index, _Cmp &&cmp) {
    std::size_t n = cont.size();

    while (true) {
        auto left = 2 * index + 1;
        auto right = 2 * index + 2;
        auto largest = index;

        if (left < n && cmp(cont.operator[](largest), cont.operator[](left))) {
            largest = left;
        }

        if (right < n && cmp(cont.operator[](largest), cont.operator[](right))) {
            largest = right;
        }

        if (largest != index) {
            std::swap(cont.operator[](index),cont.operator[](largest));
            index = largest;
        } else {
            break;
        }
    }
}

template<typename _Container, typename _Cmp>
void heapify_up(_Container &cont, std::size_t index, _Cmp &&cmp) {

    while (index > 0) {
        auto parent = (index - 1) / 2;

        if (cmp(cont.operator[](parent) , cont.operator[](index))) {
            std::swap(cont.operator[](index), cont.operator[](parent));
            index = parent;
        } else {
            break;
        }
    }
}


template<typename _Container, typename _Cmp>
void heapify_remove(_Container &cont, std::size_t index, _Cmp &&cmp) {
    if (index < cont.size()-1) {
        cont.pop_back();
    } else {
        bool cmpres = cmp(cont[index], cont.back());
        std::swap(cont[index], cont.back());
        cont.pop_back();
        if (cmpres) {
            heapify_down(cont, index, cmp);
        } else {
            heapify_up(cont, index, cmp);
        }
    }
}

}
