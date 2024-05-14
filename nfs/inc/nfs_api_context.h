#pragma once
#include "nfs_client.h"
#include "fuse_optype.h"

class NfsApiContext
{
private:

    // The client for which the context is created.
    // TODO: Since our client is a singleton class, see if we really need to hold this info here.
    NfsClient* client;

    // Fuse request structure.
    // This will be the request structure passed from the fuse layer.
    fuse_req* req;

    // Max number of times the NFS APIs can be retried.
    static int maxErrnoRetries;

    int numOfTimesRetried;


protected:
    // Operation type. This is used only for logging.
    enum fuse_optype optype;

public:

    NfsApiContext(NfsClient* _client, struct fuse_req* _req, enum fuse_optype _optype):
        client(_client),
        req(_req),
        numOfTimesRetried(0),
        optype(_optype)
    {}

    virtual ~NfsApiContext() {};

    static void setMaxErrnoRetries(int maxRetries)
    {
        maxErrnoRetries = maxRetries;
    }

    static int getMaxErrnoRetries()
    {
        return maxErrnoRetries;
    }

    struct nfs_context* GetNfsContext() const
    {
        return client->GetNfsContext();
    }

    struct rpc_context* GetRpcCtx() const
    {
        return nfs_get_rpc_context(GetNfsContext());
    }

    NfsClient* getClient() const
    {
        return client;
    }

    // This method will reply with error and delete the context object.
    void replyError(int rc) {
        if (rc) {
            //logError(rc);
        }

#if 0
        client_->getLogger()->LOG_MSG(
            LOG_DEBUG, "%s(%lu): %d\n", __func__, fuse_get_unique(req_), rc);
#endif

        fuse_reply_err(req, rc);
        delete this;
    }


    /// @brief Check RPC completion for success.
    ///
    /// On failure, retry is set true if the error is
    /// retryable; on non-retryable error, the FUSE request
    /// is completed in error and as a side effect, the
    /// RpcContext object is destroyed.
    bool succeeded(
        int rpc_status,
        int nfs_status,
        bool& retry,
        bool idempotent = true)
    {

        retry = false;
        if (rpc_status != RPC_STATUS_SUCCESS && (numOfTimesRetried < getMaxErrnoRetries()))
        {

#if 0
            client_->getLogger()->LOG_MSG(
                LOG_WARNING,
                "%s: RPC status %d (%lu).\n",
                conn_->describe().c_str(),
                rpc_status,
                fuse_get_unique(req_));
#endif

            // TODO: Should we check here only if the API has exhausted the max possible retries??
            retry = true;
            return false;
        }

#if 0
        if (client_->errorInjection() && idempotent && (numOfTimesRetried < getMaxErrnoRetries())) {
            client_->getLogger()->LOG_MSG(
                LOG_DEBUG,
                "%s: simulating failure for %lu.\n",
                __func__,
                fuse_get_unique(req_));
            failConnection();
            retry = true;
            return false;
        }

        if (client_->errorInjection() && idempotent) {
            // TODO Inject more random errors
            nfs_status = NFS3ERR_ROFS;
        }
#endif

        if (nfs_status != NFS3_OK)
        {
            if (idempotent && (numOfTimesRetried < getMaxErrnoRetries()) && isRetryableError(nfs_status))
            {
                numOfTimesRetried++;

#if 0
                client_->getLogger()->LOG_MSG(
                    LOG_INFO,
                    "%s: Retrying request %lu (attempt %u/%u).\n",
                    __func__,
                    fuse_get_unique(req_),
                    numOfTimesRetried,
                    getMaxErrnoRetries());
#endif

                retry = true;
                return false;
            }
            else
            {
                if (idempotent && numOfTimesRetried >= getMaxErrnoRetries())
                {
#if 0
                    client_->getLogger()->LOG_MSG(
                        LOG_INFO,
                        "%s: Max retry attempts reached, failing operation (%u/%u).\n",
                        __func__,
                        numOfTimesRetried,
                        getMaxErrnoRetries());
#endif
                }
                else if (idempotent && !isRetryableError(nfs_status))
                {
#if 0
                    client_->getLogger()->LOG_MSG(
                        LOG_INFO,
                        "%s: Error #%d is not retryable.\n",
                        __func__,
                        nfs_status);
#endif
                }
            }

            // This will send an error response and will delete the context object.
            // hence the caller should not be accessing this object after this point.
            // TODO: See if this function should be offloaded to the caller.
            replyError(-nfsstat3_to_errno(nfs_status));
            return false; // error occurred.
        }

        return true; // success.
    }

