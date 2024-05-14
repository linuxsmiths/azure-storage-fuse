#include "nfs_client.h"
#include "nfs_internal.h"
#include "nfs_api_context.h"

std::atomic<bool> NfsClient::initialized(false);
std::string NfsClient::server("");
std::string NfsClient::exportPath("");
RPCTransport* NfsClient::transport;
NFSFileHandle* NfsClient::rootFh;

#define RSTATUS(r) ((r) ? (r)->status : NFS3ERR_SERVERFAULT)

// The user should first init the client class before using it.
bool NfsClient::Init(
    std::string& acctName,
    std::string& contName,
    std::string& blobSuffix,
    struct mountOptions* opt)
{
    // Check if init() has been called before
    if (!initialized.exchange(true)) {

        GetInstanceImpl(&acctName, &contName, &blobSuffix, opt);

        // Get the RPC transport to be used for this client.
        transport = RPCTransport::GetInstance(opt);

        // This will init the transport layer and start the connections to the server.
        // It returns FALSE if it fails to create the connections.
        if (!transport->start())
        {
            AZLogError("Failed to start the RPC transport.");
            return false;
        }

        // Initialiaze the root file handle
        rootFh = new NFSFileHandle(nfs_get_rootfh(transport->GetNfsContext()) /*, 1  ino will be 1 for root */);
        rootFh->SetInode(1);
        //AZLogInfo("Obtained root fh is {}", rootFh->GetFh());

        return true;
    }

    // Return false if the method is called again.
    return false;
}

/*
 *  The methods below are used to implement the specific nfsv3 APIs.
 */
static void getattrCallback(
    struct rpc_context* /* rpc */,
    int rpc_status,
    void* data,
    void* privateData)
{
    auto ctx = (NfsApiContextInode*)privateData;
    auto res = (GETATTR3res*)data;
    bool retry;

    if (ctx->succeeded(rpc_status, RSTATUS(res), retry))
    {
        struct stat st;
        ctx->getClient()->stat_from_fattr3(
            &st, &res->GETATTR3res_u.resok.obj_attributes);

        // TODO: Set the Attr timeout to a better value.
        ctx->replyAttr(&st, 60/*getAttrTimeout()*/);
    } else if (retry)
    {
        ctx->getClient()->getattrWithContext(ctx);
    }
}

void NfsClient::getattrWithContext(NfsApiContextInode* ctx) {
    bool rpcRetry = false;
    auto inode = ctx->getInode();
    do {
        struct GETATTR3args args;
        ::memset(&args, 0, sizeof(args));
        args.object = GetFhFromInode(inode)->GetFh();


        if (rpc_nfs3_getattr_task(ctx->GetRpcCtx(), getattrCallback, &args, ctx) == NULL)
        {
            // This call fails due to internal issues like OOM etc
            // and not due to an actual error, hence retry.
            rpcRetry = true;
        }

    } while (rpcRetry);
}

void NfsClient::getattr(
    fuse_req_t req,
    fuse_ino_t inode,
    struct fuse_file_info* file)
{
    // TODO: We do not need the file parameter here. Understand why this is being passed.
    auto ctx = new NfsApiContextInode(this, req, FOPTYPE_GETATTR, inode);
    getattrWithContext(ctx);
}

/// @brief add a new inode for the given fh and pass to fuse_reply_entry().
void NfsClient::replyEntry(
    NfsApiContext* ctx,
    const nfs_fh3* fh,
    const struct fattr3* attr,
    const struct fuse_file_info* file,
    // following parameters are purely for debugs.
    const char* caller = nullptr,
    fuse_ino_t parent = 0,
    const char* name = nullptr)
{

    NFSFileHandle* ii;
    if (fh) {
        ii = new NFSFileHandle(fh);
        ii->SetInode((fuse_ino_t)ii);
    } else {
        ii = nullptr;
    }

#if 0
    if (caller && name) {
        ctx->getClient()->getLogger()->LOG_MSG(
            LOG_DEBUG,
            "%s allocated new inode %p for %lu+%s.\n",
            caller,
            ii,
            parent,
            name);
    }
#endif

    fuse_entry_param e;
    memset(&e, 0, sizeof(e));
    stat_from_fattr3(&e.attr, attr);
    e.ino = (fuse_ino_t)(uintptr_t)ii;
    /*
     * TODO: See if we need this.
        e.attr_timeout = attrTimeout;
        e.entry_timeout = attrTimeout;
    */
    if (file) {
        //ctx->replyCreate(&e, file);
    } else {
        ctx->replyEntry(&e);
    }
}

