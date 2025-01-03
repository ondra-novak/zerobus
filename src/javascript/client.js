const zerobus = (function(){

    function generateUID(prefix) {
      return prefix+Date.now().toString(36) + Math.random().toString(36).substr(2, 9);
    }

    class PureVirtualError extends Error {
        constructor() {
            super("Pure virtual method call");
            this.name = "PureVirtualError";
        }
    }


    const MessageType = {
        message: 0xFF,               // a message
        channels_replace: 0xFE,       // list of channels
        channels_add: 0xFD,           // list of channels
        channels_erase: 0xFC,         // list of channels
        channels_reset: 0xFB,         // send from other side that they unsubscribed all channels
        no_route: 0xFA,             // clear return path
        add_to_group: 0xF9,
        close_group: 0xF8,
        group_empty: 0xF7,
        new_session: 0xF6,
        update_serial: 0xF5,
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

    class Listener {

        constructor(cb) {
            if (cb) this.on_message = cb.bind(this);
        }

        on_message(message, bPm) {}
        on_no_route(sender, receiver) {}
        on_add_to_group(group_name, target) {}
        on_close_group(group_name)  {}
        on_group_empty(group_name)  {}
    }

    
    class WebSocketReconn {
        #ws;
        #url;
        #cb;
        #send_queue  = [];
        #clear_to_send = false;
        constructor(url, cb) {
            this.#url = url;
            this.#cb = cb;
            this.reconnect();
        }

        reconnect() {            
            this.#ws = new WebSocket(this.#url);
            this.#ws.binaryType = 'arraybuffer';
            this.#ws.onopen = ()=>{
                this.#clear_to_send = true;
                let q = this.#send_queue;
                this.#send_queue = [];
                q.forEach(x=>this.send(x));
            }
            this.#ws.onclose = () => {
                this.#clear_to_send = false;
                setTimeout(()=>this.reconnect(), 2000);                
            }
            this.#ws.onmessage = this.#cb;
        }
        send(msg) {
            queueMicrotask(()=>{
                if (this.#clear_to_send) this.#ws.send(msg);
                else this.#send_queue.push(msg);
            });
        }
        close() {
            this.#ws.onclose = null;
            this.#ws.close();            
        }
    };

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
        #serial = {
            my:generateUID(""),
            current:null,
            source:null
        }

        subscribe(channel, listener) {
            this.#check_listener(listener);
            if (!(channel in this.#channels)) {
                this.#channels[channel] = new Set();
            }
            if (this.#channels[channel].owner) return false;
            this.#channels[channel].add(listener);
            this.#notify_monitors();
            return listener;

        }
        unsubscribe(channel, listener) {
            if (!(channel in this.#channels)) return;
            this.#channels[channel].delete(listener);
            if (this.#channels[channel].size == 0) {
                const owner = this.#channels[channel]._owner;
                delete this.#channels[channel];
                if (owner) {
                    owner.on_group_empty(channel);
                }
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

        unsubscribe_all_channels(listener, and_groups){
            const chls = Object.keys(this.#channels);
            chls.forEach(name=>{
                if (and_groups || this.#channels[name]._owner == null) {
                    this.unsubscribe(name, listener);
                }
            });            
        }

        close_all_groups(listener){
            const chls = Object.keys(this.#channels);
            chls.forEach(name=>{
                if (this.#channels[name]._owner == listener) {
                    this.close_group(name);
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
                queueMicrotask(()=>{lsn.on_message(msg,true);});
                return true;
            }  else if (this.#channels[chan]) {
                let ch = this.#channels[chan];
                if (!ch._owner || ch._owner == lsn) {
                    this.#channels[chan].keys().forEach(entry=>{
                        if (entry != lsn) {
                            queueMicrotask(()=>{entry.on_message(msg,false);});
                        }
                    });
                    return true;
                }
            } else if (this.#backpath[chan]) {
                const lsn = this.#backpath[chan];
                queueMicrotask(()=>{lsn.on_message(msg,false);});
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
                const mbxname = generateUID("mbx_");
                this.#mailboxes.set(listener, mbxname);
                this.#mailboxes.set(mbxname, listener);
                return mbxname;
            }
        }

        #check_listener(lsn) {
            if (!(lsn instanceof Listener)) {
                throw TypeError("The listener must extend zerobus.Listener");
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
            if (typeof mon != "function") throw TypeError("Monitor must be function");
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
            if (subscribe_return_path && !this.#mailboxes.has(sender) && listener) {
                this.#backpath[sender] = listener;
            }
            return this.#post_message(listener,message);
        }
        clear_return_path(bridge, sender, receiver) {            
            if (this.#backpath[receiver] === bridge) {
                delete this.#backpath[receiver];
                if (this.#backpath[sender]) {
                    this.#backpath[sender].on_no_route(sender, receiver);
                }
                return true;
            }
            if (this.#mailboxes.has(sender)) {
                let lsn = this.#mailboxes.get(sender);
                lsn.on_no_route(sender, receiver);
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
            lsn.on_add_to_group(group, target_id);
            return true;
        }
        close_group(owner, group) {
            let chan = this.#channels[group];
            if (chan._owner === owner) {
                chan.keys().forEach(entry=>{
                   entry.on_close_group(group);
                });
                delete this.#channels[group];
            }
        }
        
        set_serial(lsn, serialId) {
            if (!serialId) return;
            const cur_id = this.#serial.source?this.#serial.current:this.#serial.my;
            if (cur_id == serialId) {
                return this.#serial.source == lsn;
            }
            if (cur_id > serialId) {
                this.#serial.current = serialId;
                this.#serial.source = lsn;
            }
            return true;
        }
        get_serial(lsn) {
            if (this.#serial.source) {
                return this.#serial.source != lsn?this.#serial.current:null;
            }
            return this.#serial.my;
        }

    }


    class AbstractBridge extends Listener{

        _bus;
        _mon;
        #cur_channels = new Set();
        #cycle_detect = false;
        #srl = null;


        constructor(bus) {
            super();
            this._bus = bus;
        }

        send_reset() {throw new PureVirtualError();}
        send_channels(channels, operation) {throw new PureVirtualError();}
        send_update_serial(serial) {throw new PureVirtualError();}

        send_mine_channels() {
            const cur_srl = this._bus.get_serial(this);
            if (cur_srl !== this.#srl) {
                this.#srl = cur_srl;
                if (cur_srl) this.send_update_serial(cur_srl);
            }
            const chansarr = this.#cycle_detect?[]:this._bus.get_active_channels(this);
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

        receive_channels(channels, operaton) {
            if (operaton == MessageType.channels_add) {
                channels.forEach(ch=>this._bus.subscribe(ch, this));
            } else if (operaton == MessageType.channels_erase) {
                channels.forEach(ch=>this._bus.unsubscribe(ch, this));
            } else if (operaton == MessageType.channels_replace) {
                this._bus.unsubscribe_all_channels(this);
                channels.forEach(ch=>this._bus.subscribe (ch, this));
            }
        }

        receive_reset() {
            this.#cur_channels.clear();
            this.send_mine_channels();
        }

        receive_no_route(sender, receiver) {
            this._bus.clear_return_path(this,sender,receiver);
        }
        receive_add_to_group(group, target) {
            this._bus.add_to_group(this, group, target);
        }
        receive_close_group(group) {
            this._bus.close_group(this, group);
        }
        receive_group_empty(group) {
            this._bus.unsubscribe(this, group);
        }
        receive_update_serial(serial) {
            const state = this._bus.set_serial(this, serial);
            if (state == this.#cycle_detect) {
                this.#cycle_detect = !this.#cycle_detect;
                this.send_mine_channels();
                if (this.#cycle_detect) {
                    this._bus.unsubscribe_all_channels(this,false);                    
                } else {
                    this.send_reset();
                }                 
            }
                        
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
        #binb = new BinBuilder;            

        constructor(bus, url) {
            super(bus)
            this.#ws = new WebSocketReconn(url,this.#on_message.bind(this));
            this.register_monitor();
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
                case MessageType.channels_reset: this.receive_reset();break;
                case MessageType.no_route: this.#parse_no_route(iter);break;
                case MessageType.group_empty: this.#parse_group_empty(iter);break;
                case MessageType.new_session: this.#parse_new_session(iter);break; 
                case MessageType.update_serial: this.#parse_update_serial(iter);break;

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
            this.receive_channels(channels, operation);
        }

        #parse_message(iter){
            const dec = new TextDecoder();
            const cid = decode_uint(iter);
            const sender = dec.decode(decode_binary_string(iter));
            const channel = dec.decode(decode_binary_string(iter));
            const content = decode_binary_string(iter);
            const b = this._bus.dispatch_message(this, new Message(sender,channel, content, cid), true);
            if (!b) {
                this.on_no_route(sender, channel);
            }
        }

        #parse_no_route(iter) {
            const dec = new TextDecoder();
            const sender = dec.decode(decode_binary_string(iter));
            const receiver  = dec.decode(decode_binary_string(iter));
            this.receive_no_route(sender, receiver);
        }

        #parse_add_to_group(iter) {
            const dec = new TextDecoder();
            const group = dec.decode(decode_binary_string(iter));
            const target  = dec.decode(decode_binary_string(iter));
            this.receive_add_to_group(group,target);

        }

        #parse_close_group(iter) {
            const dec = new TextDecoder();
            const group = dec.decode(decode_binary_string(iter));
            this.receive_close_group(group);
        }

        #parse_group_empty(iter) {
            const dec = new TextDecoder();
            const group = dec.decode(decode_binary_string(iter));
            this.receive_group_empty(group);
        }

        #parse_update_serial(iter) {
            const dec = new TextDecoder();
            const serial = dec.decode(decode_binary_string(iter));
            this.receive_update_serial(serial);
        }
        
        #parse_new_session(iter) {
            const ver = decode_uint(iter);
            this.version = ver;
            this._bus.unsubscribe_all_channels(this,true);
            this.receive_reset();          
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
        send_update_serial(serial) {
            this.#binb.push(MessageType.update_serial);
            encode_string(this.#binb,serial);
            this.#ws.send(this.#binb.get_data_and_clear());            
        }
        on_message(msg) {
            this.#binb.push(MessageType.message);
            encode_uint(this.#binb,msg.get_conversation_id());
            encode_string(this.#binb,msg.get_sender());
            encode_string(this.#binb,msg.get_channel());
            encode_string(this.#binb,msg.get_binary());
            this.#ws.send(this.#binb.get_data_and_clear());
        }
        on_no_route(sender, receiver) {
            this.#binb.push(MessageType.no_route);
            encode_string(this.#binb,sender);
            encode_string(this.#binb,receiver);
            this.#ws.send(this.#binb.get_data_and_clear());
        }
        on_add_to_group(group, target) {
            this.#binb.push(MessageType.add_to_group);
            encode_string(this.#binb,group);
            encode_string(this.#binb,target);
            this.#ws.send(this.#binb.get_data_and_clear());
        }
        on_close_group(group) {
            this.#binb.push(MessageType.close_group);
            encode_string(this.#binb,group);
            this.#ws.send(this.#binb.get_data_and_clear());
        }
        on_group_empty(group) {
            this.#binb.push(MessageType.group_empty);
            encode_string(this.#binb,group);
            this.#ws.send(this.#binb.get_data_and_clear());
        }
    }


    return {
        Bus: LocalBus,
        Message: Message,
        Listener:Listener,
        WebSocketReconn:WebSocketReconn,
        AbstractBridge: AbstractBridge,
        WebSocketBridge:WebSocketBridge,
    };
})();

