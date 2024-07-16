#ifndef __READAHEAD_H__
#define __READAHEAD_H__

#include <atomic>
#include <shared_mutex>

#include "aznfsc.h"

namespace aznfsc {

/**
 * Readahead state for a Blob.
 * This maintains state to track application read pattern and suggests if
 * readahead needs to be performed. Following are some of its properties:
 *
 * 1. Caller can call the get_next_ra() method to find the offset of the next
 *    readahead read it should issue. It'll return 0 if readahead should not be
 *    performed. This it can do by tracking the read IO pattern and turning off
 *    readahead if read pattern observed is not sequential. It can also return
 *    0 when there are already enough ongoing readaheads.
 * 2. It should *never* suggest readahead for the same offset more than once
 *    if the reads are issued at monotonically increasing offsets.
 * 3. It should *never* suggest readahead for the same offset for which a read
 *    has been recently issued, if the reads are issued at monotonically
 *    increasing offsets.
 * 4. Since it tracks read IO pattern, it should be made aware of *all* reads
 *    issued by the application. Also, it should be told when a readahead
 *    completes.
 *
 * TODO: Currently it only tracks a single reader stream. If it's used in a
 *       scope where multiple reader applications are performing reads, it
 *       may not be able to correctly detect sequential patterns, even if all
 *       those multiple reader streams are sequential by themselves.
 *
 *       Another way of achieving the same is for the user to use multiple
 *       ra_state objects, one per reading context, f.e., one way of doing it
 *       is to associate the ra_state with the issuing process' (pid returned
 *       by fuse_get_context()) and not with the file inode.
 *
 * How does pattern detection work?
 * ================================
 * File is divided into 1GB logical sections. Everytime access moves to a new
 * section, pattern tracking variables are reset (this is is skipped for an
 * ongoing sequential access). This is done to make sure we use the most recent
 * accesses to correctly detect the pattern and older accesses do not muddle the
 * pattern detection. Following pattern tracking variables are maintained:
 *
 * - ra_bytes is the amount of readahead in bytes. We never keep more than
 *   ra_bytes of readahead reads ongoing.
 * - min_byte_read and max_byte_read track the min and max bytes read by the
 *   application in the current section. max_byte_read-min_byte_read is called
 *   the access_range.
 * - num_reads and num_bytes_read is the total number of reads and number of
 *   bytes read in the current section, respectively.
 * - If num_reads >= 3 and num_bytes_read/access_range > 0.7, the pattern is
 *   considered sequential. Note that this allows for some reordered reads due
 *   to multiple async reads handled by multiple threads, but at the same time
 *   it marks the pattern sequential only when application is indeed reading
 *   sequentially. Note that random reads or "jumping reads" after a fixed gap
 *   will not qualify for sequential reads.
 * - Readahead windows starts from max_byte_read+1 and is ra_bytes wide.
 * - ra_ongoing is the number of readahead bytes which are still ongoing.
 * - last_byte_readahead is the last byte of readahead read issued, which means
 *   next readahead is issued from last_byte_readahead+1. When max_byte_read
 *   crosses last_byte_readahead, last_byte_readahead is updated to
 *   max_byte_read, so that we never issue readahead for something that's
 *   already read recently.
 * - Pattern tracking is reset when one of the following happens:
 *    - New read from the application lies in a different section than
 *      max_byte_read (and the current access is not sequential). This ensures
 *      our pattern detection is based on recent data and historical accesses
 *      do not have carry influence for long time.
 *    - New read starts after max_byte_read+ra_bytes. Such a large jump in
 *      read offset hints at non-sequential access and hence the access pattern
 *      need to be reviewed again and sequential pattern must be proved afresh.
 * - Following pattern tracking variables are reset:
 *    - min_byte_read
 *    - max_byte_read
 *    - num_reads
 *    - num_bytes_read
 *    - last_byte_readahead
 * - When pattern tracking is reset it'll take at least 3 reads to detect the
 *   pattern again. Till that time we won't recommend any new readaheads.
 *   Previously issued readaheads will continue and ra_ongoing is not reset.
 */
class ra_state
{
public:
    /*
     * Logical section size in bytes.
     * Everytime access moves to a new section pattern detection is reset
     * and access pattern has to prove its sequential-ness again.
     */
    const uint64_t SECTION_SIZE = (1024ULL * 1024 * 1024);

