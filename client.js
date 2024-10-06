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
        welcome: 6,                // notifies, successful login (it also assumes channels_reset in case of re-login)
        auth_req: 7,               // sent by one side to request authentication
        auth_response: 8,          // sent by other side to response authentication
        auth_failed: 9             // authentication failed - client should close connection
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
                this.#notify_monitors();
            }
            this.#channels[channel].add(listener);
                        
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
            const chls = Object.keys(this.#channels);
            chls.forEach(name=>{
                this.unsubscribe(name, listener);
            });
            Object.keys(this.#backpath).forEach(x=>{
                if (this.#backpath[x] == listener) delete this.#backpath[x];
            });
        }
                
        send_message(listener, channel, content, conversation_id) {
            const mbxname = this.#get_mbx(listener);
            return this.#post_message(new Message(mbxname, channel, content, conversation_id));
        }
        
        #post_message(msg) {
            const chan = msg.get_channel();
            if (this.#mailboxes.has(chan)) {
                const lsn = this.#mailboxes.get(chan);
                queueMicrotask(()=>{lsn.call(lsn,msg,true);});
                return true;
            } else if (this.#backpath[chan]) {
                const lsn = this.#backpath[chan];
                queueMicrotask(()=>{lsn.call(lsn,msg,false);});
                return true;                
            } else if (this.#channels[chan]) {
                this.#channels[chan].keys().forEach(entry=>{
                    queueMicrotask(()=>{entry.call(entry,msg,false);});
                });
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
                return this.#channels.has(listener)
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
                return s.size>1 || !s.has(listener); 
            })            
        }
        
        dispatch_message(listener, message, subscribe_return_path) {
            const sender = message.get_sender();
            if (subscribe_return_path && !this.#mailboxes.has(sender)) {
                this.#backpath[sender] = listener;
            }
            return this.#post_message(message);
        }
        follow_return_path(sender, callback) {
            if (this.#backpath[sender]) {
                callback(this.#backpath[sender]);
                return true;                
            } 
            return false;
        }        
        clear_return_path(listener, sender) {
            if (this.#backpath[sender] === listener) {
                delete this.#backpath[sender];
                return true;
            }
            return false;
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
        
        send_mine_channels() {
            const chans = new Set(this._bus.get_active_channels(this._listener));
            const added = Array.from(chans.difference(this.#cur_channels));
            const removed = Array.from(this.#cur_channels.difference(chans));
            if (added.length) this.send_channels(added,MessageType.channels_add);
            if (removed.length) this.send_channels(removed,MessageType.channels_erase);
            this.#cur_channels = chans; 
        }
        
        apply_their_channels(channels, operaton) {
            if (operaton == MessageType.channels_add) {
                channels.forEach(ch=>this._bus.subscribe(ch, this._listener));
            } else if (operaton == MessageType.channels_erase) {
                channels.forEach(ch=>this._bus.unsubscribe(ch, this._listener));
            } else if (operaton == MessageType.channels_replace) {
                this._bus.unsubscribe_all(this._listener);
                channels.forEach(ch=>this._bus.subscribe (ch, this._listener));
            }
        }
        
        apply_their_reset() {
            this.#cur_channels.clear();
            queueMicrotask(()=>this.send_mine_channels());   
        }
        
        apply_their_clear_path(sender, receiver) {
            this._bus.clear_return_path(listener, receiver);
            this._bus.follow_return_path(sender, lst=>{
                if (lst.bridge) lst.bridge.send_clear_path(sender, receiver);
            });            
        }
        register_monitor() {
            this._bus.register_monitor(()=>{
                this.send_mine_channels();
            })
        }
                
    }
    
    function shift(numb, count) {
        if (count >= 32) {
            numb=Math.floor(numb/4294967296);
            count -= 32;
        }
        return numb >> count;
    }
    
    function combine_uint8_buffers(/*...*/) {
        let bc = 0;
        for (const b of arguments) {
            bc += b.byteLength;
        }
        let buff =  new Uint8Array(bc);
        bc = 0;        
        for (const b of arguments) {
            buff.set(b, bc);
            bc += b.byteLength;
        }       
        return buff; 
    }
    
    function encode_uint(unumb) {
          if (!unumb) {
                let buff = new Uint8Array(1);
                buff.set(0,0);
                return buff;
            }
          const bits = Math.floor(Math.log2(unumb))+1;
          let bytes = Math.floor((bits+2)/8);
          let buff = new Uint8Array(bytes+1);
          let idx = 0;
          buff[0] = (bytes << 5) | shift(unumb , (bytes * 8));          
          while (bytes>0) {
            --bytes;
            ++idx;
            buff[idx] = (shift(unumb , (bytes * 8)) & 0xFF);            
          }
          return buff;
    }
    
    function encode_string(str, output_array) {
        if (typeof str == "string") {
            const encoder = new TextEncoder();
            const view = encoder.encode(str);
            return combine_uint8_buffers(encode_uint(view.byteLength), view);
        } else if (str instanceof Uint8Array) {
            return combine_uint8_buffers(encode_uint(str.byteLength), str);
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
        #url
        
        constructor(bus, url) {
            super(bus)
            this.#url = url;
            this.#reconnect();     
            this.register_monitor()   
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
            this._bus.unsubscibre_all(this._listener);
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
                case MessageType.welcome:  
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
        
        send_reset() {
            this.#ws.send(new Uint8Array([MessageType.channels_reset]));
        }

        send_channels(channels, operation) {
            let parts = [];
            parts.push(new Uint8Array([operation]));
            parts.push(encode_uint(channels.length));
            channels.forEach(ch=>parts.push(encode_string(ch)));
            this.#ws.send(combine_uint8_buffers.apply(null, parts));            
        }
        send_message(msg) {
            let parts = [];
            parts.push(new Uint8Array([MessageType.message]));
            parts.push(encode_uint(msg.get_conversation_id()));
            parts.push(encode_string(msg.get_sender()));
            parts.push(encode_string(msg.get_channel()));
            parts.push(encode_string(msg.get_binary()));
            this.#ws.send(combine_uint8_buffers.apply(null, parts));
        }
        send_clear_path(sender, receiver) {
            let parts = [];
            parts.push(new Uint8Array([MessageType.clear_path]));
            parts.push(encode_string(sender));
            parts.push(encode_string(receiver));
            this.#ws.send(combine_uint8_buffers.apply(null, parts));
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