#define REPLY_ENTRY(fh, attr, file) \
  ctx->getClient()->replyEntry(     \
      ctx,                          \
      &(fh),                        \
      &(attr),                      \
      (file),                       \
      __func__,                     \
      ctx->getParent(),             \
      ctx->getName())



void NfsClient::lookupCallback(
    struct NfsApicontext* apiContext,
    int rpc_status,
    void* data,
    void* private_data) {
    auto ctx = (NfsApiContextParentName*)private_data;
    auto res = (LOOKUP3res*)data;
    bool retry;

    if (rpc_status == RPC_STATUS_SUCCESS && RSTATUS(res) == NFS3ERR_NOENT) {
        // Magic special case for fuse: if we want negative cache, we
        // must not return ENOENT, must instead return success with zero inode.
        //
        // See comments in definition of fuse_entry_param in fuse header.
        struct fattr3 dummyAttr;
        ::memset(&dummyAttr, 0, sizeof(dummyAttr));
#if 0
        ctx->getClient()->getLogger()->LOG_MSG(
            LOG_DEBUG,
            "Negative caching failed lookup req (%lu).\n",
            fuse_get_unique(ctx->getReq()));
#endif

        ctx->getClient()->replyEntry(
            ctx,
            nullptr /* fh */,
            // &res->LOOKUP3res_u.resok.obj_attributes.post_op_attr_u.attributes,
            &dummyAttr,
            nullptr, /* file */
            __func__,
            ctx->getParent(),
            ctx->getName());
    } else if (ctx->succeeded(rpc_status, RSTATUS(res), retry)) {
        assert(res->LOOKUP3res_u.resok.obj_attributes.attributes_follow);

        REPLY_ENTRY(
            res->LOOKUP3res_u.resok.object,
            res->LOOKUP3res_u.resok.obj_attributes.post_op_attr_u.attributes,
            nullptr);
    } else if (retry) {
        ctx->getClient()->lookupWithContext(ctx);
    }
}

void NfsClient::lookupWithContext(NfsApiContextParentName* ctx) {
    int rpc_status = RPC_STATUS_ERROR;
    auto parent = ctx->getParent();
    do {
        LOOKUP3args args;
        ::memset(&args, 0, sizeof(args));
        args.what.dir = GetFhFromInode(parent)->GetFh();
        args.what.name = (char*)ctx->getName();

        //  if (ctx->obtainConnection()) {
        //setUidGid(*ctx, false);
        //  rpc_status = rpc_nfs3_lookup_async(
        //     ctx->GetNfsContext(), this->lookupCallback, &args, ctx);

        //  restoreUidGid(*ctx, false);
        //ctx->unlockConnection();
        //}
    } while (shouldRetry(rpc_status, ctx));
}

void NfsClient::lookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
    auto ctx = new NfsApiContextParentName(this, req, FOPTYPE_LOOKUP, parent, name);
    lookupWithContext(ctx);
}

/// @brief translate a NFS fattr3 into struct stat.
void NfsClient::stat_from_fattr3(struct stat* st, const struct fattr3* attr) {
    ::memset(st, 0, sizeof(*st));
    st->st_dev = attr->fsid;
    st->st_ino = attr->fileid;
    st->st_mode = attr->mode;
    st->st_nlink = attr->nlink;
    st->st_uid = attr->uid;
    st->st_gid = attr->gid;
    // TODO: Uncomment the below line.
    // st->st_rdev = makedev(attr->rdev.specdata1, attr->rdev.specdata2);
    st->st_size = attr->size;
    st->st_blksize = NFS_BLKSIZE;
    st->st_blocks = (attr->used + 511) >> 9;
    st->st_atim.tv_sec = attr->atime.seconds;
    st->st_atim.tv_nsec = attr->atime.nseconds;
    st->st_mtim.tv_sec = attr->mtime.seconds;
    st->st_mtim.tv_nsec = attr->mtime.nseconds;
    st->st_ctim.tv_sec = attr->ctime.seconds;
    st->st_ctim.tv_nsec = attr->ctime.nseconds;
    switch (attr->type) {
    case NF3REG:
        st->st_mode |= S_IFREG;
        break;
    case NF3DIR:
        st->st_mode |= S_IFDIR;
        break;
    case NF3BLK:
        st->st_mode |= S_IFBLK;
        break;
    case NF3CHR:
        st->st_mode |= S_IFCHR;
        break;
    case NF3LNK:
        st->st_mode |= S_IFLNK;
        break;
    case NF3SOCK:
        st->st_mode |= S_IFSOCK;
        break;
    case NF3FIFO:
        st->st_mode |= S_IFIFO;
        break;
    }
}


