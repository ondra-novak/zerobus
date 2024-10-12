# ZEROBUS

Send messages between parts of the system, wherever they are. They can be in the same process, in another process or on another network. The basic communication between components are communication channels. Each node in the system can listen to any number of channels and can also send messages to any number of channels. In addition, you can also send direct messages between nodes or create multicast groups


## Basic usage

```
#include <zerobus/bus.h>

int main() {
    auto bus = zerobus::Bus::create();      //now we have a bus instance
    //...
    //...
    
    //subscribe a listener    
    bus.subscribe("channel_name", listener); 
    
    //broadcast an anonymous message
    bus.send_message(nullptr, "channel_name", "message");   
    
     //broadcast a message and set listener as a sender
    bus.send_message(listener, "other_channel", "message"); 
```
    
    
## Listener

You need create listeners by implementing `IListener interface`

```
#include <zerobus/listener.h>
class MyListener: public IListener {
public:
    //....
    //....
    virtual void on_message(const Message &message, bool pm) noexcept {
        //...
    }
};
// NOTE - not complete declaration, see doxygen documentation  
```

The listener can receive messages through the method `on_message`. It receives a `Message` object and `pm` flag which specifies, whether the message is direct (personal) message (Personal Message), if this flag is false, the message was broadcasted to a subscribed channel

The message has following attributes

```
class Message {
public:
    ChannelID get_sender() const {return _sender;}
    ChannelID get_channel() const {return _channel;}
    MessageContent get_content() const {return _content;}
    ConversationID get_conversation() const {return _cid;}
```
* **sender** - ID of sender, it can be used as **channel** name to post direct messages. Anonymous messages have "" as sender
* **channel** - ID of channel where message was broadcasted. Personal messages have ID of received (this listener)
* **content** - content of message. 
* **conversation** - Conversation ID (UINT), this is a free to use number to distinguish different conversations within a communication

**NOTE** - routing informations (sender and channel) should be UTF-8 strings. The message content can be binary. The strings are transfered as octet-stream with no additional encoding. However the javascript client is able to work only with a binary content

### Listening using the callback

```
#include <zerobus/bus.h>
#include <zerobus/client.h>

int main() {
    auto bus = zerobus::Bus::create();      //now we have a bus instance
    
    zerobus::ClientCallback client(bus, [&](zerobus::AbstractClient &c, const zerobus::Message &msg, bool pm) {
            //c - this client
            //msg - received message
            //pm - whether personal message
    });
    //....   
}




## Extending a bus over network

To connect buses of two applications, you need select which application is **server** and which **client**. This distinction is only used to determine how the initial connection will be made, i.e. who will connect and where.

The **server** application uses `BridgeTCPServer`. The **client** application uses `BridgeTCPClient`

```
//SERVER
int main() {
    auto bus = zerobus::Bus::create();      //now we have a bus instance
    zerobus::BridgeTCPServer(bus, "localhost:12345");   //address:port
    ...
    ...
}
```

```
//CLIENT
int main() {
    auto bus = zerobus::Bus::create();      //now we have a bus instance
    zerobus::BridgeTCPClient(bus, "localhost:12345");   //address:port
    ...
    ...
}
```

You only need to keep the above instances to keep connection active. This connects two applications into single bus, where node (listener) from one application can communicate with node (listener) in other application. 

There is no limit how many connections are connected to the server. There is also no limits how many bridges can extends the bus instance in each application. 

**NOTE**: **Avoid cycles!** The `zerobus` is able to detect cycle and solve such situation somehow, but such solution is never ideal. The `zerobus` is not ready fo cycles is connection topology. 

### Examples of topologies


Star

```
                        ┌───────────┐
               ┌───────►┤  server   ├◄──────┐
               │        └─────┬─────┘       │
               │              │             │
               │         ┌────┴───┐         │
               │       ◄─┤  bus   ├─►       │
               │         └┬──┬───┬┘         │
               │          ▼  ▼   ▼          │
               ▼                            ▼
          ┌────┴─────┐                 ┌────┴─────┐
          │ client1  │                 │ client2  │
          └────┬─────┘                 └────┬─────┘
               │                            │
           ┌───┴────┐                   ┌───┴────┐
        ◄──┤  bus   ├─►               ◄─┤  bus   ├─►
           └─┬───┬──┘                   └┬─────┬─┘
             ▼   ▼                       ▼     ▼