    bool isRetry() const
    {
        return numOfTimesRetried > 0;
    }

    bool isRetryableError(int nfs_status)
    {
        switch (nfs_status)
        {
        case NFS3ERR_IO:
        case NFS3ERR_SERVERFAULT:
        case NFS3ERR_ROFS:
        case NFS3ERR_PERM:
            return true;
        default:
            return false;
        }
    }

    // This is called to send reply of getattr request.
    void replyAttr(const struct stat* attr, double attr_timeout)
    {
        fuse_reply_attr(req, attr, attr_timeout);
        delete this;
    }

    // This should also contain all the methods needed to send reply back to the client as we use low level fuse API calls.
    // Just adding the write method now, other methods should be added.
    void replyWrite(size_t count)
    {
        fuse_reply_write(req, count);
        delete this;
    }

    void replyEntry(const struct fuse_entry_param* e)
    {

        //client_->getLogger()->LOG_MSG(
        //  LOG_DEBUG, "%s(%lu)\n", __func__, fuse_get_unique(req_));

        fuse_reply_entry(req, e);
        delete this;
    }



#if 0 // Move these methods out as and when you implement them.

    virtual void replyBuf(const void* buf, size_t size) {
        finishOperation();
        client_->getLogger()->LOG_MSG(
            LOG_DEBUG, "%s(%lu): %lu\n", __func__, fuse_get_unique(req_), size);
        fuse_reply_buf(req_, (const char*)buf, size);
        delete this;
    }

    void replyEntry(const struct fuse_entry_param* e) {
        finishOperation();
        client_->getLogger()->LOG_MSG(
            LOG_DEBUG, "%s(%lu)\n", __func__, fuse_get_unique(req_));
        fuse_reply_entry(req_, e);
        delete this;
    }

    void replyCreate(
        const struct fuse_entry_param* e,
        const struct fuse_file_info* f) {
        finishOperation();
        client_->getLogger()->LOG_MSG(
            LOG_DEBUG, "%s(%lu)\n", __func__, fuse_get_unique(req_));
        fuse_reply_create(req_, e, f);
        delete this;
    }

    void replyOpen(const struct fuse_file_info* f) {
        finishOperation();
        client_->getLogger()->LOG_MSG(
            LOG_DEBUG, "%s(%lu)\n", __func__, fuse_get_unique(req_));
        fuse_reply_open(req_, f);
        delete this;
    }


    void replyWrite(size_t count) {
        finishOperation();
        client_->getLogger()->LOG_MSG(
            LOG_DEBUG, "%s(%lu): %lu\n", __func__, fuse_get_unique(req_), count);
        fuse_reply_write(req_, count);
        delete this;
    }

    void replyReadlink(const char* linkname) {
        finishOperation();
        client_->getLogger()->LOG_MSG(
            LOG_DEBUG, "%s(%lu): %s\n", __func__, fuse_get_unique(req_), linkname);
        fuse_reply_readlink(req_, linkname);
        delete this;
    }

    void replyStatfs(const struct statvfs* stbuf) {
        finishOperation();
        client_->getLogger()->LOG_MSG(
            LOG_DEBUG, "%s(%lu)\n", __func__, fuse_get_unique(req_));
        fuse_reply_statfs(req_, stbuf);
        delete this;
    }

#endif

    struct fuse_req* getReq() const {
        return req;
    }
};

/// @brief base class for operations which take an inode.
class NfsApiContextInode :  public NfsApiContext
{
public:
    NfsApiContextInode(
        NfsClient* client,
        struct fuse_req* req,
        enum fuse_optype optype,
        fuse_ino_t ino)
        : NfsApiContext(client, req, optype) {
        inode = ino;
    }

    fuse_ino_t getInode() const
    {
        return inode;
    }

private:
    fuse_ino_t inode;
};



/// @brief base class for operations which take a parent inode and a name.
class NfsApiContextParentName : public NfsApiContext {
public:
    NfsApiContextParentName(
        NfsClient* client,
        struct fuse_req* req,
        enum fuse_optype optype,
        fuse_ino_t parent,
        const char* name)
        : NfsApiContext(client, req, optype),
          parentIno(parent)
    {
        fileName = ::strdup(name);
    }

    ~NfsApiContextParentName() override
    {
        ::free((void*)fileName);
    }

    fuse_ino_t getParent() const
    {
        return parentIno;
    }

    const char* getName() const
    {
        return fileName;
    }

private:
    fuse_ino_t parentIno;
    const char* fileName;
};

int NfsApiContext::maxErrnoRetries(3);
