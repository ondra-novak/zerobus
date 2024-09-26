#pragma once

#include "bridge.h"

#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <memory_resource>

namespace zerobus {



///Implementation local message broker, it also defines messages and other functions
/**
 * To extend to network broker, you can inherit this broker, or implement a node
 * as a ordinary listener. If you need to monitor channels, you can inherit and register
 * IMonitor object
 *
 */
class LocalBus: public IBridgeAPI,
                      public std::enable_shared_from_this<LocalBus> {
public:

    LocalBus();

    virtual void subscribe(IListener *listener, ChannelID channel) override;
    virtual void unsubscribe(IListener *listener, ChannelID channel) override;
    virtual void unsubscribe_all(IListener *listener) override;
    virtual bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) override;
    virtual bool dispatch_message(IListener *listener, const Message &msg, bool subscribe_return_path) override;
    virtual Message create_message(ChannelID sender, ChannelID channel, MessageContent msg, ConversationID cid) override;
    virtual void get_active_channels(IListener *listener, FunctionRef<void(ChannelList)> &&callback) const override;
    virtual void get_subscribed_channels(IListener *listener, FunctionRef<void(ChannelList)> &&callback) const override;
    virtual void register_monitor(IMonitor *mon) override;
    virtual void unregister_monitor(const IMonitor *mon) override;
    virtual bool is_channel(ChannelID id) const override;
    virtual void unsubcribe_private(IListener *listener) override;
    virtual std::string get_random_channel_name(std::string_view prefix) const override;
    virtual std::string_view get_node_id() const override;

    ///Create local message broker;
    static Bus create();

protected:

    class MessageDef;

    using mstring = std::basic_string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char> >;
    template<typename T>
    using mvector = std::vector<T, std::pmr::polymorphic_allocator<T> >;

    ///Channel definition
    /** Channel is standalone object. It is reference using shared_ptr. It cannot
     * be moved.
     * The class solves major synchronization issue. It is locked during broadcasting
     * so the listeners are called under the lock. However it is possible, that
     * listener will need to unsubscribe, or subscribe a different listener
     * during this process. So channel class uses recursive mutex, which allows
     * to call channel operations under the lock. For situation when channel
     * is unsubscribed during broadcasting, the registration is changed to nullptr
     * and it is clean when lock is eventually released.
     *
     */
    class ChanDef {
    public:
        ///Construct channel
        /**
         * @param name channel name. You can use get_id(), to receive ChannelID under which
         * the channel can be stored in a map
         */
        ChanDef(std::string_view name, std::pmr::memory_resource *memres);
        ///cannot be copied nor moved
        ChanDef(const ChanDef &) = delete;
        ///cannot be copied nor moved
        ChanDef &operator=(const ChanDef &) = delete;

        ///lock channel internals
        void lock();        //lock the item
        ///unlock channel internals
        void unlock();      //unlock the item
        ///determine whether it is empty (no listeners)
        bool empty() const;
        ///add listener
        void add_listener(IListener *lsn);
        ///remove listener
        bool remove_listener(IListener *lsn);
        ///determines whether channel can be exported seen from perspective or listener
        bool can_export(IListener *lsn) const;
        ///enumerates active listeners (available from source code only)
        template<std::invocable<IListener *> Fn>
        void enum_listeners(Fn &&fn);
        ///retrieve id
        ChannelID get_id() const;
    protected:
        mstring _name;  //a channel name
        mvector<IListener *> _listeners; //list of listeners. Nullptr are skipped
        mutable std::recursive_mutex _mx;
        unsigned int _recursion = 0; //count of lock recursion
        unsigned int _del_count = 0; //count of deleted listners(set nullptr);
    };

    struct BackPathItem { // @suppress("Miss copy constructor or assignment operator")
        BackPathItem *prev = {};
        BackPathItem *next = {};
        mvector<char> id = {};
        IListener *l = {};

        void promote(BackPathItem *root);
        void remove();
    };

    using PChanMapItem = std::shared_ptr<ChanDef>;

    using ListenerToChannelMap = std::unordered_map<IListener *, mvector<ChannelID>,
            std::hash<IListener *>, std::equal_to<IListener *>,
            std::pmr::polymorphic_allocator<std::pair<IListener * const, mvector<ChannelID> > > >;
    using ChannelMap = std::unordered_map<ChannelID, PChanMapItem,
            std::hash<ChannelID>, std::equal_to<ChannelID>,
            std::pmr::polymorphic_allocator<std::pair<const ChannelID,  PChanMapItem > > >;
    using ListenerToMailboxMap = std::unordered_map<IListener *, mstring,
            std::hash<IListener *>, std::equal_to<IListener *>,
            std::pmr::polymorphic_allocator<std::pair<IListener * const, mstring> > >;
    using MailboxToListenerMap = std::unordered_map<std::string_view, IListener *,
            std::hash<std::string_view>, std::equal_to<std::string_view>,
            std::pmr::polymorphic_allocator<std::pair<const std::string_view , IListener *> > >;
    using BackPathMap = std::unordered_map<std::string_view, BackPathItem,
            std::hash<std::string_view>, std::equal_to<std::string_view>,
            std::pmr::polymorphic_allocator<std::pair<const std::string_view , BackPathItem > > >;


    class BackPathStorage {
    public:
        BackPathStorage(std::pmr::memory_resource &res);
        void store_path(const ChannelID &chan, IListener *lsn);
        IListener *find_path(const ChannelID &chan) const;
        void remove_listener(IListener *l);

        std::size_t _limit = 128;     //maximum total of entries in _back_path container

    protected:
        BackPathMap _entries;                 //map of back path routing
        BackPathItem *_root = {};
        BackPathItem *_last = {};

    };

    mutable std::recursive_mutex _mx;               //recursive mutex
    std::pmr::synchronized_pool_resource _mem_resource; //contains memory resource for messages
    ChannelMap _channels;                   //main map mapping channel name to channel instance
    ListenerToChannelMap _listeners;        //helps to find subscribed channels
    ListenerToMailboxMap _mailboxes_by_ptr; //maps listener pointer to mailbox name
    MailboxToListenerMap _mailboxes_by_name; //maps mailbox name to listener ptr
    BackPathStorage _back_path;
    mvector<IMonitor *> _monitors;      //list of monitors
    mutable std::vector<ChannelID> _tmp_channels;    //temporary vector for get_active_channels
    std::string _cycle_detector_id;    //contains a random string which is used to detect cycles

    ///removes channel from existing listener.
    /**
     * @param channel channel to remove
     * @param listener listener
     * @retval true channel found and removed
     * @retval false channel was not subscribed
     *
     * @note if the listener has no more channels, it is removed from map
     */
    bool remove_channel_from_listener_lk(std::string_view channel, IListener *listener);
    ///erase mailbox
    /**
     * @param listener listener which mailbox is erased
     */
    void erase_mailbox_lk(IListener *listener);
    ///create mailbox address
    /**
     * @param listener listener
     * @return mailbox address
     *
     * @note returns existing if already created
     */
    std::string_view get_mailbox(IListener *listener);



    std::pair<PChanMapItem,bool> get_channel_lk(ChannelID name);

    void channel_list_updated_lk();

    bool forward_message_internal(IListener *listener, const Message &msg) ;

};

}

