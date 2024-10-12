#pragma once

#include "bridge.h"

#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory_resource>
#include <deque>

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

    virtual bool subscribe(IListener *listener, ChannelID channel) override;
    virtual void unsubscribe(IListener *listener, ChannelID channel) override;
    virtual void unsubscribe_all(IListener *listener) override;
    virtual bool send_message(IListener *listener, ChannelID channel, MessageContent msg, ConversationID cid) override;
    virtual bool dispatch_message(IListener *listener, const Message &msg, bool subscribe_return_path) override;
    virtual ChannelList get_active_channels(IListener *listener, ChannelListStorage &storage) const override;
    virtual ChannelList get_subscribed_channels(IListener *listener, ChannelListStorage &storage) const override;
    virtual void register_monitor(IMonitor *mon) override;
    virtual void unregister_monitor(const IMonitor *mon) override;
    virtual bool is_channel(ChannelID id) const override;
    virtual void unsubcribe_private(IListener *listener) override;
    virtual std::string get_random_channel_name(std::string_view prefix) const override;
    virtual std::string_view get_cycle_detect_channel_name() const override;
    virtual bool clear_return_path(IListener *lsn, ChannelID sender, ChannelID receiver)  override;
    virtual void force_update_channels()  override;
    virtual bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) override;
    virtual void close_group(IListener *owner, ChannelID group_name) override;
    virtual void close_all_groups(IListener *owner) override;
    virtual void unsubscribe_all_channels(IListener *listener, bool and_groups) override;

    ///Create local message broker;
    static Bus create();

    void lock() const;
    void unlock() const;

protected:


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
        ///dtor
        ~ChanDef();

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

        IListener *get_owner() const {return _owner;}

        void set_owner(IListener *owner) {_owner = owner;}

        bool has(IListener *lsn) const;
    protected:
        mstring _name;  //a channel name
        IListener *_owner = {}; //owner of group;
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

        void promote(BackPathItem * &root);
        void remove();
    };

    using PChanMapItem = std::shared_ptr<ChanDef>;

    using ListenerToChannelMap = std::unordered_map<IListener *, mvector<ChannelID>,
            std::hash<IListener *>, std::equal_to<IListener *>,
            std::pmr::polymorphic_allocator<std::pair<IListener * const, mvector<ChannelID> > > >;
    using ChannelMap = std::map<ChannelID, PChanMapItem,
            std::less<>,std::pmr::polymorphic_allocator<std::pair<const ChannelID,  PChanMapItem > > >;
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

    struct PrivQueueItem { // @suppress("Miss copy constructor or assignment operator")
        IListener *target;
        Message msg;
        bool pm;

    };

    using PrivateQueue = std::deque<PrivQueueItem, std::pmr::polymorphic_allocator<PrivQueueItem> >;


    mutable std::recursive_mutex _mutex;               //recursive mutex
    std::pmr::synchronized_pool_resource _mem_resource; //contains memory resource for messages
    ChannelMap _channels;                   //main map mapping channel name to channel instance
    ListenerToMailboxMap _mailboxes_by_ptr; //maps listener pointer to mailbox name
    MailboxToListenerMap _mailboxes_by_name; //maps mailbox name to listener ptr
    BackPathStorage _back_path;
    mvector<IMonitor *> _monitors;      //list of monitors
    PrivateQueue _private_queue;
    mutable std::string _cycle_detector_id = {};    //contains a random string which is used to detect cycles
    mutable const IListener *_last_proxy=nullptr;    //pointer to last proxy - to check, whether there are more proxies
    mutable bool _channels_change = false;
    bool _priv_queue_running = false;
    mutable unsigned int _recursion = 0;
    ///erase mailbox
    /**
     * @param listener listener which mailbox is erased
     */
    void erase_mailbox_lk(IListener *listener);

    void erase_groups_lk(IListener *owner);
    ///create mailbox address
    /**
     * @param listener listener
     * @return mailbox address
     *
     * @note returns existing if already created
     */
    std::string_view get_mailbox(IListener *listener);

    bool unsubscribe_all_channels_lk(IListener *listener, bool and_groups);


    PChanMapItem get_channel_lk(ChannelID name);


    bool forward_message_internal(IListener *listener,  const Message &msg) ;

    void run_priv_queue(IListener *target, const Message &msg, bool pm);

    struct TLSQueueItem;
    struct TLState;


};

}

