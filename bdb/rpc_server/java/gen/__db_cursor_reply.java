/*
 * Automatically generated by jrpcgen 0.95.1 on 12/18/01 7:23 PM
 * jrpcgen is part of the "Remote Tea" ONC/RPC package for Java
 * See http://acplt.org/ks/remotetea.html for details
 */
package com.sleepycat.db.rpcserver;
import org.acplt.oncrpc.*;
import java.io.IOException;

public class __db_cursor_reply implements XdrAble {
    public int status;
    public int dbcidcl_id;

    public __db_cursor_reply() {
    }

    public __db_cursor_reply(XdrDecodingStream xdr)
           throws OncRpcException, IOException {
        xdrDecode(xdr);
    }

    public void xdrEncode(XdrEncodingStream xdr)
           throws OncRpcException, IOException {
        xdr.xdrEncodeInt(status);
        xdr.xdrEncodeInt(dbcidcl_id);
    }

    public void xdrDecode(XdrDecodingStream xdr)
           throws OncRpcException, IOException {
        status = xdr.xdrDecodeInt();
        dbcidcl_id = xdr.xdrDecodeInt();
    }

}
// End of __db_cursor_reply.java
