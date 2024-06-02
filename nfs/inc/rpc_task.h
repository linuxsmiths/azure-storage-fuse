#ifndef __RPC_TASK_H__
#define __RPC_TASK_H__

#include <cstddef>
#include <string>
#include <mutex>
#include <stack>
#include <shared_mutex>
#include "nfs_client.h"
#include <vector>

#define MAX_OUTSTANDING_RPC_TASKS 65536

class rpc_task_helper;

struct lookup_rpc_task
{
    lookup_rpc_task() = default;

    void set_file_name(const char* name)
    {
        file_name = (char*)malloc(strlen(name) + 1);
        file_name = strdup(name);
    }

    void set_parent_inode(fuse_ino_t parent)
    {
        parent_inode = parent;
    }

#if 0
    ~lookup_rpc_task()
    {
        ::free((void*)file_name);
    }
#endif

    fuse_ino_t get_parent_inode() const
    {
        return parent_inode;
    }

    const char* get_name() const
    {
        return file_name;
    }

    void free_name()
    {
        ::free((void*)file_name);
    }

private:
    fuse_ino_t parent_inode;
    char* file_name;
};

struct getattr_rpc_task
{
    fuse_ino_t get_inode() const
    {
        return inode;
    }

    void set_inode(fuse_ino_t ino)
    {
        inode = ino;
    }

private:
    fuse_ino_t inode;
};


// This is the context that will be used by the Nfsv3 Setattr API
struct setatt_rpc_task
{
public:
    struct stat* get_attr() const
    {
        return attribute;
    }

    int get_attr_flags_to_set() const
    {
        return to_set;
    }

    fuse_file_info* get_file() const
    {
        return file_ptr;
    }

    fuse_ino_t get_inode() const
    {
        return inode;
    }

    void set_inode(fuse_ino_t ino)
    {
        inode = ino;
    }

    void set_fuse_file(fuse_file_info* fileinfo)
    {
        // The fuse can pass this as nullptr.
        if (fileinfo == nullptr)
            return;
        ::memcpy(&file, fileinfo, sizeof(file));
        file_ptr = &file;
    }

    void set_attribute_and_mask(struct stat* attr, int mask)
    {
        attribute = attr;
        to_set= mask;
    }

private:
    // Inode of the file for which attributes have to be set.
    fuse_ino_t inode;

    // File info passed by the fuse layer.
    fuse_file_info file;
    fuse_file_info* file_ptr;

    // Attributes value to be set to.
    struct stat* attribute;

    // Valid attribute mask to be set.
    int to_set;
};

struct create_file_rpc_task
{
#if 0
    ~create_file_rpc_task()
    {
        ::free((void*)file_name);
    }
#endif
    fuse_ino_t get_parent_inode() const
    {
        return parent_inode;
    }

    const char* get_name() const
    {
        return file_name;
    }

    mode_t get_mode() const
    {
        return mode;
    }

    struct fuse_file_info* get_file() const
    {
        return file_ptr;
    }

    void set_parent_inode(fuse_ino_t parent)
    {
        parent_inode = parent;
    }

    void set_file_name(const char* name)
    {
        file_name = (char*)malloc(strlen(name) + 1);
        file_name = strdup(name);
    }

    void set_mode(mode_t mod)
    {
        mode = mod;
    }

    void set_fuse_file(fuse_file_info* fileinfo)
    {
        assert(fileinfo != nullptr);
        ::memcpy(&file, fileinfo, sizeof(file));
        file_ptr = &file;
    }

    void free_name()
    {
        ::free((void*)file_name);
    }

private:
    fuse_ino_t parent_inode;
    char* file_name;
    mode_t mode;
    struct fuse_file_info file;
    struct fuse_file_info* file_ptr;
    bool is_used;
};

struct mkdir_rpc_task
{
#if 0
    ~mkdir_rpc_task()
    {
        ::free((void*)dir_name);
    }
#endif
    fuse_ino_t get_parent_inode() const
    {
        return parent_inode;
    }

    const char* get_name() const
    {
        return dir_name;
    }

    mode_t get_mode() const
    {
        return mode;
    }

