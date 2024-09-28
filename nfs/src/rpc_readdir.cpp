#include "rpc_readdir.h"
#include "nfs_inode.h"
#include "nfs_client.h"

directory_entry::directory_entry(char *name_,
                                 cookie3 cookie_,
                                 const struct stat& attr,
                                 struct nfs_inode* nfs_inode_) :
    cookie(cookie_),
    attributes(attr),
    has_attributes(true),
    nfs_inode(nfs_inode_),
    name(name_)
{
    assert(name != nullptr);
    // Sanity check for attr. Blob NFS only supports these files.
    assert(((attr.st_mode & S_IFMT) == S_IFREG) ||
           ((attr.st_mode & S_IFMT) == S_IFDIR) ||
           ((attr.st_mode & S_IFMT) == S_IFLNK));

    /*
     * inode must have a refcnt held before adding to directory_entry.
     * Every inode referenced from a directory_entry has a dircachecnt
     * reference held. We need the lookupcnt ref to ensure the inode is
     * not freed before we grab the dircachecnt ref.
     * Once dircachecnt ref is held, the caller may choose to drop the
     * lookupcnt ref and dircachecnt ref will correctly prevent the inode
     * from being freed while it's referenced by the directory_entry.
     */
    assert(!nfs_inode->is_forgotten());
    nfs_inode->dircachecnt++;
}

directory_entry::directory_entry(char *name_,
                                 cookie3 cookie_,
                                 uint64_t fileid_) :
    cookie(cookie_),
    has_attributes(false),
    nfs_inode(nullptr),
    name(name_)
{
    assert(name != nullptr);
    // NFS recommends against this.
    assert(fileid_ != 0);

    /*
     * fuse_add_direntry() needs these two fields, so set them.
     * A readdir response doesn't tell us about the filetype (which is what
     * fuse wants to extract from the st_mode fields), so set it to 0.
     */
    ::memset(&attributes, 0, sizeof(attributes));
    attributes.st_ino = fileid_;
    attributes.st_mode = 0;
}

directory_entry::~directory_entry()
{
    AZLogVerbose("~directory_entry({}) called", name);

    if (nfs_inode) {
        assert(nfs_inode->dircachecnt > 0);
        nfs_inode->dircachecnt--;
    }

    assert(name != nullptr);
    ::free(name);
}

readdirectory_cache::~readdirectory_cache()
{
    AZLogInfo("[{}] ~readdirectory_cache() called", inode->get_fuse_ino());

    /*
     * The cache must have been purged before deleting.
     */
    assert(dir_entries.empty());
}

void directory_entry::update_inode(struct nfs_inode *inode)
{
    assert(!nfs_inode);
    *(const_cast<struct nfs_inode**>(&nfs_inode)) = inode;
    inode->dircachecnt++;
}

void readdirectory_cache::set_confirmed()
{
    // Confirmed at time.
    confirmed_msecs = get_current_msecs();

    AZLogDebug("[{}] Marked as confirmed, seq_last_cookie={}, "
               "eof_cookie={}",
               inode->get_fuse_ino(), seq_last_cookie, eof_cookie);
}

void readdirectory_cache::clear_confirmed()
{
    confirmed_msecs = 0;

    AZLogDebug("[{}] Clear confirmed", inode->get_fuse_ino());
}

bool readdirectory_cache::is_confirmed() const
{
    const uint64_t now = get_current_msecs();
    return (confirmed_msecs + (inode->get_actimeo() * 1000)) > now;
}

void readdirectory_cache::set_eof(uint64_t eof_cookie)
{
    // Every directory will at least have "." and "..".
    assert(eof_cookie >= 2);

    eof = true;
    this->eof_cookie = eof_cookie;

    /*
     * If we have seen/cached all cookies right from cookie=1 upto
     * eof_cookie, mark the directory as confirmed.
     */
    if (seq_last_cookie == eof_cookie) {
        set_confirmed();
    } else {
        AZLogDebug("[{}] Marked as NOT confirmed, seq_last_cookie={}, "
                   "eof_cookie={}",
                   inode->get_fuse_ino(), seq_last_cookie, eof_cookie);
        clear_confirmed();
    }
}

