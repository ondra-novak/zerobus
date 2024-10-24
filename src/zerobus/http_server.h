#pragma once
#include "network.h"

namespace zerobus {

///custom http server
/** Inherit this class if you want to serve request send to websocket endpoint, but
 * they are not websocket requests
 *
 * The zerobus supports minimal framework for http servers. You will probably need to
 * implement all features of such server.
 */

class IHttpServer {
public:
    virtual ~IHttpServer() = default;


    ///Called when http request is send to websocket endpoint
    /**
     * @param handle handle of this connection.
     * @param ctx network context
     * @param header contains header block (as single string) without ending empty line.
     * The emtpy line is extracted but not included
     * @param body_data contains initial part of body data if there are any.
     * Note that this is beginning of body, if body is large, you will need to fetch rest
     * of the body from the connection.
     *
     * @note it is excepted that function will aquires ownership over the connection. It
     * is responsible to call ctx->destroy() one the connection is no longer needed
     *
     * @note the string content is valid only in context of this function. You must create
     * copy if your function runs asynchronously. This is important especially for
     * coroutines
     */
    virtual void on_request( ConnHandle handle,
            std::shared_ptr<INetContext> ctx,
            std::string_view header, std::string_view body_data) noexcept = 0;

};


}