    void set_parent_inode(fuse_ino_t parent)
    {
        parent_inode = parent;
    }

    void set_dir_name(const char* name)
    {
        dir_name = (char*)malloc(strlen(name) + 1);
        dir_name = strdup(name);
    }

    void set_mode(mode_t mod)
    {
        mode = mod;
    }

    void free_name()
    {
        ::free((void*)dir_name);
    }

private:
    fuse_ino_t parent_inode;
    char* dir_name;
    mode_t mode;
    bool is_used;
};

struct readdir_rpc_task
{
public:
    void set_size(size_t sz)
    {
        size = sz;
    }

    void set_offset(off_t off)
    {
        offset = off;
    }

    void set_inode(fuse_ino_t ino)
    {
        inode = ino;
    }

    void set_fuse_file(fuse_file_info* fileinfo)
    {
        // The fuse can pass this as nullptr.
        if (fileinfo == nullptr)
            return;
        ::memcpy(&file, fileinfo, sizeof(file));
        file_ptr = &file;
    }

    void set_cookieverf(const cookieverf3* cokieverf)
    {
        ::memcpy(&cookieverf, cokieverf, sizeof(cookieverf));
    }

    void set_cookie(cookie3 cokie)
    {
        cookie = cokie;
    }

    fuse_ino_t get_inode() const
    {
        return inode;
    }

    cookie3 get_cookie() const
    {
        return cookie;
    }

    const cookieverf3* get_cookieverf() const
    {
        return &cookieverf;
    }

    off_t get_offset() const
    {
        return offset;
    }

    size_t get_size() const
    {
        return size;
    }

private:
    // Inode of the directory.
    fuse_ino_t inode;

    size_t size;

    off_t offset;

    cookie3 cookie;

    cookieverf3 cookieverf;

// File info passed by the fuse layer.
    fuse_file_info file;
    fuse_file_info* file_ptr;
};

struct readdirplus_rpc_task
{
public:
    void set_size(size_t sz)
    {
        size = sz;
    }

    void set_offset(off_t off)
    {
        offset = off;
    }

    void set_inode(fuse_ino_t ino)
    {
        inode = ino;
    }

    void set_fuse_file(fuse_file_info* fileinfo)
    {
        // The fuse can pass this as nullptr.
        if (fileinfo == nullptr)
            return;
        ::memcpy(&file, fileinfo, sizeof(file));
        file_ptr = &file;
    }

    void set_cookieverf(const cookieverf3* cokieverf)
    {
        ::memcpy(&cookieverf, cokieverf, sizeof(cookieverf));
    }

    void set_cookie(cookie3 cokie)
    {
        cookie = cokie;
    }

    fuse_ino_t get_inode() const
    {
        return inode;
    }

    off_t get_offset() const
    {
        return offset;
    }

    cookie3 get_cookie() const
    {
        return cookie;
    }

    const cookieverf3* get_cookieverf() const
    {
        return &cookieverf;
    }

    size_t get_size() const
    {
        return size;
    }

private:
    // Inode of the directory.
    fuse_ino_t inode;

    size_t size;

    off_t offset;

    cookie3 cookie;

    cookieverf3 cookieverf;

    // File info passed by the fuse layer.
    fuse_file_info file;
    fuse_file_info* file_ptr;
};

struct rpc_task
{
    // The client for which the context is created.
    struct nfs_client* client;

    // Fuse request structure.
    // This will be the request structure passed from the fuse layer.
    fuse_req* req;

    // Max number of times the NFS APIs can be retried.
    static int max_errno_retries;

    int num_of_times_retried;

    // This is the index of the object in the rpc_task_list vector.
    int index;

protected:
    // Operation type. This is used only for logging.
    enum fuse_opcode optype;

public:
    union {
        struct lookup_rpc_task lookup_task;
        struct getattr_rpc_task getattr_task;
        struct setatt_rpc_task setattr_task;
        struct create_file_rpc_task create_task;
        struct mkdir_rpc_task mkdir_task;
        struct readdir_rpc_task readdir_task;
        struct readdirplus_rpc_task readdirplus_task;
    } rpc_api;

// TODO: Add valid flag here for APIs?