bool readdirectory_cache::add(struct directory_entry* entry,
                              bool acquire_lock)
{
    assert(entry != nullptr);
    assert(entry->name != nullptr);
    // 0 is not a valid cookie.
    assert(entry->cookie != 0);

    {
        /*
         * If acquire_lock is true, get exclusive lock on the map for adding
         * the entry to the map. We use a dummy_lock for minimal code changes
         * in the no-lock case.
         * If you call it with acquire_lock=false make sure readdircache_lock
         * is held in exclusive mode.
         */
        std::shared_mutex dummy_lock;
        std::unique_lock<std::shared_mutex> lock(
                acquire_lock ? readdircache_lock : dummy_lock);

        // TODO: Fix this.
        if (cache_size >= MAX_CACHE_SIZE_LIMIT) {
            AZLogWarn("[{}] Exceeding cache max size. No more entries will "
                      "be added to the cache! curent size: {}",
                      this->inode->get_fuse_ino(), cache_size);
            return false;
        }

        if (entry->nfs_inode) {
            /*
             * directory_entry constructor must have grabbed the
             * dircachecnt ref.
             */
            assert(entry->nfs_inode->dircachecnt > 0);

            AZLogDebug("[{}] Adding {} fuse ino {}, cookie {}, to readdir "
                       "cache (dircachecnt {})",
                       this->inode->get_fuse_ino(),
                       entry->name,
                       entry->nfs_inode->get_fuse_ino(),
                       entry->cookie,
                       entry->nfs_inode->dircachecnt.load());
        }

        assert(dir_entries.size() == dnlc_map.size());

        /*
         * If entry->name exists with a different cookie, remove that.
         * Note that caller must have removed entry->cookie but entry->name
         * may exist with another cookie (f.e. added by lookup and then now
         * we are here for readdirplus).
         */
        const cookie3 cookie = filename_to_cookie(entry->name);
        if (cookie != 0) {
            remove(cookie, nullptr, false);
        }

        AZLogDebug("[{}] Adding dir cache entry {} -> {}",
                   this->inode->get_fuse_ino(), entry->cookie, entry->name);

        const auto it = dir_entries.emplace(entry->cookie, entry);

        /*
         * Caller only calls us after ensuring cookie isn't already cached,
         * but since we don't hold the readdircache_lock across removing the
         * old entry and adding this one, it may race with some other thread.
         *
         * TODO: Move the code to remove directory_entry with key
         *       entry->cookie, from readdir{plus}_callback() to here, inside
         *       the lock.
         */
        if (it.second) {
            AZLogDebug("[{}] Adding dnlc cache entry {} -> {}",
                       this->inode->get_fuse_ino(), entry->name,
                       entry->cookie);

            cache_size += entry->get_cache_size();

            /*
             * Also add to the DNLC cache.
             * In the common case the entry must not be present in the DNLC
             * cache, but in case directory changes, same filename must have
             * been seen on a different cookie value earlier.
             * In any case, overwrite that.
             */
            dnlc_map[entry->name] = entry->cookie;

            /*
             * Update seq_last_cookie as long as the sequence of cookies isn't
             * broken.
             * Note that Blob NFS server uses unit incrementing cookies, hence
             * the following check works.
             *
             * For other NFS servers which return arbitrary cookie values, this
             * won't work. Ref: ENABLE_NON_AZURE_NFS
             */
            if (entry->cookie == (seq_last_cookie + 1)) {
                seq_last_cookie = entry->cookie;
            }
        }

        assert(dir_entries.size() == dnlc_map.size());
        return it.second;
    }

    /*
     * TODO: Prune the map for space constraint.
     * For now we will just not add entry into the cache if it is full.
     */
    return false;
}

void readdirectory_cache::dnlc_add(const char *filename,
                                   struct nfs_inode *inode)
{
    assert(filename);
    assert(inode);
    assert(inode->magic == NFS_INODE_MAGIC);

