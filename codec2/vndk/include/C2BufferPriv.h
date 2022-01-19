/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef STAGEFRIGHT_CODEC2_BUFFER_PRIV_H_
#define STAGEFRIGHT_CODEC2_BUFFER_PRIV_H_

#include <functional>
#include <sys/stat.h>

#include <C2Buffer.h>

class C2BasicLinearBlockPool : public C2BlockPool {
public:
    explicit C2BasicLinearBlockPool(const std::shared_ptr<C2Allocator> &allocator);

    virtual ~C2BasicLinearBlockPool() override = default;

    virtual C2Allocator::id_t getAllocatorId() const override {
        return mAllocator->getId();
    }

    virtual local_id_t getLocalId() const override {
        return BASIC_LINEAR;
    }

    virtual c2_status_t fetchLinearBlock(
            uint32_t capacity,
            C2MemoryUsage usage,
            std::shared_ptr<C2LinearBlock> *block /* nonnull */) override;

    // TODO: fetchCircularBlock

private:
    const std::shared_ptr<C2Allocator> mAllocator;
};

class C2BasicGraphicBlockPool : public C2BlockPool {
public:
    explicit C2BasicGraphicBlockPool(const std::shared_ptr<C2Allocator> &allocator);

    virtual ~C2BasicGraphicBlockPool() override = default;

    virtual C2Allocator::id_t getAllocatorId() const override {
        return mAllocator->getId();
    }

    virtual local_id_t getLocalId() const override {
        return BASIC_GRAPHIC;
    }

    virtual c2_status_t fetchGraphicBlock(
            uint32_t width,
            uint32_t height,
            uint32_t format,
            C2MemoryUsage usage,
            std::shared_ptr<C2GraphicBlock> *block /* nonnull */) override;

private:
    const std::shared_ptr<C2Allocator> mAllocator;
};

class C2BufferStats {
public:
    C2BufferStats(const char *name);

    virtual ~C2BufferStats();

    void printStats();

    void clearStats();

    void onBufferAllocated(size_t allocSize);

    void onBufferEvicted(size_t allocSize);

    void onBufferRecycled(size_t allocSize);

    void onBufferUnused(size_t allocSize);

private:
    std::string mImplName; ///< name for debugging
    const char *mName; ///< C-string version of name
    std::mutex mLock;
    size_t mSizeCached;         /* pool active size of allocations */
    size_t mBuffersCached;      /* # of cached buffers available for circulation */
    size_t mSizePeak;           /* pool peak memory usage */
    size_t mBuffersPeak;        /* # of max allocations reached in pool */
    size_t mBuffersInUse;       /* # of allocations with client */
    size_t mTotalAllocations;   /* # of fetches from pool */
    size_t mTotalRecycles;      /* # of allocations from cache(recycle count) */
};

class C2BufferAllocation {
public:
    C2BufferAllocation(const native_handle_t *const handle, uint32_t size)
        : mHandle(handle),
          mAllocSize(size),
          mUId(getUId(handle)) {}

    virtual ~C2BufferAllocation() = default;

    const native_handle_t* handle() const {
        return mHandle;
    }

    uint32_t size() const {
        return mAllocSize;
    }

    uint64_t uId() const {
        return mUId;
    }

private:
    uint64_t getUId(const native_handle_t *const handle) {
        struct stat buf;
        int fd = -1, ret = 0;

        if (handle && handle->numFds > 0) {
            fd = handle->data[0];
            ret = fstat(fd, &buf);
            if (ret) {
                ALOGE("%s: fstat failed. fd %d, ret %d\n", __func__, fd, ret);
                return 0;
            }

            return buf.st_ino;
        }
        return 0;
    }

private:
    const native_handle_t *mHandle;
    const uint32_t mAllocSize;
    const uint64_t mUId;    // inode
};

class InternalBuffer {
public:
    InternalBuffer(const std::shared_ptr<C2LinearAllocation> &c2Linear,
        const std::shared_ptr<C2BufferStats> stats,
        const AllocBasicParams &c2Param);

    InternalBuffer(const std::shared_ptr<C2GraphicAllocation> &c2Graphic,
        const std::shared_ptr<C2BufferStats> stats,
        const AllocBasicParams &c2Param);

    virtual ~InternalBuffer() = default;

