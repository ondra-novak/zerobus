#pragma once
#include "bus.h"

namespace zerobus {

class AbstractClient: public IListener {
public:

    AbstractClient(Bus bus):_bus(std::move(bus)) {}
    ~AbstractClient() {_bus.unsubscribe_all(this);}


    AbstractClient(const AbstractClient &) = delete;
    AbstractClient &operator=(const AbstractClient &) = delete;

    Bus get_bus() const {return _bus;}

    ///subscribe channel
    /**
     * @param channel channel
     */
    void subscribe(ChannelID channel) {_bus.subscribe(this, channel);}
    ///unsubscribe channel
    /**
     * @param channel
     */
    void unsubscribe(ChannelID channel) {_bus.unsubscribe(this,channel);}
    ///unsubscribe listener from all channels
    /**
     * after return, the associated object can be destroyed
     */
    void unsubscribe_all() {_bus.unsubscribe_all(this);}

    ///unsubscribe private channel
    /**
     * This closes private channel and thus prevents to receive more private messages
     *
     * Private channel is created automatically when you specify a listener as
     * a parameter of the function send_message. This function deletes this
     * channel. Note that next usage of send_message creates new private channel
     * with a different address
     *
     */
    void unsubcribe_private() {_bus.unsubcribe_private(this);}
    ///send message
    /**
     * @param channel channel
     * @param msg message
     * @param cid conversation identifier, can be 0 if has no meaning
     * @retval true message has been posted (it doesn't indicate that has been delivered)
     * @retval false message was not posted (no information about how to route message)
     */
    bool send_message(ChannelID channel, MessageContent msg, ConversationID cid = 0) {
        return _bus.send_message(this, channel, msg, cid);
    }
    ///Generate random channel name
    /**
     * @param prefix channel name prefix
     * @return a random channel name with high entropy.
     *
     * @note useful to create ad-hoc multicast groups.
     */
    std::string get_random_channel_name(std::string_view prefix) const {
        return _bus.get_random_channel_name(prefix);
    }

    ///Test whether Id is existing channel
    /**
     * @param id channel
     * @retval true existing channel
     * @retval false channel doesn't exists
     * 
     * @note This function works on groups as well
     * 
     * @note This function cannot be used with private channel
     */
    bool is_channel(ChannelID id) const {
        return _bus.is_channel(id);
    }

    ///Add a new member to group
    /** 
     * @param group_name name of new group
     * @param sender_id name of private channel of a new member, 
     * which must be retrieved from a request message by function get_sender()
     * @retval true member added
     * @retval false cannot add member. The group is probably exists and 
     *   has different owner, or it is channel
     */
    bool add_to_group(ChannelID group_name, ChannelID sender_id) {
        return _bus.add_to_group(this, group_name, sender_id);
    }

    ///Close group
    /**
     * @param group_name name of group to close
     * @note you can only close a group you owns. 
     */
    void close_group(ChannelID group_name) {
        _bus.close_group(this, group_name);
    }

    ///Close all groups opened by this client
    void close_all_group() {
        _bus.close_all_groups(this);
    }

    ///Retrieves current subscribed channels and groups
    /**
     * @param storage a storage object. It is intended to store channel data
     * and hold locks during processing the result. You should reset or
     * destroy the object, once you are done with result (otherwise it
     * can hold some resources)
     * 
     * You can reuse existing object from previous call to speed up
     * operation a lite. However don't forget to call clear() 
     * after usage/
     * 
     * @return Returns list of subscribred channels
     */
    Bus::ChannelList get_subscribed_channels(Bus::ChannelListStorage &storage) const {
        return _bus.get_subscribed_channels(this, storage);
     }

public:
    //Overrides
    virtual void on_close_group(ChannelID ) noexcept override {}
    virtual void on_no_route(ChannelID , ChannelID ) noexcept override {}
    virtual void on_group_empty(ChannelID ) noexcept override {}
    virtual void on_message(const Message &, bool ) noexcept override {}
    virtual void on_add_to_group(ChannelID , ChannelID ) noexcept override {}

protected:
    Bus _bus;    
};


template<std::invocable<AbstractClient &, const Message &, bool> Fn >
class ClientCallback: public AbstractClient {
public:

    ClientCallback(Bus bus, Fn &&fn)
        :AbstractClient(std::move(bus))
        ,_fn(std::forward<Fn>(fn)) {}

    virtual void on_message(const Message &message, bool pm) noexcept override {
        _fn(*this, message, pm);
    }


protected:
    Fn _fn;
};


}