```


Kaskade

```
                                      ┌───────────┐
                             ┌───────►┤   server  ├◄──────┐
                             │        └─────┬─────┘       │
                             │              │             │
                             │         ┌────┴───┐         │
                             │       ◄─┤  bus   ├─►       │
                             │         └┬──┬───┬┘         │
                             │          ▼  ▼   ▼          │
                             ▼                            ▼
                        ┌────┴─────┐                 ┌────┴─────┐
                        │  client1 │                 │  client2 │
                        └────┬─────┘                 └────┬─────┘
                             │                            │
                         ┌───┴────┐                   ┌───┴────┐
                      ◄──┤  bus   ├─►               ◄─┤  bus   ├─►
                         └───┬────┘                   └┬─────┬─┘
                        ┌────┴──────┐                  ▼     ▼
               ┌───────►┤   server  ├◄──────┐
               │        └───────────┘       │
               │                            │
               │                            │
               │                            │
               │                            │
               │                            │   
               ▼                            ▼
          ┌────┴─────┐                 ┌────┴─────┐
          │  client1 │                 │  client2 │
          └────┬─────┘                 └────┬─────┘
               │                            │
           ┌───┴────┐                   ┌───┴────┐
        ◄──┤  bus   ├─►               ◄─┤  bus   ├─►
           └─┬───┬──┘                   └┬─────┬─┘
             ▼   ▼                       ▼     ▼
```

Bridge between starts


```
                        ┌───────────┐                              ┌────┴──────┐
               ┌───────►┤   server  ├◄──────┐             ┌───────►┤   server  ├◄──────┐
               │        └─────┬─────┘       │             │        └───────────┘       │
               │              │             │             │                            │
               │         ┌────┴───┐         │             │                            │
               │       ◄─┤  bus   ├─►       │             │                            │
               │         └┬──┬───┬┘         │             │                            │
               │          ▼  ▼   ▼          │             │                            │
               ▼                            ▼             ▼                            ▼
          ┌────┴─────┐                 ┌────┴─────┐  ┌────┴─────┐                 ┌────┴─────┐
          │  client1 │                 │  client2 │  │  client1 │                 │  client2 │
          └────┬─────┘                 └────┬─────┘  └────┬─────┘                 └────┬─────┘
               │                            │  ┌────────┐ │                            │
           ┌───┴────┐                       └──┤  bus   ├─┘                        ┌───┴────┐
        ◄──┤  bus   ├─►                        └┬──┬───┬┘                        ◄─┤  bus   ├─►
           └┬─────┬─┘                           ▼  ▼   ▼                           └┬─────┬─┘
            ▼     ▼                                                                 ▼     ▼
```


## Direct messages

Sending direct messages is easy, just send a message to the sender ID as a reply. This message will then arrive as a personal message directly to the original sender's node. They can then send a reply back to the original recipient and this way you can communicate back and forth as many times as you like

Only a node that knows the ID from the "sender" field can send a direct message. If the ID is obtained by any other method, the message may not be delivered.

Typical use is for RPC. The server listens for RPC requests on the selected channel and responds by sending direct messages to the request senders


```
    //RPC ping service
    int main() {
        auto bus = Bus::create();
        ClientCallback rpc_ping(bus, [](AbstractClient &c, const Message &msg, bool ){
            c.send_message(msg.get_sender(), msg.get_content(), msg.get_conversation());
        });
        rpc_ping.subscribe("ping"); //subscribe to channel "ping"
    
     //...
     //...   
        
    }
