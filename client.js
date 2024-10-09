const zerobus = (function(){

    function generateUID(prefix) {
      // Zkombinování aktuálního času s náhodným číslem a konverze na řetězec
      return prefix+Date.now().toString(36) + Math.random().toString(36).substr(2, 9);
    }


    const MessageType = {
        message: 0,               // a message
        channels_replace: 1,       // list of channels
        channels_add: 2,           // list of channels
        channels_erase: 3,         // list of channels
        channels_reset: 4,         // send from other side that they unsubscribed all channels
        clear_path: 5,             // clear return path
        add_to_group: 6,
        close_group: 7,
        welcome: 8,                // notifies, successful login (it also assumes channels_reset in case of re-login)
        auth_req: 9,               // sent by one side to request authentication
        auth_response: 10,          // sent by other side to response authentication
        auth_failed: 11             // authentication failed - client should close connection
    };

    class Message {
        #sender;
        #channel;
        #content;
        #conversation_id
        #str;
        #bin;
        #json;

        constructor(sender, channel, content, conversation_id) {
            if (!conversation_id) conversation_id = 0;
            if (!channel || typeof channel != "string"
                || (typeof content != "string" && typeof content != "object")
                || typeof conversation_id != "number") throw TypeError();
            this.#sender = sender;
            this.#channel = channel;
            this.#content = content;
            this.#conversation_id = conversation_id;
        }

        get_sender() {
            return this.#sender;
        }

        get_channel() {
            return this.#channel;
        }

        get_content() {
            return this.#content;
        }

        get_conversation_id() {
            return this.#conversation_id;
        }

        get_string() {  /* retrieve content as string */
            if (this.#str === undefined) {
                if (typeof this.#content == "string") return this.#content;
                if (this.#content instanceof Uint8Array) {
                    const dec = new TextDecoder();
                    this.#str = dec.decode(this.#content);
                } else if (typeof this.#content == "object") {
                    this.#str = JSON.stringify(this.#content);
                }
            }
            return this.#str;
        }
        get_binary() { /* retrieve content as binary */
            if (this.#bin === undefined) {
                if (this.#content instanceof Uint8Array) return this.#content;
                const dec = new TextEncoder();
                if (typeof this.#content == "object") {
                    this.#str = JSON.stringify(this.#content);
                    this.#bin = dec.encode(this.#str);
                } else if (typeof this.#content == "string") {
                    this.#bin = dec.encode(this.#content);
                }
            }
            return this.#bin;
        }

        get_object() { /* retrieve content as object (json) */
            if (this.#json == undefined){
                if (typeof this.#content == "string") {
                    this.#json = JSON.parse(this.#content);
                } else if (this.#content instanceof Uint8Array) {
                    const dec = new TextDecoder();
                    this.#str = dec.decode(this.#content);
                    this.#json = JSON.parse(this.#str);
                } else {
                    this.#json = this.#content;
                }
            }
            return this.#json;
        }


    }

    class BinBuilder {
        #buffer
        #pos
        constructor(initial_capacity = 32) {
            this.#buffer = new Uint8Array(initial_capacity);
            this.#pos = 0;
        }

        #check_size(need_store) {
            if (this.#pos + need_store > this.#buffer.byteLength) {
                let newcapa = Math.max(this.#pos + need_store, this.#pos * 2);
                let newbuff = new Uint8Array(newcapa);
                newbuff.set(this.#buffer);
                this.#buffer = newbuff;
            }
        }

        push(uint8) {
            this.#check_size(1);
            this.#buffer[this.#pos] = uint8;
            ++this.#pos;
        }

        append(uint8_array) {
            this.#check_size(uint8_array.byteLength);
            this.#buffer.set(uint8_array, this.#pos);
            this.#pos += uint8_array.byteLength;
        }

        get_data() {
            return this.#buffer.slice(0, this.#pos);
        }

        get_data_and_clear() {
            const r = this.get_data();
            this.#pos = 0;
            return r;
        }

        clear() {
            this.#pos = 0;
        }
    }

    class BinIterator {
        buffer;
        offset;
        constructor(uint8_buffer, offset) {
            this.buffer = uint8_buffer;
            this.offset = !offset?0:offset;
        }
        get() {
            return this.buffer[this.offset];
        }
        next() {
            ++this.offset;
        }
        at_end() {
            return this.offset >= this.buffer.byteLength;
        }
    }


    class LocalBus {

        #channels = {};
        #backpath = {}
        #mailboxes = new Map();
        #monitors = new Set();
        #notify = false;

        subscribe(channel, listener) {
            this.#check_listener(listener);
            if (!(channel in this.#channels)) {
                this.#channels[channel] = new Set();
            }
            this.#channels[channel].add(listener);
            this.#notify_monitors();

        }
        unsubscribe(channel, listener) {
            if (!(channel in this.#channels)) return;
            this.#channels[channel].delete(listener);
            if (this.#channels[channel].size == 0) {
                delete this.#channels[channel];
                this.#notify_monitors();
            }
        }


        unsubscribe_private(listener) {
            if (this.#mailboxes.has(listener)) {
                const name = this.#mailboxes.get(listener);
                this.#mailboxes.delete(name);
                this.#mailboxes.delete(listener);
                this.#notify_monitors();
            }
        }

        unsubscribe_all(listener) {
            this.unsubscribe_private(listener);
            this.unsubscribe_all_channels(listener);
            this.close_all_groups(listener);
            Object.keys(this.#backpath).forEach(x=>{
                if (this.#backpath[x] == listener) delete this.#backpath[x];
            });
        }

        unsubscribe_all_channels(listener){
            const chls = Object.keys(this.#channels);
            chls.forEach(name=>{
                this.unsubscribe(name, listener);
            });
        }

        close_all_groups(listener){
            const chls = Object.keys(this.#channels);
            chls.forEach(name=>{
                if (this.#channels[name]._owner == listener) {
                    delete this.#channels[name];
                }
            });
        }

        send_message(listener, channel, content, conversation_id) {
            const mbxname = this.#get_mbx(listener);
            return this.#post_message(listener,new Message(mbxname, channel, content, conversation_id));
        }

        #post_message(lsn,msg) {
            const chan = msg.get_channel();
            if (this.#mailboxes.has(chan)) {
                const lsn = this.#mailboxes.get(chan);
                queueMicrotask(()=>{lsn.call(lsn,msg,true);});
                return true;
            }  else if (this.#channels[chan]) {
                let ch = this.#channels[chan];
                if (!ch._owner || ch._owner == lsn) {
                    this.#channels[chan].keys().forEach(entry=>{
                        if (entry != lsn) {
                            queueMicrotask(()=>{entry.call(entry,msg,false);});
                        }
                    });
                    return true;
                }
            } else if (this.#backpath[chan]) {
                const lsn = this.#backpath[chan];
                queueMicrotask(()=>{lsn.call(lsn,msg,false);});
                return true;
               }
            return false;
        }

        #get_mbx(listener) {
            if (!listener) return "";
            this.#check_listener(listener);
            if (this.#mailboxes.has(listener)) {
                return this.#mailboxes.get(listener);
            } else {
                const mbxname = generateUID("!mbx_");
                this.#mailboxes.set(listener, mbxname);
                this.#mailboxes.set(mbxname, listener);
                return mbxname;
            }
        }

        #check_listener(lsn) {
            if (typeof lsn != "function"){
                throw TypeError();
            }
        }


        get_random_channel_name(prefix) {
            return generateUID(prefix);
        }

        is_channel(channel) {
            if (channel in this.#channels) {
                return this.#channels.size() > 0;
            }
        }
        get_subscribed_channels(listener) {
            const chls = Object.keys(this.#channels);
            return chls.filter(name=>{
                return this.#channels[name].has(listener)
            });
        }

        register_monitor(mon) {
            this.#check_listener(mon);
            this.#monitors.add(mon);
        }

        unregister_monitor(mon){
            this.#monitors.delete(mon);
        }

        #notify_monitors() {
            if (this.#notify) return;
            this.#notify = true;
            queueMicrotask(()=>{
                this.#notify = false;
                this.#monitors.forEach(m=>m.call(m)) ;
            });
        }

        get_active_channels(listener) {
            let ch = Object.keys(this.#channels);
            return ch.filter(x=>{
                const s =this.#channels[x];
                return !s._owner && (s.size>1 || !s.has(listener));
            })
        }

        dispatch_message(listener, message, subscribe_return_path) {
            const sender = message.get_sender();
            if (subscribe_return_path && !this.#mailboxes.has(sender) && listener.bridge) {
                this.#backpath[sender] = listener;
            }
            return this.#post_message(listener,message);
        }
        clear_return_path(bridge, sender, receiver) {
            if (this.#backpath[receiver] === bridge) {
                delete this.#backpath[receiver];
                if (this.#backpath[sender]) {
                    this.#backpath[sender].bridge.send_clear_path(sender, receiver);
                }
                return true;
            }
            if (this.#mailboxes.has(sender)) {
                let lsn = this.#mailboxes.get(sender);
                if (lsn.bridge) lsn.bridge.send_clear_path(sender, receiver);
                return true;
            }
            return false;

        }
        add_to_group(owner, group, target_id) {
            let lsn;
            if (this.#mailboxes.has(target_id)) {
                lsn = this.#mailboxes.get(target_id);
            } else if (this.#backpath[target_id]) {
                lsn = this.#backpath[target_id];
            }
            if (!lsn) return false;
            let chan = this.#channels[group];
            if (!chan) {
                this.#channels[group] = chan = new Set();
            }
            if (chan._owner && chan._owner != owner) return false;
            chan._owner = owner;
            chan.add(lsn);
            if (lsn.bridge) lsn.bridge.send_add_to_group(group, target_id);
            return true;
        }
        close_group(owner, group) {
            let chan = this.#channels[group];
            if (chan._owner === owner) {
                chan.keys().forEach(entry=>{
                   if (entry.bridge) entry.bridge.send_close_group(group);
                });
                delete this.#channels[group];
            }
        }
    }


    class PureVirtualError extends Error {
        constructor() {
            super("Pure virtual method call");
            this.name = "PureVirtualError";
        }
    }

    class AbstractBridge {

        _bus;
        _listener;
        _mon;
        #cur_channels = new Set();


        constructor(bus) {
            this._bus = bus;
            this._listener = msg=>{
                this.send_message(msg);
            };
            this._listener.bridge = this;
        }

        send_reset() {throw new PureVirtualError();}
        send_channels(channels, operation) {throw new PureVirtualError();}
        send_message(msg) {throw new PureVirtualError();}
        send_clear_path(sender, receiver) {throw new PureVirtualError();}
        send_add_to_group(group, target) {throw new PureVirtualError();}
        send_close_group(group) {throw new PureVirtualError();}

        send_mine_channels() {
            const chansarr = this._bus.get_active_channels(this._listener);
            if (this.#cur_channels.size == 0) {
                if (chansarr.length == 0) return;
                this.send_channels(chansarr, MessageType.channels_replace);
                this.#cur_channels = new Set(chansarr);
            } else {
                const chans = new Set(chansarr);
                const added = Array.from(chans.difference(this.#cur_channels));
                const removed = Array.from(this.#cur_channels.difference(chans));
                if (added.length) this.send_channels(added,MessageType.channels_add);
                if (removed.length) this.send_channels(removed,MessageType.channels_erase);
                this.#cur_channels = chans;
            }
        }

        apply_their_channels(channels, operaton) {
            if (operaton == MessageType.channels_add) {
                channels.forEach(ch=>this._bus.subscribe(ch, this._listener));
            } else if (operaton == MessageType.channels_erase) {
                channels.forEach(ch=>this._bus.unsubscribe(ch, this._listener));
            } else if (operaton == MessageType.channels_replace) {
                this._bus.unsubscribe_all_channels(this._listener);
                channels.forEach(ch=>this._bus.subscribe (ch, this._listener));
            }
        }

        apply_their_reset() {
            this.#cur_channels.clear();
            this.send_mine_channels();
        }

        apply_their_clear_path(sender, receiver) {
            this._bus.clear_return_path(this._listener,sender,receiver);
        }
        apply_their_add_to_group(group, target) {
            this._bus.add_to_group(this._listener, group, target);
        }
        apply_their_close_group(group) {
            this._bus.close_group(this._listener, group);
        }
        register_monitor() {
            if (!this._mon) {
                this._mon= ()=>{
                        this.send_mine_channels();
                }
                this._bus.register_monitor(this._mon)
            }
        }

    }

    function shift(numb, count) {
        if (count >= 32) {
            numb=Math.floor(numb/4294967296);
            count -= 32;
        }
        return numb >> count;
    }

    function encode_uint(builder, unumb) {
        if (!unumb) {
            builder.push(0);
            return;
        }
        const bits = Math.floor(Math.log2(unumb))+1;
        let bytes = Math.floor((bits+2)/8);
        let idx = 0;
        builder.push((bytes << 5) | shift(unumb , (bytes * 8)));
        while (bytes>0) {
            --bytes;
            ++idx;
            builder.push(shift(unumb , (bytes * 8)) & 0xFF);
        }
    }

    function encode_string(builder,str) {
        if (typeof str == "string") {
            const encoder = new TextEncoder();
            const view = encoder.encode(str);
            encode_uint(builder, view.byteLength);
            builder.append(view);
        } else if (str instanceof Uint8Array) {
            encode_uint(builder, str.byteLength);
            builder.append(str);
        } else {
            throw TypeError();
        }
    }


    function decode_uint(iter) {
        const fb = iter.buffer[iter.offset];
        let numb = fb  & 0x1F;
        let bytes = fb >> 5;
        ++iter.offset;
        while (bytes > 0) {
            numb = (numb * 256) + iter.buffer[iter.offset];
            ++iter.offset;
            --bytes;
        }
        return numb;
    }

    function decode_binary_string(iter) {
        const sz = decode_uint(iter);
        const data = iter.buffer.slice(iter.offset, iter.offset+sz);
        iter.offset += sz;
        return data;
    }

    class WebSocketBridge extends AbstractBridge{

        #ws;
        #url;
        #binb = new BinBuilder;

        constructor(bus, url) {
            super(bus)
            this.#url = url;
            this.#reconnect();
        }


        #reconnect() {
            this._bus.unsubscribe_all(this._listener);
            this.#ws = new WebSocket(this.#url);
            this.#ws.binaryType = 'arraybuffer';
            this.#ws.onerror = this.#reconnect_delayed.bind(this);
            this.#ws.onclose = this.#reconnect.bind(this);
            this.#ws.onmessage = this.#on_message.bind(this);
        }

        #reconnect_delayed() {
            this.#ws.onclose = null;
            console.warn("WebSocketBus: connection error, reconnecting ...");
            setTimeout(()=>this.#reconnect(), 2000);
        }

        #on_message(event) {
            if (!event.data || !(event.data instanceof ArrayBuffer)) return;
            const data = new Uint8Array(event.data);
            let iter = new BinIterator(data);
            const mtype = iter.get();
            iter.next();
            switch (mtype) {
                case MessageType.message: this.#parse_message(iter);break;
                case MessageType.channels_add:
                case MessageType.channels_replace:
                case MessageType.channels_erase: this.#parse_channels(mtype,iter);break;
                case MessageType.add_to_group:this.#parse_add_to_group(iter);break;
                case MessageType.close_group:this.#parse_close_group(iter);break;
                case MessageType.welcome: this.register_monitor()
                                          /* no break */
                case MessageType.channels_reset: this.apply_their_reset();break;
                case MessageType.clear_path: this.#parse_clear_path(iter);break;
                case MessageType.welcome: this.send_mine_channels();break;
            }
        }

        #parse_channels(operation, iter) {
            const dec = new TextDecoder();
            const count = decode_uint(iter);
            let channels = [];
            for (let i = 0; i < count; ++i) {
                const str = dec.decode(decode_binary_string(iter));
                channels.push(str);
            }
            this.apply_their_channels(channels, operation);
        }

        #parse_message(iter){
            const dec = new TextDecoder();
            const cid = decode_uint(iter);
            const sender = dec.decode(decode_binary_string(iter));
            const channel = dec.decode(decode_binary_string(iter));
            const content = decode_binary_string(iter);
            this._bus.dispatch_message(this._listener, new Message(sender,channel, content, cid), true);
        }

        #parse_clear_path(iter) {
            const dec = new TextDecoder();
            const sender = dec.decode(decode_binary_string(iter));
            const receiver  = dec.decode(decode_binary_string(iter));
            this.apply_their_clear_path(sender, receiver);
        }

        #parse_add_to_group(iter) {
            const dec = new TextDecoder();
            const group = dec.decode(decode_binary_string(iter));
            const target  = dec.decode(decode_binary_string(iter));
            this.apply_their_add_to_group(group,target);

        }

        #parse_close_group(iter) {
            const dec = new TextDecoder();
            const group = dec.decode(decode_binary_string(iter));
            this.apply_their_close_group(group);

        }

        send_reset() {
            this.#binb.push(MessageType.channels_reset);
            this.#ws.send(this.#binb.get_data_and_clear());
        }

        send_channels(channels, operation) {
            this.#binb.push(operation);
            encode_uint(this.#binb,channels.length);
            channels.forEach(ch=>encode_string(this.#binb,ch));
            this.#ws.send(this.#binb.get_data_and_clear());
        }
        send_message(msg) {
            this.#binb.push(MessageType.message);
            encode_uint(this.#binb,msg.get_conversation_id());
            encode_string(this.#binb,msg.get_sender());
            encode_string(this.#binb,msg.get_channel());
            encode_string(this.#binb,msg.get_binary());
            this.#ws.send(this.#binb.get_data_and_clear());
        }
        send_clear_path(sender, receiver) {
            this.#binb.push(MessageType.clear_path);
            encode_string(this.#binb,sender);
            encode_string(this.#binb,receiver);
            this.#ws.send(this.#binb.get_data_and_clear());
        }
        send_add_to_group(group, target) {
            this.#binb.push(MessageType.add_to_group);
            encode_string(this.#binb,group);
            encode_string(this.#binb,target);
            this.#ws.send(this.#binb.get_data_and_clear());
        }
        send_close_group(group) {
            this.#binb.push(MessageType.close_group);
            encode_string(this.#binb,group);
            this.#ws.send(this.#binb.get_data_and_clear());
        }
    }


    return {
        Bus: LocalBus,
        Message: Message,
        MessageType:MessageType,
        AbstractBridge: AbstractBridge,
        WebSocketBridge:WebSocketBridge,
    };
})();

