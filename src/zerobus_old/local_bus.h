#pragma once

#include "bridge.h"

#include <string>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory_resource>
#include <deque>
#include <shared_mutex>

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
    virtual ChannelList get_active_channels(const IListener *listener, ChannelListStorage &storage) const override;
    virtual ChannelList get_subscribed_channels(const IListener *listener, ChannelListStorage &storage) const override;
    virtual void register_monitor(IMonitor *mon) override;
    virtual void unregister_monitor(const IMonitor *mon) override;
    virtual bool is_channel(ChannelID id) const override;
    virtual void unsubcribe_private(IListener *listener) override;
    virtual std::string get_random_channel_name(std::string_view prefix) const override;
    virtual bool clear_return_path(IListener *lsn, ChannelID sender, ChannelID receiver)  override;
    virtual void force_update_channels()  override;
    virtual bool add_to_group(IListener *owner, ChannelID group_name, ChannelID uid) override;
    virtual void close_group(IListener *owner, ChannelID group_name) override;
    virtual void close_all_groups(IListener *owner) override;
    virtual void unsubscribe_all_channels(IListener *listener, bool and_groups) override;
    virtual bool set_serial(IListener *lsn, SerialID serialId) override;
    virtual SerialID get_serial(IListener *lsn) const override;
    virtual void update_subscribtion(IListener *lsn, Operation op, ChannelList channels) override;

    ///Create local message broker;
    static Bus create();

    void lock() const;
    void unlock() const;

protected:


    using mstring = std::basic_string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char> >;
    template<typename T>
    using mvector = std::vector<T, std::pmr::polymorphic_allocator<T> >;

    class ITargetDef {
    public:
        virtual ~ITargetDef() = default;
        virtual void broadcast(const IListener *lsn, const Message &msg) const = 0;

    };

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
    class ChanDef : public ITargetDef{
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

        ///determine whether it is empty (no listeners)
        bool empty() const;
        ///add listener
        void add_listener(IListener *lsn);
        ///remove listener
        bool remove_listener(IListener *lsn);
        ///determines whether channel can be exported seen from perspective or listener
        bool can_export(const IListener *lsn) const;
        ///retrieve id
        ChannelID get_id() const;

        IListener *get_owner() const {return _owner;}

        void set_owner(IListener *owner) {_owner = owner;}

        virtual void broadcast(const IListener *lsn, const Message &msg) const override;

        bool has(const IListener *lsn) const;
    protected:
        mstring _name;  //a channel name
        IListener *_owner = {}; //owner of group;
        mvector<IListener *> _listeners; //list of listeners. Nullptr are skipped
        mutable std::shared_mutex _mx;
    };

    struct BackPathItem { // @suppress("Miss copy constructor or assignment operator")
        BackPathItem *prev = {};
        BackPathItem *next = {};
        mvector<char> id = {};
        IListener *l = {};

        void promote(BackPathItem  &root);
        void remove();
    };

    using PTargetMapItem = std::shared_ptr<ITargetDef>;
    using PChanMapItem = std::shared_ptr<ChanDef>;

    class MbxDef: public ITargetDef {
    public:
        MbxDef(IListener *lsn, mstring id);
        virtual void broadcast(const IListener *lsn, const Message &msg) const override;
        std::string_view get_id() const {return _id;}
        IListener *get_owner() const {return _owner;}
        void disable();
    protected:
        IListener *_owner;
        mstring _id;
        mutable std::atomic<bool> _disabled = {false};
    };

    using PMBxDef = std::shared_ptr<MbxDef>;

    using ListenerToChannelMap = std::unordered_map<IListener *, mvector<ChannelID>,
            std::hash<IListener *>, std::equal_to<IListener *>,
            std::pmr::polymorphic_allocator<std::pair<IListener * const, mvector<ChannelID> > > >;
    using ChannelMap = std::map<ChannelID, PChanMapItem,
            std::less<>,std::pmr::polymorphic_allocator<std::pair<const ChannelID,  PChanMapItem > > >;
    using ListenerToMailboxMap = std::unordered_map<IListener *, PMBxDef,
            std::hash<IListener *>, std::equal_to<IListener *>,
            std::pmr::polymorphic_allocator<std::pair<IListener * const, PMBxDef> > >;
    using MailboxToListenerMap = std::unordered_map<std::string_view, PMBxDef,
            std::hash<std::string_view>, std::equal_to<std::string_view>,
            std::pmr::polymorphic_allocator<std::pair<const std::string_view , PMBxDef> > >;
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
        BackPathItem _root = {};
        BackPathItem *_last = {};

    };



    mutable std::recursive_mutex _mutex;               //recursive mutex
    std::pmr::synchronized_pool_resource _mem_resource; //contains memory resource for messages
    ChannelMap _channels;                   //main map mapping channel name to channel instance
    ListenerToMailboxMap _mailboxes_by_ptr; //maps listener pointer to mailbox name
    MailboxToListenerMap _mailboxes_by_name; //maps mailbox name to listener ptr
    BackPathStorage _back_path;
    mvector<IMonitor *> _monitors;      //list of monitors
    std::string _this_serial;           //this node serial id
    std::string _cur_serial;            //current serial id
    IListener *_serial_source = {};     //listener which sets _cur_serial
    mutable bool _channels_change = false;
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
    bool subscribe_lk(IListener *listener, ChannelID channel) ;
    void unsubscribe_lk(IListener *listener, ChannelID channel) ;


    PChanMapItem get_channel_lk(ChannelID name);


    bool forward_message_internal(IListener *listener,  const Message &msg) ;


    void channel_is_empty(ChannelID id);
    void remove_mailbox(IListener *lsn);

    template<bool ref>
    struct TLSMsgQueueItem;
    struct TLSLsnQueueItem;
    template<bool ref>
    struct TLSPrivMsgQueueItem;
    struct TLState;


};

}