    // This function is responsible for setting up the members of lookup_task.
    void set_lookup(struct nfs_client* clt,
                    fuse_req* request,
                    const char* name,
                    fuse_ino_t parent_ino);

    // This function is responsible for issuing the lookup call to the server.
    // lookup_task structure should be populated before calling this function
    // by calling set_lookup().
    void run_lookup();

    // This function is responsible for setting up the members of getattr_task.
    void set_getattr(struct nfs_client* clt,
                     fuse_req* request,
                     fuse_ino_t ino);

    // This function is responsible for issuing the getattr call to the server.
    // getattr_task structure should be populated before calling this function
    // by calling set_getattr().
    void run_getattr();

    // This function is responsible for setting up the members of setattr_task.
    void set_setattr(struct nfs_client* clt,
                     fuse_req* request,
                     fuse_ino_t ino,
                     struct stat* attr,
                     int toSet,
                     struct fuse_file_info* file);

    // This function is responsible for issuing the setattr call to the server.
    // setattr_task structure should be populated before calling this function
    // by calling set_setattr().
    void run_setattr();

    // This function is responsible for setting up the members of create_file_task.
    void set_create_file(struct nfs_client* clt,
                         fuse_req* request,
                         fuse_ino_t parent_ino,
                         const char* name,
                         mode_t mode,
                         struct fuse_file_info* file);

    // This function is responsible for issuing the create file call to the server.
    // create_task structure should be populated before calling this function
    // by calling set_create_file().
    void run_create_file();

    // This function is responsible for setting up the members of mkdir_task.
    void set_mkdir(struct nfs_client* clt,
                   fuse_req* request,
                   fuse_ino_t parent_ino,
                   const char* name,
                   mode_t mode);

    // This function is responsible for issuing the mkdir call to the server.
    // mkdir_task structure should be populated before calling this function
    // by calling set_mkdir().
    void run_mkdir();

    // This function is responsible for setting up the members of readdir_task.
    void set_readdir(struct nfs_client* clt,
                     fuse_req* request,
                     fuse_ino_t inode,
                     size_t size,
                     off_t offset,
                     struct fuse_file_info* file);

    // This function is responsible for issuing the readdir call to the server.
    // readdir_task structure should be populated before calling this function
    // by calling set_readdir().
    void run_readdir();

    // This function is responsible for setting up the members of readdirplus_task.
    void set_readdirplus(struct nfs_client* clt,
                         fuse_req* request,
                         fuse_ino_t inode,
                         size_t size,
                         off_t offset,
                         struct fuse_file_info* file);

    // This function is responsible for issuing the readdirplus call to the server.
    // readdirplus_task structure should be populated before calling this function
    // by calling set_readdirplus().
    void run_readdirplus();

    void set_client(struct nfs_client* clt)
    {
        client = clt;
    }

    void set_fuse_req(fuse_req* request)
    {
        req = request;
    }

    void set_op_type(enum fuse_opcode optyp)
    {
        optype = optyp;
    }

    enum fuse_opcode get_op_type()
    {
        return optype;
    }

    static void setmax_errno_retries(int max_retries)
    {
        max_errno_retries = max_retries;
    }

    static int get_max_errno_retries()
    {
        return max_errno_retries;
    }

    struct nfs_context* get_nfs_context() const;

    struct rpc_context* get_rpc_ctx() const
    {
        return nfs_get_rpc_context(get_nfs_context());
    }

    nfs_client* get_client() const
    {
        assert (client != nullptr);
        return client;
    }

    bool get_index() const
    {
        return index;
    }

    // The task should not be accessed after this function is called.
    void free_rpc_task();

    // This method will reply with error and free the rpc task.
    void reply_error(int rc)
    {
        fuse_reply_err(req, rc);
        free_rpc_task();
    }

    void reply_attr(const struct stat* attr, double attr_timeout)
    {
        fuse_reply_attr(req, attr, attr_timeout);
        free_rpc_task();
    }

    void reply_write(size_t count)
    {
        fuse_reply_write(req, count);
        free_rpc_task();
    }