    /*
     * Access density is a percentage measure of how "packed" the reads are.
     * If an application is reading all over the file (aka random reads)
     * or it's reading with periodic gaps between accesses, then the access
     * density will be low and we won't consider it as sequential.
     */
    const int ACCESS_DENSITY_MIN = 70;

    /**
     * Initialize readahead state.
     * Most important data is amount of readahead allowed. Caller will typically
     * read it from from some user configured value. Fuse inode number is to
     * help in logging.
     *
     * TODO: If we can pass the filename add it too for better logging.
     */
    ra_state(uint64_t ino, int _ra_kib) :
        fuse_ino(ino),
        ra_bytes(_ra_kib * 1024)
    {
        /*
         * Some sanity asserts
         * Readahead less than 128KB are not effective and more than 1GB is
         * unnecessary.
         */
        assert(_ra_kib >= 128 && _ra_kib <= 1024*1024);

        AZLogInfo("[{}] Readahead set to {} KiB", ino, _ra_kib);
    }

    /**
     * Returns the offset of the next readahead to issue. Caller must pass the
     * length of the readahead it wants to issue.
     * Return value of 0 would indicate "don't issue readahead read", this would
     * mostly be caused by recent application read pattern which has been
     * indentifed as non-sequential, or if the current ongoing readaheads are
     * already ra_bytes.
     *
     * If this function returns a non-zero value, then caller MUST issue a
     * readahead read at the returned offset and 'length' (or less) and MUST
     * call on_readahead_complete(length) when this readahead read completes,
     * to let ra_state know.
     * Note that the argument to on_readahead_complete() MUST be 'length' even
     * if the readahead read ends up reading less.
     *
     * Note: It doesn't track the file size, so it may recommend readahead
     *       offsets beyond eof. It's the caller's responsibility to handle
     *       that.
     */
    uint64_t get_next_ra(uint64_t length)
    {
        assert(length > 0);

        if ((last_byte_readahead + 1 + length) > AZNFSC_MAX_FILE_SIZE) {
            return 0;
        }

        /*
         * Application read pattern is known to be non-sequential?
         */
        if (!is_sequential()) {
            return 0;
        }

        /*
         * Keep readahead bytes issued always less than ra_bytes.
         */
        if ((ra_ongoing += length) > ra_bytes) {
            assert(ra_ongoing >= length);
            ra_ongoing -= length;
            return 0;
        }

        std::unique_lock<std::shared_mutex> _lock(lock);

        /*
         * Atomically update last_byte_readahead, as we don't want to return
         * duplicate readahead offset to multiple calls.
         */
        const uint64_t next_ra =
            std::atomic_exchange(&last_byte_readahead, last_byte_readahead + length) + 1;

        return next_ra;
    }

