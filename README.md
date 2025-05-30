# ZEROBUS

This library implements a communication system based on nodes called `Bus`, which are interconnected using `Bridge` objects. The end nodes are referred to as `Terminal`.

Individual terminals can communicate with each other by sending messages to communication channels. Messages are broadcast to all terminals subscribed to the given channel. In addition, peer-to-peer communication and multicast group messaging are also supported.

From the perspective of a single terminal, it does not matter where its counterpart is located. It can be in the same program or on the other side of the network. As long as there is a connection between these points—either direct or through transit nodes—the message is always delivered, and a response can be sent back in the opposite direction.

This library is written in C++20.

## Supported platform

- Linux (GCC-14, CLANG-18) - uses linux sockets, epoll, posix_spawn
- Windows (MSC 17.9) - uses WSA Sockets, IOCP, named pipes and CreateProcess (console)

## Supported Communication Patterns

- Many-to-many (broadcast)
- Request-Response
- Publisher-Subscriber
- Round-Robin sending (not yet implemented)

All of these can be implemented within a single network. There is no need to create separate networks for each communication pattern.


## Basic usage

```
#include <zerobus/bus.h>

int main() {
    auto bus = zerobus::Bus::create();      //now we have a bus instance

    auto t1 = bus.new_terminal(callback);   //create terminal

    //...
    //...

    //subscribe terminal
    t1.subscribe("channel_name");

    //broadcast an anonymous message
    bus.send_message(nullptr, "channel_name", "message");

    //broadcast a message and set t1 as sender
    t1.send_message(listener, "other_channel", "message");
```


## Terminal

A terminal is an object implemented as the `Terminal` class. Typically, you need to inherit from this class and implement methods for receiving messages.

Alternatively, you can use the `Bus::new_terminal` function, which allows you to define a terminal using a callback function. This function is then called for various events. To handle different events, you can use `zerobus::overloaded` (or any implementation of the overloaded pattern).


Example of use overloaded

```
    //terminal reverses content of message and sents it back as a response
    auto sn = bus.new_terminal(zerobus::overloaded{
        //when channel message
        [&](Terminal &c, const ChannelMessage &msg){
            std::string s ( msg.get_content());
            rp = msg.get_sender();
            std::reverse(s.begin(), s.end());
            c.send_message(msg.get_sender(), s, msg.get_conversation());
        //when response not delivered
        },[&](Terminal &, const Undelivered &) {
            recvd_error = true;
        }
    });

    //subscribe to listen on channel "reverse"
    sn.subscribe("reverse");
```

# Message

this also covers `DirectMessage` and `ChannelMessage`.

- `Message` generic message with no extra specification
- `DirectMessage` peer-to-peer direct message / reply
- `ChannelMessage` message posted to channel or group, may have more receivers


```
class Message {
public:

    ChannelID sender;
    ChannelID channel;
    MessageContent content;
    ConversationID cid;
    Importance importance;
};
```
* **sender** - ID of sender, it can be used as **channel** name to post direct messages. Anonymous messages have "" as sender. Assumed encoding UTF-8
* **channel** - ID of channel where message was broadcasted. Personal messages have ID of received (this listener). 
Assumed encoding UTF-8
* **content** - content of message. No encoding assumed, it is allowed to be binary. 
* **conversation** - Conversation ID (UINT), this is a free to use number to distinguish different conversations within a communication
* **importance** - defines importance and other flags. There are three levels of importance: `low`, `normal`, `high`. Only high
messages are considered as critical for delivery. Failing to deliver causes disconnecting of the peer. Other messages can be discarded if condition of the network prevents delivery. Low importance messages can be discarded erlier than normal messages. You can also request a notification about such event.

From a programmer's perspective, you should treat objects of the `Message` class as references. The data for these objects is typically allocated in the sender's context. If you need to make a copy of a message, you cannot use the copy constructor, as this would still only copy the reference. For this reason, there is a `Message::copy()` method. Otherwise, the message becomes invalid once the current scope is exited.




## Extending a bus over network

To enable network expansion, there is a mechanism for connecting via bridges (Bridge). A bridge is an entity, typically split into two halves, which are connected through some medium (for example, a TCP connection). Bridges register themselves with the Bus as a terminal, but their main purpose is to forward all messages to their other half. This allows the Bus object to extend its reach to other computers in the network.

Once the connection is established, all Terminals are able to communicate with each other regardless of their physical location. The Bus object, in cooperation with the Bridge, ensures reliable message routing, whether it is direct messages or messages intended for multicast channels and groups.

There are several types of bridges
- `NullBridge` allows to connect two Bus instances in the same process. It doesn't perform serializing and parsing of messages, it directly carries messages between instances
- `DebugNullBridge` allows to peek into protocol and debug it
- `WsBridge` implements bridge server/client over WebSocket protocol. It sends messages as WebSocket binary frames. It also supports encryption by using TLS.
- `ZmqBridge` implements bridge over ZMQ protocol

Example using WsBridge

``` 
    auto master = Bus::create();
    WsBridge server(master, {/* additional configuration */});
    server.bind(address);
    /* client.connect(address) - in client mode */   
```

The `WsBridge` can be used in client or server mode (or both). In client mode, it connects to bridge running
in server mode. It also reconnects if connection is lost. In server mode, the bridge waits and accepts 
connections. You can combine client and server mode, so you can open port as server and also connect to other
bridge as client. Each point-to-point connection is considered as bridge. So it is easy to create server Bus and 
allows other clients to connect it and as result, you have `star network` where each terminal can send message
to any other terminal in any branch of the network

## Cycles

Creating cycles in the network is not recommended, but Zerobus can handle them. If there is a cycle in the network, it is internally disconnected to form an acyclic graph. This applies only to multicast channels. Since it is not guaranteed that the disconnection will be performed with regard to optimal transmission, it is better to avoid cycles so that these issues do not need to be addressed.