    void reply_entry(const struct fuse_entry_param* e)
    {
        fuse_reply_entry(req, e);
        free_rpc_task();
    }

    void reply_create(
        const struct fuse_entry_param* entry,
        const struct fuse_file_info* file)
    {
        fuse_reply_create(req, entry, file);
        free_rpc_task();
    }

    //
    // Check RPC completion for success.
    //
    // On success, true is returned.
    // On failure, false is returned and \p retry is set to true if the error is retryable else set to false.
    //
    bool succeeded(
        int rpc_status,
        int nfs_status,
        bool& retry,
        bool idempotent = true)
    {
        retry = false;

        if (rpc_status != RPC_STATUS_SUCCESS && (num_of_times_retried < get_max_errno_retries()))
        {
            retry = true;
            return false;
        }

        if (nfs_status != NFS3_OK)
        {
            if (idempotent && (num_of_times_retried < get_max_errno_retries()) && is_retryable_error(nfs_status))
            {
                num_of_times_retried++;
                retry = true;
                return false;
            }

            return false;
        }

        return true; // success.
    }

    bool is_retry() const
    {
        return num_of_times_retried > 0;
    }

    bool is_retryable_error(int nfs_status)
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

    struct fuse_req* get_req() const
    {
        return req;
    }
};

class rpc_task_helper
{
private:
    // Mutex for synchronizing access to free_task_index stack.
    std::shared_mutex task_index_lock;

    // Stack containing index into the rpc_task_list vector.
    std::stack<int> free_task_index;

    // List of RPC tasks which is used to run the task.
    std::vector<struct rpc_task> rpc_task_list;

    // Condition variable to wait for free task index availability.
    std::condition_variable_any cv;

    // This is a singleton class, hence make the constructor private.
    rpc_task_helper()
    {
        // Hold an exclusive lock.
        std::unique_lock<std::shared_mutex> lock(task_index_lock);

        // There should be no elements in the stack.
        assert(free_task_index.empty());

        // Initialize the index stack.
        for (int i=0; i<MAX_OUTSTANDING_RPC_TASKS; i++)
        {
            free_task_index.push(i);
        }

        // There should be MAX_OUTSTANDING_RPC_TASKS index available.
        assert(free_task_index.size() == MAX_OUTSTANDING_RPC_TASKS);

        rpc_task_list.resize(MAX_OUTSTANDING_RPC_TASKS);
    }

public:
    ~rpc_task_helper() = default;

    static rpc_task_helper* get_instance()
    {
        static rpc_task_helper helper;
        return &helper;
    }

    // This returns a free rpc task instance from the pool of rpc tasks.
    // This call will block till a free rpc task is available.
    void get_rpc_task_instance(struct rpc_task** task)
    {
        int free_index = 0;
        const bool got_free_index = get_free_task_index(free_index);

        // The get_free_task_index() function will block till a free index is
        // available, hence we should never see a false return.
        assert(got_free_index);

        assert(free_index < MAX_OUTSTANDING_RPC_TASKS);
        rpc_task_list[free_index].index = free_index;
        *task = &rpc_task_list[free_index];
    }

    bool get_free_task_index(int& free_index)
    {
        free_index = 0;

        // Hold an exclusive lock to fetch the free index.
        std::unique_lock<std::shared_mutex> lock(task_index_lock);

        // Wait for a free rpc task to be available.
        cv.wait(lock, [this] { return !free_task_index.empty(); });

        if (!free_task_index.empty())
        {
            free_index = free_task_index.top();

            free_task_index.pop();

            return true;
        }

        // We should ideally never be coming here as the condition variable
        // waits for the free index to be available.
        return false;
    }

    void release_free_index(int index)
    {
        {
            // Hold an exclusive lock to add the free index.
            std::unique_lock<std::shared_mutex> lock(task_index_lock);
            free_task_index.push(index);
        }

        // Notify that a new free index is available.
        cv.notify_one();
    }

    void free_rpc_task_instance(struct rpc_task* task)
    {
        int index_to_free = task->get_index();
        release_free_index(index_to_free);

        // TODO: See if we need to clear out the struct members here.
    }
};

#endif /* __RPC_TASK_H__ */