    /*
     * When adding directly to DNLC we use impossible cookie values,
     * starting at UINT64_MAX/2. These cannot occur in READDIR/READDIRPLUS
     * response from the Blob NFS server.
     *
     * TODO: This needs review for supporting other NFS servers.
     *       Ref: ENABLE_NON_AZURE_NFS.
     */
    static uint64_t bigcookie = (UINT64_MAX >> 1);

    std::unique_lock<std::shared_mutex> lock(readdircache_lock);

    /*
     * See the directory_entry update rules in directory_entry comments.
     */
    cookie3 cookie = filename_to_cookie(filename);

    if (cookie != 0) {
        struct directory_entry *de =
            lookup(cookie, nullptr, false /* acquire_lock */);

        if (de->nfs_inode == inode) {
            /*
             * Type (1) or (3) entry already present, with matching nfs_inode,
             * do nothing. Drop the dircachecnt ref held by lookup().
             */
            assert(de->nfs_inode->dircachecnt > 0);
            de->nfs_inode->dircachecnt--;
            return;
        } else if (!de->nfs_inode) {
            /*
             * Type (2) entry present, keep the cookie but add nfs_inode,
             * effectively promoting the entry to type (1).
             */
             assert(!inode->is_forgotten());
             de->update_inode(inode);
             de->attributes = inode->attr;
             return;
        } else {
            /*
             * Stale type (1) or (3) entry present (new nfs_inode doesn't
             * match the saved one), filename has either been renamed or
             * deleted+recreated. We need to delete the old entry and create
             * a new type (3) entry.
             */
            assert(de->nfs_inode->dircachecnt > 0);
            de->nfs_inode->dircachecnt--;
            [[maybe_unused]] const bool found = remove(cookie, nullptr, false);
            assert(found);

            cookie = bigcookie++;
        }
    } else {
        cookie = bigcookie++;
    }

    struct directory_entry *dir_entry =
        new struct directory_entry(strdup(filename), cookie, inode->attr, inode);

    /*
     * dir_entry must have one ref on the inode.
     * This ref will protect the inode while this directory_entry is
     * present in the readdirectory_cache (added below).
     */
    assert(inode->dircachecnt >= 1);

    add(dir_entry, false /* acquire_lock */);
}

struct directory_entry *readdirectory_cache::lookup(
        cookie3 cookie,
        const char *filename_hint,
        bool acquire_lock) const
{
    // Either cookie or filename_hint (not both) must be passed.
    assert((cookie == 0) == (filename_hint != nullptr));

    // Take shared look to see if the entry exists in the cache.
    /*
     * If acquire_lock is true, get shared lock on the map for looking up the
     * entry in the map. We use a dummy_lock for minimal code changes in the
     * no-lock case.
     * If you call it with acquire_lock=false make sure readdircache_lock
     * is held in shared or exclusive mode.
     */
    std::shared_mutex dummy_lock;
    std::shared_lock<std::shared_mutex> lock(
            acquire_lock ? readdircache_lock : dummy_lock);

    if (filename_hint) {
        cookie = filename_to_cookie(filename_hint);
        if (cookie == 0) {
            return nullptr;
        }
    }

    const auto it = dir_entries.find(cookie);

    struct directory_entry *dirent =
        (it != dir_entries.end()) ? it->second : nullptr;

    /*
     * If filename_hint was passed it MUST match the name in the dirent.
     */
    assert(!dirent || !filename_hint ||
           (::strcmp(dirent->name, filename_hint) == 0));

    if (dirent && dirent->nfs_inode) {
        /*
         * When a directory_entry is added to to readdirectory_cache we
         * hold a ref on the inode, so while it's in the cache dircachecnt
         * must be non-zero.
         */
        assert(dirent->nfs_inode->dircachecnt > 0);

        /*
         * Grab a ref on behalf of the caller so that the inode doesn't
         * get freed while the directory_entry is referring to it.
         * Once they are done using this directory_entry, they must drop
         * this ref, mostly done in send_readdir_response().
         */
        dirent->nfs_inode->dircachecnt++;
    }

    return dirent;
}