    std::shared_ptr<C2BufferAllocation> allocation() const {
        return mAllocation;
    }

    AllocBasicParams config() const {
        return mConfig;
    }

    void updateExpire(uint64_t currTsUs);

    bool expire(uint64_t currTsUs) const;

private:
    const std::shared_ptr<C2BufferStats> mStats;
    const std::shared_ptr<C2BufferAllocation> mAllocation;
    const AllocBasicParams mConfig;
    uint64_t mExpireUs;

    static constexpr uint32_t kCacheDeltaUs = 1000000;
};

class C2PoolAllocator {
public:
    C2PoolAllocator(
        const std::shared_ptr<C2Allocator> &allocator,
        const std::shared_ptr<C2BufferStats> &stats);

    virtual ~C2PoolAllocator() = default;

    c2_status_t allocate(const AllocBasicParams &c2Params,
                          std::unique_ptr<InternalBuffer> *buf);

    bool compatible(const AllocBasicParams &newAlloc,
                    const AllocBasicParams &oldAlloc);

private:
    const std::shared_ptr<C2Allocator> mAllocator;
    const std::shared_ptr<C2BufferStats> mStats;
};

class C2BasicBufferManager {
public:
    C2BasicBufferManager(
        const std::shared_ptr<C2Allocator> &allocator,
        const std::shared_ptr<C2BufferStats> stats,
        const char *name);

    virtual ~C2BasicBufferManager() = default;

    void evictUnusedBuffers_l(bool forceEvict = false);

    virtual c2_status_t grabHandle(
        const AllocBasicParams &c2Params,
        uint64_t *uId, const native_handle_t** handle);

    virtual void returnHandle(uint64_t uId);

private:
    std::string mImplName; ///< name for debugging
    const char *mName; ///< C-string version of name
    std::mutex mLock;
    uint64_t mTimestampUs;
    uint64_t mLastEvictBufferUs;
    uint64_t mLastLogBufferTsUs;
    const std::shared_ptr<C2BufferStats> mStats;
    const std::unique_ptr<C2PoolAllocator> mAllocator;
    std::list<std::unique_ptr<InternalBuffer>> mFreeBuffers;
    std::list<std::unique_ptr<InternalBuffer>> mBuffersInUse;

    struct EvictBuffer {
        explicit EvictBuffer(std::function<void()> func) : evict(func) {}

        virtual ~EvictBuffer() {
            evict();
        }

        std::function<void()> evict;
    };

    static constexpr uint32_t kCacheDeltaUs = 1000000;
    static constexpr uint32_t kLogDeltaUs = 5000000;
    static constexpr size_t kMaxCachedBufferCount = 64;
    static constexpr size_t kCachedBufferCountTarget = kMaxCachedBufferCount - 16;
};

class C2CustomBufferManager {
public:
    C2CustomBufferManager() = default;

    virtual ~C2CustomBufferManager() = default;

    virtual c2_status_t initialize(
            const AllocBasicParams &c2Params,
            const uint32_t capacity) = 0;

    virtual c2_status_t grabHandle(
            const AllocBasicParams &c2Params,
            uint32_t *pId, uint64_t *uId, const native_handle_t** handle) = 0;

    virtual void returnHandle(uint32_t pId, uint64_t uId) = 0;

    virtual c2_status_t grow(size_t newSize) = 0;

    virtual c2_status_t shrink(size_t newSize) = 0;
};

class C2CustomContigiousBufferManager : public C2CustomBufferManager {
public:
    C2CustomContigiousBufferManager(
        const std::shared_ptr<C2Allocator> &allocator,
        const std::shared_ptr<C2BufferStats> stats, const char *name);

    virtual ~C2CustomContigiousBufferManager() = default;

    virtual c2_status_t initialize(
            const AllocBasicParams &c2Params,
            const uint32_t capacity) override;

    virtual c2_status_t grabHandle(
            const AllocBasicParams &c2Params,
            uint32_t *pId, uint64_t *uId, const native_handle_t** handle) override;

    virtual void returnHandle(uint32_t pId, const uint64_t uId) override;

    virtual c2_status_t grow(size_t newSize) override;

