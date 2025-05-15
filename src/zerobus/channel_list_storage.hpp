#include "message.hpp"
#include <memory>


namespace zerobus {


    ///Holds list of channels including their underlying data
    /**
     * The ChannelList is defined as a span of ChannelIDs, which requires
     * memory to store its content. This class provides functionality to
     * store the ChannelList along with its underlying data, ensuring that
     * the list can be used independently of its original source. It manages
     * the memory for the list's content, allowing for safe and efficient
     * storage and retrieval.
     */

    class ChannelListStorage {
    public:

        ///default constructor create empty list
        ChannelListStorage() = default;

        ///create object from ChannelList object, copy underlying data
        explicit ChannelListStorage(const ChannelList &lst) {
            store_channels(lst);
        }

        ///create copy
        ChannelListStorage(const ChannelListStorage &other) {
            store_channels(other.get_stored());
        }

        ///assigment
        ChannelListStorage &operator=(const ChannelListStorage &other) {
            if (this != &other) {
            store_channels(other.get_stored());
            }
            return *this;
        }

        ///move is default
        ChannelListStorage(ChannelListStorage &&) = default;
        ///move is default
        ChannelListStorage &operator=(ChannelListStorage &&) = default;

        /**
         * Stores a new list of channels in the object, replacing the current content.
         *
         * This function copies the provided list of channels into the object, including
         * the underlying data. If the currently allocated memory block is large enough
         * to hold the new data, it will be reused to optimize performance. Otherwise,
         * the existing memory block is released, and a larger block is allocated.
         *
         * @param lst The list of channels to store.
         * @return A new list of channels that references the stored data in this object.
         *
         * @note This function is optimized for memory reuse to minimize allocations
         *       when updating the stored list of channels.
         */
        ChannelList store_channels(const ChannelList &lst);

        ///Retrieve currently stored list
        /**
         * @return currently stored list
         */
        ChannelList get_stored() const;

        ChannelList make_ordered();

        ///Generates difference between two lists
        /**
         * operation return b-a;
         *
         * @param a ordered list
         * @param b ordered list
         * @return list of items presented in b but not a.
         */
        ChannelList set_difference(const ChannelList &a, const ChannelList &b);

        ChannelList set_union(const ChannelList &a, const ChannelList &b);


    protected:

        struct ChannelData {
            std::size_t size;
            std::size_t count;
            ChannelID *items();
            const ChannelID *items() const;
            char *strings();
            void clear();
        };

        struct ChannelDataDeleter {
            void operator()(ChannelData *ptr);
        };

        std::unique_ptr<ChannelData, ChannelDataDeleter> _data;
        void copy_strings();
        void alloc_items(std::size_t count, std::size_t chars);
        template<typename Op>
        ChannelList set_op(const ChannelList &a, const ChannelList &b, Op &&op);

};


}