struct nfs_inode *readdirectory_cache::dnlc_lookup(
        const char *filename,
        bool *negative_confirmed) const
{
    assert(filename != nullptr);

    const struct directory_entry *dirent = lookup(0, filename);

    if (dirent && dirent->nfs_inode) {
        assert(::strcmp(filename, dirent->name) == 0);

        /*
         * lookup() must have held a dircachecnt ref on the inode, drop that
         * but only after holding a fresh lookupcnt ref.
         */
        dirent->nfs_inode->incref();
        assert(dirent->nfs_inode->dircachecnt > 0);
        dirent->nfs_inode->dircachecnt--;
        return dirent->nfs_inode;
    } else if (dirent) {
        /*
         * If dirent is non-null it means the directory_entry was created from
         * READDIR results. It cannot be used to serve a LOOKUP request but we
         * know for sure that file exists. Let the caller know so that they can
         * perform LOOKUP RPC to get the fh and attr details.
         */
        if (negative_confirmed) {
            *negative_confirmed = false;
        }
    } else {
        if (negative_confirmed) {
            *negative_confirmed = is_confirmed();
        }
    }

    return nullptr;
}

bool readdirectory_cache::remove(cookie3 cookie,
                                 const char *filename_hint,
                                 bool acquire_lock)
{
    // Either cookie or filename_hint (not both) must be passed.
    assert((cookie == 0) == (filename_hint != nullptr));

    struct nfs_inode *inode = nullptr;

    {
        /*
         * If acquire_lock is true, get exclusive lock on the map for removing
         * the entry from the map. We use a dummy_lock for minimal code changes
         * in the no-lock case.
         * If you call it with acquire_lock=false make sure readdircache_lock
         * is held in exclusive mode.
         */
        std::shared_mutex dummy_lock;
        std::unique_lock<std::shared_mutex> lock(
                acquire_lock ? readdircache_lock : dummy_lock);

        if (filename_hint) {
            cookie = filename_to_cookie(filename_hint);
            if (cookie == 0) {
                return false;
            }
        }

        const auto it = dir_entries.find(cookie);
        struct directory_entry *dirent =
            (it != dir_entries.end()) ? it->second : nullptr;

        /*
         * Given cookie not found in the cache.
         * It should not happpen though since the caller would call remove()
         * only after checking.
         */
        if (!dirent) {
            return false;
        }

        assert(dirent->cookie == cookie);

        /*
         * Remove the DNLC entry.
         */
        [[maybe_unused]] const int cnt = dnlc_map.erase(dirent->name);
        assert(cnt == 1);

        /*
         * This just removes it from the cache, no destructor is called at
         * this point.
         */
        dir_entries.erase(it);

        inode = dirent->nfs_inode;

        // READDIR created cache entry, nothing more to do.
        if (!inode) {
            delete dirent;
            return true;
        }

        assert(inode->magic == NFS_INODE_MAGIC);

        /*
         * Any inode referenced by a directory_entry added to a
         * readdirectory_cache must have one reference held, by
         * readdirectory_cache::add().
         */
        assert(inode->dircachecnt > 0);

        AZLogDebug("[{}] Removing {} fuse ino {}, cookie {}, from "
                   "readdir cache (lookupcnt={}, dircachecnt={}, "
                   "forget_expected={})",
                   this->inode->get_fuse_ino(),
                   dirent->name,
                   inode->get_fuse_ino(),
                   dirent->cookie,
                   inode->lookupcnt.load(),
                   inode->dircachecnt.load(),
                   inode->forget_expected.load());

        /*
         * If this is the last dircachecnt on this inode, it means
         * there are no more readdirectory_cache,s referencing this
         * inode. If there are no lookupcnt refs then we can free it.
         * For safely freeing the inode against any races, we need to call
         * decref() and for that we need to make sure we have at least one
         * ref on the inode, so we call incref() before deleting the
         * directory_entry. Later below we call decref() to drop the ref
         * held and if that's the only ref, inode will be deleted.
         */
        if (inode->dircachecnt == 1) {
            inode->incref();

            /*
             * This will call ~directory_entry() which will drop the
             * inode's original dircachecnt.
             */
            delete dirent;
        } else {
            delete dirent;
            return true;
        }
    }

    AZLogDebug("[D:{}] inode {} to be freed, after readdir cache remove",
               this->inode->get_fuse_ino(),
               inode->get_fuse_ino());

    /*
     * Drop the extra ref held above. If it's the last ref the inode will be
     * freed.
     */
    assert(inode->lookupcnt > 0);
    inode->decref();

    return true;
}