However, the network can benefit from cycles if individual terminals advertise their position using the `anounce` function. This function can traverse even a cyclic graph and set up routing so that the shortest path to a given terminal exists. However, this function is limited to unicast routing.

It is possible to create a cyclic graph and activate both `anounce` and multicast channels. While `anounce` will work without problems, for multicast channels a spanning tree of the graph will be created, even though it may not be in the ideal optimal state.


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


Cascade

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
## Communication Tools

- Multicast channels
- Direct messages
- Groups
- Round-robin

### Multicast Channels

Any terminal can subscribe to any channel. If the channel does not exist, it is created (and conversely, if the last participant unsubscribes, the channel is removed).

Any other terminal can send a message to this channel by specifying the channel name as the recipient. The message is broadcast to all terminals subscribed to the channel. The message is not sent to the sender if they are also subscribed to the channel.

### Direct Messages

A direct message can be used when sending a reply, for example as a response to a message sent to a channel. Sending a direct message is simple. Every message has a sender set (unless it is anonymous, then it has no sender). If we send a message and specify the sender of the original message as the recipient, this new message is delivered directly to the original sender—as if it were a reply.

Communication using direct messages can then continue if the recipient sends another reply. The number of direct messages is not limited.

If a terminal knows the address for a direct message—and the condition is that it learned it from the `sender` field—it can send a direct message at any later time, there is no time limit for how long the address is valid. However, if there are many terminals in the network, there may be a limit on the size of routing tables, where old entries are removed if they become full.

### Groups

A group is a restricted channel, meaning it can have multiple recipients but only one sender. Unlike channels, you cannot directly subscribe to a group. Each group has an owner, and it is the owner who must add recipients to the group. Also, only the owner can send messages to the group. However, group members have the right to unsubscribe and leave the group at any time.

The owner can also close the group, which unsubscribes all its members.

### Round-Robin

This is an extension of messaging that allows you to require only a single recipient, even if the recipient is specified as a channel or group name. In this case, the message is sent to only one recipient from the group or channel, and recipients are rotated so that the next such message is sent to a different recipient. It is not possible to deterministically specify which recipient will ultimately receive the message.

This is useful, for example, if channels are used to advertise services and there are multiple terminals providing those services. Then the client wants to ensure that their request is delivered to only one provider, who will respond to the message, regardless of which one is selected. If this extension were not active, all providers would respond.


## The protocol

The `zerobus` uses **WebSocket** as underlying protocol. It uses `binary` messages that are exchanged between server and client.

 - each message has the first byte as **message type**
 - following bytes depends on message type

there are several message types defined

### #0 ChannelReset

To save bandwidth, only changes in subscribed channels are sent. This message requests the other side to clear its record of subscribed channels for this side, because all channels have been unsubscribed here. The other side responds to this request by sending an AddChannels message to re-add the existing channels.

This message has no additional data

### #1 Message

Contains actual message. 
Format: 
    -importance/flags (1 byte)
    -sender (binary string)
    -channel (binary string)
    -content (binary string)
    -conversation id (unsigned number)


### #2 AddChannels

Used to add new channels to the existing subscription list. The message contains the list of channels to be added.

### #3 EraseChannels

Used to remove specified channels from the subscription list. The message contains the list of channels to be removed.

### #4 Announce

Used to announce the presence or position of a terminal in the network, typically for routing purposes.

### #5 Undelivered

Indicates that a message could not be delivered to its intended recipient. Used for error handling and notification.

### #6 AddToGroup

Sent by a group owner to add a recipient to a multicast group. Contains the group name and recipient ID.

### #7 CloseGroup

Sent by a group owner to close a group, unsubscribing all its members. Contains the group name.

### #8 GroupEmpty

Sent to the group owner when the last member leaves the group. Notifies that the group is now empty.

### #9 UpdateSerial

Sent by the master node to update the serial ID of the network. Used for cycle detection and network topology management.

### #10 NewSession

## Serialization

### numbers

Number format:
 - First byte:
    - The 3 highest bits are reserved and specify the number of additional bytes (0–7).
    - The 5 lowest bits contain the lowest 5 bits of the transmitted number.
 - The following bytes (if needed) contain the remaining bits of the number in little-endian order.

 Examples:
 - The number 30 is represented by a single byte (fits in 5 bits).
 - The number 33 is represented by two bytes (the first 5 bits in the first byte, the rest in the next byte).

Schema:

```
+--------+----------------+-------------------+-------------------+
| 7 6 5  | 4 3 2 1 0      | 8-15              | 16-23             |
|--------+----------------+-------------------+-------------------+
| # extra| lowest 5 bits  | next 8 bits       | next 8 bits       |
| bytes  | of number      | (if needed)       | (if needed)       |
+--------+----------------+-------------------+-------------------+
```

Where:
- Bits 7-5 of the first byte: number of extra bytes (0–7)
- Bits 4-0 of the first byte: lowest 5 bits of the number
- 2nd, 3rd, ... bytes: remaining bits of the number in little-endian order (if needed)

Maximum is 2^61

### strings

Strings are transfered as <number><data> where number as encoded as described above

<5>Hello


```
+--------+----------------+------+-------+-------+-------+-------+
| 7 6 5  | 4 3 2 1 0      | 8-15 | 16-23 |       |       |       |
|--------+----------------+------+-------+-------+-------+-------+
|   0    | 0 0 1 0 1      |   H  |   e   |   l   |   l   |   o   |
+--------+----------------+------+-------+-------+-------+-------+
```

### Message

```
+--------+---------------------------------------------------------
|  type  |                      message data
|  (1b)  |                       (varianble)
+--------+---------------------------------------------------------
```