    /**
     * Hook for reporting an application read to ra_state.
     * All application read requests MUST be reported so that the readahead
     * engine has complete knowledge of the application read pattern and can
     * provide correct recommendations on readahead.
     * This must be called *before* issuing the read and not after the read
     * completes.
     */
    void on_application_read(uint64_t offset, uint64_t length)
    {
        assert(offset < AZNFSC_MAX_FILE_SIZE);
        assert((offset + length) <= AZNFSC_MAX_FILE_SIZE);

        if (length == 0) {
            assert(0);
            return;
        }

        std::unique_lock<std::shared_mutex> _lock(lock);

        const uint64_t curr_section = (max_byte_read / SECTION_SIZE);
        const uint64_t this_section = (offset / SECTION_SIZE);
        // How far from the current last byte read, is this new request.
        const uint64_t read_gap =
            std::abs((int64_t) (offset - max_byte_read));
        bool reset_readahead = false;

        /*
         * If this read is beyond ra_bytes away from the current last byte
         * read, then this strongly indicates a non-sequential pattern.
         * Reset the readahead state, switching to random access and let the
         * read pattern prove once again for sequential-ness.
         */
        if (read_gap > ra_bytes) {
            reset_readahead = true;
        } else if (curr_section != this_section) {
            /*
             * Read is within the readahead window but the section changes.
             * Since the section size is usually much larger than the readahead
             * window size, so this usually means one of the following two:
             * 1. this_section == "curr_section + 1" (likely for seq pattern).
             * 2. this_section == "curr_section - 1"
             */
            if (this_section != curr_section + 1) {
                assert((this_section == (curr_section - 1)) ||
                       (max_byte_read == UINT64_MAX));
                reset_readahead = true;
            } else {
                /*
                 * Common case of sequential reads progressing to the next
                 * section, don't reset pattern detector.
                 */
                reset_readahead = !is_sequential_nolock();
            }
        }

        if (reset_readahead) {
            num_reads = 1;
            num_bytes_read = length;
            min_byte_read = offset;
            max_byte_read = offset + length - 1;
            last_byte_readahead = 0;
        } else {
            num_reads++;
            num_bytes_read += length;
            max_byte_read = std::max(max_byte_read.load(), offset + length - 1);
            min_byte_read = std::min(min_byte_read.load(), offset);
        }

        /*
         * Next readahead will be from last_byte_readahead+1, so if this read
         * is past the current last_byte_readahead, update last_byte_readahead.
         */
        while (last_byte_readahead < max_byte_read) {
            uint64_t expected = last_byte_readahead;
            last_byte_readahead.compare_exchange_weak(expected,
                                                      max_byte_read);
        }
    }

    /**
     * Hook for reporting completion of a readahead read.
     * This MUST be called for every readahead that get_next_ra() suggested
     * and the length parameter MUST match what was passed to get_next_ra().
     * This must be called before the readahead read completes, successful or
     * not.
     */
    void on_readahead_complete(uint64_t offset, uint64_t length)
    {
        // ra_ongoing is atomic, don't need the lock.
        assert(ra_ongoing >= length);
        ra_ongoing -= length;
    }

    /**
     * Returns the currently observed access pattern.
     */
    bool is_sequential() const
    {
        std::shared_lock<std::shared_mutex> _lock(lock);

        return is_sequential_nolock();
    }

    /**
     * This will run self tests to test the correctness of this class.
     */
    static int unit_test();

private:
    bool is_sequential_nolock() const
    {
        /*
         * Need minimum 3 reads from current section to check the access
         * pattern.
         */
        if (num_reads < 3) {
            return false;
        }

        const int64_t access_range = (max_byte_read - min_byte_read);
        assert(access_range > 0);

        const int access_density = (num_bytes_read * 100) / access_range;
        assert(access_density <= 100);

        return (access_density > ACCESS_DENSITY_MIN);
    }

    /*
     * Fuse inode for the file this readahead state corresponds to.
     * This is for logging.
     */
    const uint64_t fuse_ino;

    /*
     * Total readahead size in bytes, aka the "readahead window".
     * Readahead reads recommended by us will always be less than
     * "max_byte_read + ra_bytes".
     */
    const uint64_t ra_bytes;

    /*
     * Last byte of readahead read recommended by most recent call to
     * get_next_ra(). Next readahead recommended will start at the next byte
     * after this.
     * This is reset when pattern detection is reset.
     */
    std::atomic<uint64_t> last_byte_readahead = 0;

    /*
     * Smallest and largest byte read in the current section.
     * This is truthfully updated as application reports its read calls through
     * on_application_read().
     * These are reset when pattern detection is reset.
     */
    std::atomic<uint64_t> min_byte_read = 0;
    std::atomic<uint64_t> max_byte_read = UINT64_MAX;

    /*
     * Current ongoing readahead bytes.
     * This depends on application correctly informing us of readahead reads
     * completing by calling on_readahead_complete().
     * This is not reset when pattern detection is reset.
     */
    std::atomic<uint64_t> ra_ongoing = 0;

    /*
     * Number of read calls and number of bytes read by those, in the current
     * section.
     * These are reset when pattern detection is reset.
     */
    std::atomic<uint64_t> num_reads = 0;
    std::atomic<uint64_t> num_bytes_read = 0;

    /*
     * Lock for safely accessing/updating above state.
     */
    mutable std::shared_mutex lock;
};

}

#endif /* __READAHEAD_H__ */