```

The above client acts as RPC server which responds with content of the request (ping). The sender receives response as personal message

In order to easily link requests and responses, it is possible to use `conversation_id`. The RPC server typically gets this number from the request and includes it in the response. This allows the client to number the requests and then associate responses with them

- `c.send_message(msg.get_sender(), msg.get_content(), `**msg.get_conversation()**`)`;



## Groups

Groups allow messages from a single source to be sent to multiple recipients, as needed in the publisher-subscriber pattern.

Unlike channels, groups cannot be subscribed to and messages cannot be sent to them. The owner adds the recipient to the group and is the only one who can send messages to the group. The recipient can only unsubscribe from the group.

To add recipient to a group, the owner of group must call

```
bus.add_to_group (owner, group_name, recipient_id);
```

To send message to the group, you need to call

```
bus.send_message(owner, group_name, message)
```


In order to obtain a `recipient_id`, a request for a future recipient must first be received, as with the RPC. You use `sender_id` as recipient id. The initial request can be like request to a subscribe into group. The owner can for example check authorization of such request

To close group, the owner need to call 

```
bus.close_group(owner, group_name);
```

The recepient can unsubscribe self by calling

```
bus.unsubscribe(recipient, group_name)
```

## Before client is destroyed

Every client should unsubscribe from all channels. This is performed by function 

```
bus.unsubscribe_all(client)
```
This removes the client from all channels, groups and closes its personal channel. After this function, the client can be destroyed

The `AbstractClient` and `ClientCallback` do this in destructor automatically

## The protocol

The `zerobus` uses **WebSocket** as underlying protocol. It uses `binary` messages that are exchanged between server and client. 

 - each message has the first byte as **message type**
 - following bytes depends on message type
 
there are several message types defined


### Message type 0xFF - Message Packet

A message sent from one node to other

Contains: 
- conversation_id: uint
- sender: string
- channel: string
- content: string

(see `serialization rules` below)
    
### Message type 0xFE - Channels (replace)

List of channels listened by other side. Replace any existing subscribtion by new list

Contains: 
- count of channels: uint
- channels...: array of string

(see `serialization rules` below)


### Message type 0xFD - Channels (add)

List of channels listened by other side. Add new channels to existing list

Contains: 
- count of channels: uint
- channels...: array of string

(see `serialization rules` below)


### Message type 0xFC - Channels (erase)

List of channels listened by other side. Unsubscribe specified list of channels

Contains: 
- count of channels: uint
- channels...: array of string

(see `serialization rules` below)


### Message type 0xFB - Reset

Sent by other side that they has been unsubscribed from all channels. Mine side should
use 0xFE message to refresh list of channels

This message has no extra arguments

```
   |                    |
   * -----> RESET ----->|
   |                    |
   |< Channels replace -*
   |                    |
```

### Message type 0xFA - Clear Path 

Sent from recepient's node when recipient is no longer available (has been destroyed)

Contains: 
- sender id: string
- recepient id: string

(see `serialization rules` below)
    

### Message type 0xF9 - Add to group 

Sent from group owner to recepient's node when recepient is added to a multicast group

Contains:
- group name : string
- recepient id: string

(see `serialization rules` below)


### Message type 0xF8 - Close group

Sent from group owner to whole group when group is closed

Contains:
- group name : string

(see `serialization rules` below)

### Serialization rules

#### serialization UINT

```
+-------------------------------+-------------------------------+
| L | L | L | N | N | N | N | N | N | N | N | N | N | N | N | N |
+-------------------------------+-------------------------------+
```
L - count of additional bytes
N - UINT number - big-endian

Example: 12345h -> 41 23 45

#### serialization STRING

- length: UINT
- content: bytes[length]

Example: `Hello` -> 05 'H' 'e' 'l' 'l' 'o' 

#### serialization or multiple arguments and arrays

There is no separator, one arguments follow other, array elements are placed one right after the other


## Filters

Filters allows to filter messages by channel/group name if they are passed through bridges. Each bridge can have one filter.

The filter can specify filter rules for incoming and outgoing messages.


```
class Filter {
public:
    
    //incoming messages
    virtual bool on_incoming(ChannelID id); 
    
    //outgoing messages
    virtual bool on_outgoing(ChannelID id); 
    
    //incoming add to group message
    virtual bool on_incoming_add_to_group(ChannelID group_name, ChannelID target_id);
    
    //outgoing add to group message
    virtual bool on_outgoing_add_to_group(ChannelID group_name, ChannelID target_id);
    
    //incoming close group message
    virtual bool on_incoming_close_group(ChannelID group_name);
    
    //outgoing close group message
    virtual bool on_outgoing_close_group(ChannelID group_name);

};