    virtual c2_status_t shrink(size_t newSize) override;

private:
    c2_status_t mInit;
    std::string mImplName; ///< name for debugging
    const char *mName; ///< C-string version of name
    std::mutex mLock;
    uint32_t mNextSlot;
    struct Entry {
        uint32_t id;
        std::unique_ptr<InternalBuffer> buf;
        bool ownedByClient;
    };
    const std::shared_ptr<C2BufferStats> mStats;
    const std::unique_ptr<C2PoolAllocator> mAllocator;
    std::vector<Entry> mBuffers;
};

class C2CustomNonContigiousBufferManager : public C2CustomBufferManager {
public:
    C2CustomNonContigiousBufferManager(
        const std::shared_ptr<C2Allocator> &allocator,
        const std::shared_ptr<C2BufferStats> stats, const char *name);

    virtual ~C2CustomNonContigiousBufferManager() = default;

    virtual c2_status_t initialize(
            const AllocBasicParams &c2Params,
            const uint32_t capacity) override;

    virtual c2_status_t grabHandle(
            const AllocBasicParams &c2Params,
            uint32_t *pId, uint64_t *uId, const native_handle_t** handle) override;

    virtual void returnHandle(uint32_t pId, const uint64_t uId) override;

    virtual c2_status_t grow(size_t newSize) override;

    virtual c2_status_t shrink(size_t newSize) override;

private:
    c2_status_t mInit;
    std::string mImplName; ///< name for debugging
    const char *mName; ///< C-string version of name
    std::mutex mLock;
    uint32_t mPoolCapacity;
    struct Entry {
        uint32_t id;
        std::unique_ptr<InternalBuffer> buf;
    };
    const std::shared_ptr<C2BufferStats> mStats;
    const std::unique_ptr<C2PoolAllocator> mAllocator;
    std::list<Entry> mFreeBuffers;
    std::list<Entry> mBuffersInUse;
};

class C2BasicBlockPool : public C2BlockPool {
public:
    C2BasicBlockPool(const std::shared_ptr<C2Allocator> &allocator,
        const uint32_t poolType, const local_id_t localId);

    virtual ~C2BasicBlockPool() override;

    virtual C2Allocator::id_t getAllocatorId() const override {
        return mAllocator->getId();
    }

    virtual local_id_t getLocalId() const override {
        return mLocalId;
    }

    virtual uint32_t getPoolType() const override {
        return mPoolType;
    }

    virtual c2_status_t fetchLinearBlock(
            uint32_t capacity,
            C2MemoryUsage usage,
            std::shared_ptr<C2LinearBlock> *block /* nonnull */) override;

    virtual c2_status_t fetchGraphicBlock(
            uint32_t width,
            uint32_t height,
            uint32_t format,
            C2MemoryUsage usage,
            std::shared_ptr<C2GraphicBlock> *block /* nonnull */) override;

private:
    const std::shared_ptr<C2Allocator> mAllocator;
    const local_id_t mLocalId;
    const uint32_t mPoolType;

    class Impl;
    std::unique_ptr<Impl> mImpl;
};

class C2CustomBlockPool : public C2BlockPool {
public:
    C2CustomBlockPool(const std::shared_ptr<C2Allocator> &allocator,
        const uint32_t poolType, const local_id_t localId, const bool contigious);

    virtual ~C2CustomBlockPool() override;

    virtual C2Allocator::id_t getAllocatorId() const override {
        return mAllocator->getId();
    }

    virtual local_id_t getLocalId() const override {
        return mLocalId;
    }

    virtual uint32_t getPoolType() const override {
        return mPoolType;
    }

    virtual c2_status_t fetchLinearBlock(
            uint32_t capacity,
            C2MemoryUsage usage,
            std::shared_ptr<C2LinearBlock> *block /* nonnull */) override;

    virtual c2_status_t fetchGraphicBlock(
            uint32_t width,
            uint32_t height,
            uint32_t format,
            C2MemoryUsage usage,
            std::shared_ptr<C2GraphicBlock> *block) override;

    virtual c2_status_t init(
            const AllocBasicParams &c2Params, uint32_t poolCapacity) override;

private:
    const std::shared_ptr<C2Allocator> mAllocator;
    const local_id_t mLocalId;
    const uint32_t mPoolType;

    class Impl;
    std::unique_ptr<Impl> mImpl;
};
#endif // STAGEFRIGHT_CODEC2_BUFFER_PRIV_H_
