#ifndef __NFS_CONNECTION_H__
#define __NFS_CONNECTION_H__

#include "aznfsc.h"
#include "log.h"
#include "nfs_internal.h"

/**
 * This represents one connection to the NFS server.
 * For achieving higher throughput we can have more than one connections to the
 * NFS server, as many as the nconnect config/mount option.
 */
class nfs_connection
{
private:
    /*
     * Every nfs_connection belongs to an nfs_client.
     * This is the nfs_client.
     */
    struct nfs_client *const client = nullptr;

    /*
     * libnfs' nfs_context structure on which the actual API operation happens.
     * This is initialized when the connection is started.
     */
    struct nfs_context *nfs_context = nullptr;

public:
    nfs_connection(struct nfs_client* _client):
        client(_client)
    {
        assert(client != nullptr);
    }

    ~nfs_connection()
    {
        // Must have been closed when destructor is called.
        assert(nfs_context == nullptr);
    }

    /*
     * Returns the libnfs context pointer used by all libnfs APIs.
     */
    struct nfs_context* get_nfs_context()
    {
        return nfs_context;
    }

    /*
     * This should open the connection to the server.
     * It should init the nfs_context, make a libnfs mount call and start a
     * libnfs poll loop on those by calling nfs_mt_service_thread_start(ctx).
     * This will return false if we fail to open the connection.
     */
    bool open();

    /*
     * Close the connections to the server and clean up the structure.
     */
    void close()
    {
        if (nfs_context) {
            nfs_mt_service_thread_stop(nfs_context);
            nfs_destroy_context(nfs_context);
            nfs_context = nullptr;
        }
    }
};

#endif /* __NFS_CONNECTION_H__ */