/*
 * inode_map_lock must be held by the caller.
 */
void readdirectory_cache::clear()
{
    /*
     * TODO: Later when we implement readdirectory_cache purging due to
     *       memory pressure, we need to ensure that any directory which
     *       is currently being enumerated by nfs_inode::lookup_dircache(),
     *       should not be purged, as that may cause those inodes to be
     *       orphanned (they will have lookupcnt and dircachecnt of 0 and
     *       still lying aroung in the inode_map.
     */
    std::vector<struct nfs_inode*> tofree_vec;

    {
        std::unique_lock<std::shared_mutex> lock(readdircache_lock);

        eof = false;
        cache_size = 0;
        ::memset(&cookie_verifier, 0, sizeof(cookie_verifier));

        for (auto it = dir_entries.begin(); it != dir_entries.end(); ++it) {
            struct nfs_inode *inode = it->second->nfs_inode;
            if (inode) {
                assert(inode->magic == NFS_INODE_MAGIC);
                /*
                 * Any inode referenced by a directory_entry added to
                 * a readdirectory_cache must have one reference held,
                 * by readdirectory_cache::add().
                 */
                assert(inode->dircachecnt > 0);

                AZLogDebug("[{}] Removing {} fuse ino {}, cookie {}, from "
                           "readdir cache (dircachecnt {} lookupcnt {}, "
                           "forget_expected {})",
                           this->inode->get_fuse_ino(),
                           it->second->name,
                           inode->get_fuse_ino(),
                           it->second->cookie,
                           inode->dircachecnt.load(),
                           inode->lookupcnt.load(),
                           inode->forget_expected.load());
            }

            /*
             * If this is the last dircachecnt on this inode, it means
             * there are no more readdirectory_cache,s referencing this
             * inode. If there are no lookupcnt refs then we can free it.
             * For safely freeing the inode against any races, we need to call
             * decref() and for that we need to make sure we have at least one
             * ref on the inode, so we call incref() before deleting the
             * directory_entry, and add the inode to a vector which we later
             * iterate over and call decref() for all the inodes.
             */
            if (inode && (inode->dircachecnt == 1)) {
                tofree_vec.emplace_back(inode);
                inode->incref();
            }

            /*
             * This will call ~directory_entry(), which will drop the
             * dircachecnt. Note that we grabbed a lookupcnt ref on the
             * inode so the following decref() will free the inode if that
             * was the only ref.
             */
            delete it->second;
        }

        // For every entry added to dir_entries we add one to dnlc_map.
        assert(dir_entries.size() == dnlc_map.size());

        dir_entries.clear();
        dnlc_map.clear();

        /*
         * No cookies in the cache, hence no sequence.
         */
        seq_last_cookie = 0;
        clear_confirmed();
    }

    if (!tofree_vec.empty()) {
        AZLogDebug("[{}] {} inodes to be freed, after readdir cache purge",
                   this->inode->get_fuse_ino(),
                   tofree_vec.size());
        /*
         * Drop the extra ref we held above, for all inodes in tofree_vec.
         */
        for (struct nfs_inode *inode : tofree_vec) {
            assert(inode->magic == NFS_INODE_MAGIC);
            assert(inode->lookupcnt > 0);

            inode->decref();
        }
    }
}
