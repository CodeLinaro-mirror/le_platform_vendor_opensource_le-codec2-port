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
 *
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "C2Buffer"
#include <C2Debug.h>

#include <list>
#include <map>
#include <mutex>

#include <C2BufferPriv.h>
#include <C2BlockInternal.h>
#include <C2PlatformSupport.h>

namespace {

// This anonymous namespace contains the helper classes that allow our implementation to create
// block/buffer objects.
//
// Inherit from the parent, share with the friend.
class ReadViewBuddy : public C2ReadView {
    using C2ReadView::C2ReadView;
    friend class ::C2ConstLinearBlock;
};

class WriteViewBuddy : public C2WriteView {
    using C2WriteView::C2WriteView;
    friend class ::C2LinearBlock;
};

class ConstLinearBlockBuddy : public C2ConstLinearBlock {
    using C2ConstLinearBlock::C2ConstLinearBlock;
    friend class ::C2LinearBlock;
};

class LinearBlockBuddy : public C2LinearBlock {
    using C2LinearBlock::C2LinearBlock;
    friend class ::C2BasicLinearBlockPool;
};

class AcquirableReadViewBuddy : public C2Acquirable<C2ReadView> {
    using C2Acquirable::C2Acquirable;
    friend class ::C2ConstLinearBlock;
};

class AcquirableWriteViewBuddy : public C2Acquirable<C2WriteView> {
    using C2Acquirable::C2Acquirable;
    friend class ::C2LinearBlock;
};

class GraphicViewBuddy : public C2GraphicView {
    using C2GraphicView::C2GraphicView;
    friend class ::C2ConstGraphicBlock;
    friend class ::C2GraphicBlock;
};

class AcquirableConstGraphicViewBuddy : public C2Acquirable<const C2GraphicView> {
    using C2Acquirable::C2Acquirable;
    friend class ::C2ConstGraphicBlock;
};

class AcquirableGraphicViewBuddy : public C2Acquirable<C2GraphicView> {
    using C2Acquirable::C2Acquirable;
    friend class ::C2GraphicBlock;
};

class ConstGraphicBlockBuddy : public C2ConstGraphicBlock {
    using C2ConstGraphicBlock::C2ConstGraphicBlock;
    friend class ::C2GraphicBlock;
};

class GraphicBlockBuddy : public C2GraphicBlock {
    using C2GraphicBlock::C2GraphicBlock;
    friend class ::C2BasicGraphicBlockPool;
};

class BufferDataBuddy : public C2BufferData {
    using C2BufferData::C2BufferData;
    friend class ::C2Buffer;
    friend class ::C2InfoBuffer;
};

}  // namespace

/* ========================================== 1D BLOCK ========================================= */

/**
 * This class is the base class for all 1D block and view implementations.
 *
 * This is basically just a placeholder for the underlying 1D allocation and the range of the
 * alloted portion to this block. There is also a placeholder for a blockpool data.
 */
class C2_HIDE _C2Block1DImpl : public _C2LinearRangeAspect {
public:
    _C2Block1DImpl(const std::shared_ptr<C2LinearAllocation> &alloc,
            size_t offset = 0, size_t size = ~(size_t)0)
        : _C2LinearRangeAspect(alloc.get(), offset, size),
          mAllocation(alloc) { }

    _C2Block1DImpl(const _C2Block1DImpl &other, size_t offset = 0, size_t size = ~(size_t)0)
        : _C2LinearRangeAspect(&other, offset, size),
          mAllocation(other.mAllocation) { }

    /** returns native handle */
    const C2Handle *handle() const {
        return mAllocation ? mAllocation->handle() : nullptr;
    }

    /** returns the allocator's ID */
    C2Allocator::id_t getAllocatorId() const {
        // BAD_ID can only happen if this Impl class is initialized for a view - never for a block.
        return mAllocation ? mAllocation->getAllocatorId() : C2Allocator::BAD_ID;
    }

    std::shared_ptr<C2LinearAllocation> getAllocation() const {
        return mAllocation;
    }

private:
    std::shared_ptr<C2LinearAllocation> mAllocation;
};

/**
 * This class contains the mapped data pointer, and the potential error.
 *
 * range is the mapped range of the underlying allocation (which is part of the allotted
 * range).
 */
class C2_HIDE _C2MappedBlock1DImpl : public _C2Block1DImpl {
public:
    _C2MappedBlock1DImpl(const _C2Block1DImpl &block, uint8_t *data,
                         size_t offset = 0, size_t size = ~(size_t)0)
        : _C2Block1DImpl(block, offset, size), mData(data), mError(C2_OK) { }

    _C2MappedBlock1DImpl(c2_status_t error)
        : _C2Block1DImpl(nullptr), mData(nullptr), mError(error) {
        // CHECK(error != C2_OK);
    }

    const uint8_t *data() const {
        return mData;
    }

    uint8_t *data() {
        return mData;
    }

    c2_status_t error() const {
        return mError;
    }

private:
    uint8_t *mData;
    c2_status_t mError;
};

/**
 * Block implementation.
 */
class C2Block1D::Impl : public _C2Block1DImpl {
    using _C2Block1DImpl::_C2Block1DImpl;
};

const C2Handle *C2Block1D::handle() const {
    return mImpl->handle();
};

C2Allocator::id_t C2Block1D::getAllocatorId() const {
    return mImpl->getAllocatorId();
};

C2Block1D::C2Block1D(std::shared_ptr<Impl> impl, const _C2LinearRangeAspect &range)
    // always clamp subrange to parent (impl) range for safety
    : _C2LinearRangeAspect(impl.get(), range.offset(), range.size()), mImpl(impl) {
}

/**
 * Read view implementation.
 *
 * range of Impl is the mapped range of the underlying allocation (which is part of the allotted
 * range). range of View is 0 to capacity() (not represented as an actual range). This maps to a
 * subrange of Impl range starting at mImpl->offset() + _mOffset.
 */
class C2ReadView::Impl : public _C2MappedBlock1DImpl {
    using _C2MappedBlock1DImpl::_C2MappedBlock1DImpl;
};

C2ReadView::C2ReadView(std::shared_ptr<Impl> impl, uint32_t offset, uint32_t size)
    : _C2LinearCapacityAspect(C2LinearCapacity(impl->size()).range(offset, size).size()),
      mImpl(impl),
      mOffset(C2LinearCapacity(impl->size()).range(offset, size).offset()) { }

C2ReadView::C2ReadView(c2_status_t error)
    : _C2LinearCapacityAspect(0u), mImpl(std::make_shared<Impl>(error)), mOffset(0u) {
    // CHECK(error != C2_OK);
}

const uint8_t *C2ReadView::data() const {
    return mImpl->error() ? nullptr : mImpl->data() + mOffset;
}

c2_status_t C2ReadView::error() const {
    return mImpl->error();
}

C2ReadView C2ReadView::subView(size_t offset, size_t size) const {
    C2LinearRange subRange(*this, offset, size);
    return C2ReadView(mImpl, mOffset + subRange.offset(), subRange.size());
}

/**
 * Write view implementation.
 */
class C2WriteView::Impl : public _C2MappedBlock1DImpl {
    using _C2MappedBlock1DImpl::_C2MappedBlock1DImpl;
};

C2WriteView::C2WriteView(std::shared_ptr<Impl> impl)
// UGLY: _C2LinearRangeAspect requires a bona-fide object for capacity to prevent spoofing, so
// this is what we have to do.
// TODO: use childRange
    : _C2EditableLinearRangeAspect(std::make_unique<C2LinearCapacity>(impl->size()).get()), mImpl(impl) { }

C2WriteView::C2WriteView(c2_status_t error)
    : _C2EditableLinearRangeAspect(nullptr), mImpl(std::make_shared<Impl>(error)) {}

uint8_t *C2WriteView::base() { return mImpl->data(); }

uint8_t *C2WriteView::data() { return mImpl->data() + offset(); }

c2_status_t C2WriteView::error() const { return mImpl->error(); }

/**
 * Const linear block implementation.
 */
C2ConstLinearBlock::C2ConstLinearBlock(std::shared_ptr<Impl> impl, const _C2LinearRangeAspect &range, C2Fence fence)
    : C2Block1D(impl, range), mFence(fence) { }

C2Acquirable<C2ReadView> C2ConstLinearBlock::map() const {
    void *base = nullptr;
    uint32_t len = size();
    c2_status_t error = mImpl->getAllocation()->map(
            offset(), len, { C2MemoryUsage::CPU_READ, 0 }, nullptr, &base);
    // TODO: wait on fence
    if (error == C2_OK) {
        std::shared_ptr<ReadViewBuddy::Impl> rvi = std::shared_ptr<ReadViewBuddy::Impl>(
                new ReadViewBuddy::Impl(*mImpl, (uint8_t *)base, offset(), len),
                [base, len](ReadViewBuddy::Impl *i) {
                    (void)i->getAllocation()->unmap(base, len, nullptr);
                    delete i;
        });
        return AcquirableReadViewBuddy(error, C2Fence(), ReadViewBuddy(rvi, 0, len));
    } else {
        return AcquirableReadViewBuddy(error, C2Fence(), ReadViewBuddy(error));
    }
}

C2ConstLinearBlock C2ConstLinearBlock::subBlock(size_t offset_, size_t size_) const {
    C2LinearRange subRange(*mImpl, offset_, size_);
    return C2ConstLinearBlock(mImpl, subRange, mFence);
}

/**
 * Linear block implementation.
 */
C2LinearBlock::C2LinearBlock(std::shared_ptr<Impl> impl, const _C2LinearRangeAspect &range)
    : C2Block1D(impl, range) { }

C2Acquirable<C2WriteView> C2LinearBlock::map() {
    void *base = nullptr;
    uint32_t len = size();
    c2_status_t error = mImpl->getAllocation()->map(
            offset(), len, { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE }, nullptr, &base);
    // TODO: wait on fence
    if (error == C2_OK) {
        std::shared_ptr<WriteViewBuddy::Impl> rvi = std::shared_ptr<WriteViewBuddy::Impl>(
                new WriteViewBuddy::Impl(*mImpl, (uint8_t *)base, 0, len),
                [base, len](WriteViewBuddy::Impl *i) {
                    (void)i->getAllocation()->unmap(base, len, nullptr);
                    delete i;
        });
        return AcquirableWriteViewBuddy(error, C2Fence(), WriteViewBuddy(rvi));
    } else {
        return AcquirableWriteViewBuddy(error, C2Fence(), WriteViewBuddy(error));
    }
}

C2ConstLinearBlock C2LinearBlock::share(size_t offset_, size_t size_, C2Fence fence) {
    return ConstLinearBlockBuddy(mImpl, C2LinearRange(*this, offset_, size_), fence);
}

C2BasicLinearBlockPool::C2BasicLinearBlockPool(
        const std::shared_ptr<C2Allocator> &allocator)
  : mAllocator(allocator) { }

c2_status_t C2BasicLinearBlockPool::fetchLinearBlock(
        uint32_t size,
        C2MemoryUsage usage,
        std::shared_ptr<C2LinearBlock> *block /* nonnull */) {
    C2TRACED("C2Buffer::fetchLinearBlock");
    block->reset();

    std::shared_ptr<C2LinearAllocation> alloc;
    c2_status_t err = mAllocator->newLinearAllocation(size, usage, &alloc);
    if (err != C2_OK) {
        return err;
    }

    *block = _C2BlockFactory::CreateLinearBlock(alloc);

    return C2_OK;
}

std::shared_ptr<C2LinearBlock> _C2BlockFactory::CreateLinearBlock(
        const std::shared_ptr<C2LinearAllocation> &alloc,
        std::function<void(C2LinearBlock*)> deleter) {
    std::shared_ptr<C2Block1D::Impl> impl =
        std::make_shared<C2Block1D::Impl>(alloc);
    return std::shared_ptr<C2LinearBlock>(new C2LinearBlock(impl, *impl), deleter);
}

std::shared_ptr<C2LinearBlock> _C2BlockFactory::CreateLinearBlock(
        const std::shared_ptr<C2LinearAllocation> &alloc,
        size_t offset, size_t size) {
    std::shared_ptr<C2Block1D::Impl> impl =
        std::make_shared<C2Block1D::Impl>(alloc, offset, size);
    return std::shared_ptr<C2LinearBlock>(new C2LinearBlock(impl, *impl));
}

static uint64_t getTimestampNow() {
    struct timespec ts;
    uint64_t stamp;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    stamp = ts.tv_nsec / 1000;
    stamp += (ts.tv_sec * 1000000LL);
    return stamp;
}

static std::string getPoolTypeString(const uint32_t poolType) {
    switch(poolType) {
        case C2AllocatorStore::DEFAULT_LINEAR:         return std::string("1D");
        case C2AllocatorStore::DEFAULT_GRAPHIC:        return std::string("2D");
        case C2AllocatorStore::LINEAR_CONTIGUOUS:      return std::string("1D");
        case C2AllocatorStore::LINEAR_NON_CONTIGUOUS:  return std::string("1D");
        case C2AllocatorStore::GRAPHIC_CONTIGUOUS:     return std::string("2D");
        case C2AllocatorStore::GRAPHIC_NON_CONTIGUOUS: return std::string("2D");
    }
    return std::string("NA");
}

struct C2LinearBufferAllocationDtor {
    C2LinearBufferAllocationDtor(
        const std::shared_ptr<C2LinearAllocation> &alloc,
        const std::shared_ptr<C2BufferStats> stats)
        : mAllocation(alloc), mStats(stats) {}

    void operator()(C2BufferAllocation *alloc) {
        delete alloc;

        auto stats = mStats.lock();
        if (stats) {
            stats->onBufferEvicted(mAllocation->allocSize());
        }
    }

    const std::shared_ptr<C2LinearAllocation> mAllocation;
    const std::weak_ptr<C2BufferStats> mStats;
};

struct C2GraphicBufferAllocationDtor {
    C2GraphicBufferAllocationDtor(
        const std::shared_ptr<C2GraphicAllocation> &alloc,
        const std::shared_ptr<C2BufferStats> stats)
        : mAllocation(alloc), mStats(stats) {}

    void operator()(C2BufferAllocation *alloc) {
        delete alloc;

        auto stats = mStats.lock();
        if (stats) {
            stats->onBufferEvicted(mAllocation->allocSize());
        }
    }

    const std::shared_ptr<C2GraphicAllocation> mAllocation;
    const std::weak_ptr<C2BufferStats> mStats;
};

AllocBasicParams::AllocBasicParams()
    : data({ALLOC_NONE, {0, 0}, {0}}) {}

AllocBasicParams::AllocBasicParams(C2MemoryUsage usage, uint32_t size)
    : data({ALLOC_LINEAR, usage, {[0] = size}}) {}

AllocBasicParams::AllocBasicParams(
    C2MemoryUsage usage,
    uint32_t width, uint32_t height, uint32_t format)
    : data({ALLOC_GRAPHIC, usage, {width, height, format}}) {}

C2BufferAllocation::C2BufferAllocation(
    const native_handle_t *const handle, uint32_t size)
    : mHandle(handle),
      mAllocSize(size),
      mUId(getUId(handle)) {}

uint64_t C2BufferAllocation::getUId(const native_handle_t *const handle) {
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

InternalBuffer::InternalBuffer(
    const std::shared_ptr<C2LinearAllocation> &c2Linear,
    const std::shared_ptr<C2BufferStats> stats,
    const AllocBasicParams &c2Param)
    : mExpireUs(getTimestampNow() + kCacheDeltaUs),
      mStats(stats),
      mAllocation(new C2BufferAllocation(c2Linear->handle(), c2Linear->allocSize()),
        C2LinearBufferAllocationDtor(c2Linear, mStats)),
      mConfig(c2Param) {
    mStats->onBufferAllocated(c2Linear->allocSize());
}

InternalBuffer::InternalBuffer(const std::shared_ptr<C2GraphicAllocation> &c2Graphic,
    const std::shared_ptr<C2BufferStats> stats,
    const AllocBasicParams &c2Param)
    : mExpireUs(getTimestampNow() + kCacheDeltaUs),
      mStats(stats),
      mAllocation(new C2BufferAllocation(c2Graphic->handle(), c2Graphic->allocSize()),
        C2GraphicBufferAllocationDtor(c2Graphic, mStats)),
      mConfig(c2Param) {
    mStats->onBufferAllocated(c2Graphic->allocSize());
}

void InternalBuffer::updateExpire(uint64_t currTsUs) {
    mExpireUs = currTsUs + kCacheDeltaUs;
}

bool InternalBuffer::expire(uint64_t currTsUs) const {
    return currTsUs >= mExpireUs;
}

C2PoolAllocator::C2PoolAllocator(
    const std::shared_ptr<C2Allocator> &allocator,
    const std::shared_ptr<C2BufferStats> &stats)
    : mAllocator(allocator), mStats(stats) {}

void C2PoolAllocator::resetAllocIndex() {
    return mAllocator->resetAllocIndex();
}

bool C2PoolAllocator::compatible(
    const AllocBasicParams &newAlloc,
    const AllocBasicParams &oldAlloc) {

    if (newAlloc.data.allocType != oldAlloc.data.allocType ||
        newAlloc.data.usage.expected != oldAlloc.data.usage.expected) {
        return false;
    }

    for (int i = 0; i < AllocBasicParams::kMaxIntParams; ++i) {
        if (newAlloc.data.params[i] != oldAlloc.data.params[i]) {
            return false;
        }
    }

    return true;
}

c2_status_t C2PoolAllocator::allocate(
    const AllocBasicParams &c2Params,
    std::unique_ptr<InternalBuffer> *buf) {
    c2_status_t ret = C2_BAD_VALUE;
    switch(c2Params.data.allocType) {
        case AllocBasicParams::ALLOC_LINEAR: {
            std::shared_ptr<C2LinearAllocation> c2Linear;
            ret = mAllocator->newLinearAllocation(
                    c2Params.data.params[0], c2Params.data.usage, &c2Linear);
            if (ret == C2_OK && c2Linear) {
                *buf = std::make_unique<InternalBuffer>(c2Linear, mStats, c2Params);
                return ret;
            }
            ALOGE("%s: alloc failed. size %u, usage %#llx\n",
                __func__, c2Params.data.params[0], c2Params.data.usage);
            break;
        }
        case AllocBasicParams::ALLOC_GRAPHIC: {
            std::shared_ptr<C2GraphicAllocation> c2Graphic;
            ret = mAllocator->newGraphicAllocation(
                    c2Params.data.params[0],
                    c2Params.data.params[1],
                    c2Params.data.params[2],
                    c2Params.data.usage, &c2Graphic);
            if (ret == C2_OK && c2Graphic) {
                *buf = std::make_unique<InternalBuffer>(c2Graphic, mStats, c2Params);
                return ret;
            }
            ALOGE("%s: alloc failed. w %u, h %u, f %#llx, u %#llx\n",
                __func__, c2Params.data.params[0],
                c2Params.data.params[1], c2Params.data.params[2], c2Params.data.usage);
            break;
        }
        case AllocBasicParams::ALLOC_NONE:
        default:
            ALOGE("%s: invalid allocType %#x\n", __func__, c2Params.data.allocType);
            break;

    }
    return ret;
}

C2BufferStats::C2BufferStats(const char *name)
    : mSizeCached(0),
      mBuffersCached(0),
      mSizePeak(0),
      mBuffersPeak(0),
      mBuffersInUse(0),
      mTotalAllocations(0),
      mTotalRecycles(0),
      mImplName(std::string(name) + ".Stats"),
      mName(mImplName.c_str()) {}

C2BufferStats::~C2BufferStats() {
    ALOGD("[%s] Destruction: Active: count(inUse) %zu(%zu), size %.03f MB,"
            " Peak: count %zu, size %.03f MB, Recycle ratio(%zu/%zu)",
          mName, mBuffersCached, mBuffersInUse,
          (float)mSizeCached / (float)(1024 * 1024),
          mBuffersPeak, (float)mSizePeak / (float)(1024 * 1024),
          mTotalRecycles, mTotalAllocations);
}

void C2BufferStats::printStats() {
    std::lock_guard<std::mutex> lock(mLock);
    ALOGD("[%s] Active: count(inUse) %zu(%zu), size %.03f MB,"
            " Peak: count %zu, size %.03f MB, Recycle ratio(%zu/%zu)",
          mName, mBuffersCached, mBuffersInUse,
          (float)mSizeCached / (float)(1024 * 1024),
          mBuffersPeak, (float)mSizePeak / (float)(1024 * 1024),
          mTotalRecycles, mTotalAllocations);
}

void C2BufferStats::clearStats() {
    std::lock_guard<std::mutex> lock(mLock);
    mSizeCached = 0;
    mBuffersCached = 0;
    mSizePeak = 0;
    mBuffersPeak = 0;
    mBuffersInUse = 0;
    mTotalAllocations = 0;
    mTotalRecycles = 0;
}

void C2BufferStats::onBufferAllocated(size_t allocSize) {
    std::lock_guard<std::mutex> lock(mLock);
    mSizeCached += allocSize;
    mBuffersCached++;

    mSizePeak += allocSize;
    mBuffersPeak++;

    mBuffersInUse++;
    mTotalAllocations++;
}

void C2BufferStats::onBufferEvicted(size_t allocSize) {
    std::lock_guard<std::mutex> lock(mLock);
    mSizeCached -= allocSize;
    mBuffersCached--;
}

void C2BufferStats::onBufferRecycled(size_t allocSize) {
    std::lock_guard<std::mutex> lock(mLock);
    mBuffersInUse++;

    mTotalAllocations++;
    mTotalRecycles++;
}

void C2BufferStats::onBufferUnused(size_t allocSize) {
    std::lock_guard<std::mutex> lock(mLock);
    mBuffersInUse--;
}

C2BasicBufferManager::C2BasicBufferManager(
    const std::shared_ptr<C2Allocator> &allocator,
    const std::shared_ptr<C2BufferStats> stats,
    const char *name)
    : mTimestampUs(getTimestampNow()),
      mLastEvictBufferUs(mTimestampUs),
      mLastLogBufferTsUs(mTimestampUs),
      mStats(stats),
      mAllocator(std::make_unique<C2PoolAllocator>(allocator, stats)),
      mImplName(std::string(name) + ".BBMImpl"),
      mName(mImplName.c_str()) {
}

void C2BasicBufferManager::evictUnusedBuffers_l(bool forceEvict) {
    int32_t poolCapacity =  mFreeBuffers.size() + mBuffersInUse.size();

    if (forceEvict || mTimestampUs >= mLastEvictBufferUs + kCacheDeltaUs ||
        poolCapacity > kMaxCachedBufferCount) {
        size_t evicted = 0;
        for(auto it = mFreeBuffers.begin(); it != mFreeBuffers.end(); ) {
            if (forceEvict || (*it)->expire(mTimestampUs) ||
                mFreeBuffers.size() > kCachedBufferCountTarget) {
                it = mFreeBuffers.erase(it);
                ++evicted;
            } else {
                ++it;
            }
        }
        ALOGV("[%s] %s: total %u, free %u, inUse %u, evicted %zu\n", mName, __func__,
            poolCapacity, mFreeBuffers.size(), mBuffersInUse.size(), evicted);
        mLastEvictBufferUs = mTimestampUs;
    }

    /* print buffer statistics at every 5 secs */
    if (mTimestampUs >= mLastLogBufferTsUs + kLogDeltaUs) {
        mStats->printStats();
        mLastLogBufferTsUs = mTimestampUs;
    }
}

c2_status_t C2BasicBufferManager::grabHandle(const AllocBasicParams &params,
    uint64_t *uId, const native_handle_t** handle) {
    std::lock_guard<std::mutex> lock(mLock);
    std::unique_ptr<InternalBuffer> buf;
    uint32_t bufferId, size;
    c2_status_t ret;

    /* update current timestamp */
    mTimestampUs = getTimestampNow();

    auto evictBuffer = [&]() {
        /* evict buffers which is unused > 1 sec */
        evictUnusedBuffers_l();
    };
    EvictBuffer e(evictBuffer);

    auto it = std::find_if(mFreeBuffers.begin(), mFreeBuffers.end(),
            [&](const std::unique_ptr<InternalBuffer>& buf) -> bool {
                return mAllocator->compatible(buf->config(), params);
            });
    if (it != mFreeBuffers.end()) {
        mBuffersInUse.splice(mBuffersInUse.end(), mFreeBuffers, it);
        *handle = mBuffersInUse.back()->allocation()->handle();
        *uId = mBuffersInUse.back()->allocation()->uId();
        size = mBuffersInUse.back()->allocation()->size();
        ALOGV("[%s] %s: buf-id %llu, size %u, free %u, inUse %u\n", mName, __func__,
            *uId, size, mFreeBuffers.size(), mBuffersInUse.size());
        mStats->onBufferRecycled(size);
        return C2_OK;
    }

    ret = mAllocator->allocate(params, &buf);
    if (ret != C2_OK) {
        ALOGE("[%s] %s: buffer allocation failed\n", mName, __func__);
        return C2_NO_MEMORY;
    }
    mBuffersInUse.push_back(std::move(buf));
    *handle = mBuffersInUse.back()->allocation()->handle();
    *uId = mBuffersInUse.back()->allocation()->uId();
    size = mBuffersInUse.back()->allocation()->size();
    ALOGV("[%s] %s: buf-id %llu, size %u, free %u, inUse %u\n", mName, __func__,
        *uId, size, mFreeBuffers.size(), mBuffersInUse.size());

    return C2_OK;
}

void C2BasicBufferManager::returnHandle(uint64_t uId) {
    std::lock_guard<std::mutex> lock(mLock);
    uint32_t size;

    /* update current timestamp */
    mTimestampUs = getTimestampNow();

    auto evictBuffer = [&]() {
        /* evict buffers which is unused > 1 sec */
        evictUnusedBuffers_l();
    };
    EvictBuffer e(evictBuffer);

    auto it = std::find_if(
            mBuffersInUse.begin(), mBuffersInUse.end(),
            [&](const std::unique_ptr<InternalBuffer>& buf) -> bool {
                return buf->allocation()->uId() == uId;
            });
    if (it == mBuffersInUse.end()) {
        ALOGE("[%s] %s: not owned by client: buf-id %llu",
            mName, __func__, uId);
        return;
    }

    mFreeBuffers.splice(mFreeBuffers.end(), mBuffersInUse, it);
    mFreeBuffers.back()->updateExpire(mTimestampUs);
    size = mFreeBuffers.back()->allocation()->size();

    ALOGV("[%s] %s: buf-id %llu, size %u, free %u, inUse %u\n",
        mName, __func__, (*it)->allocation()->uId(), size,
        mFreeBuffers.size(), mBuffersInUse.size());

    mStats->onBufferUnused(size);
}

C2CustomContiguousBufferManager::C2CustomContiguousBufferManager(
    const std::shared_ptr<C2Allocator> &allocator,
    const std::shared_ptr<C2BufferStats> stats, const char *name)
    : mInit(C2_NO_INIT),
      mNextSlot(0),
      mStats(stats),
      mAllocator(std::make_unique<C2PoolAllocator>(allocator, stats)),
      mImplName(std::string(name) + ".CCBMImpl"),
      mName(mImplName.c_str()) {}

c2_status_t C2CustomContiguousBufferManager::initialize(
    const AllocBasicParams &params, uint32_t capacity) {
    std::lock_guard<std::mutex> lock(mLock);
    std::unique_ptr<InternalBuffer> buf;
    uint32_t bufferId;
    c2_status_t ret;
    mBuffers.clear();
    mAllocator->resetAllocIndex();
    mStats->clearStats();
    for (size_t i = 0; i < capacity; ++i) {
        bufferId = mBuffers.size();
        ret = mAllocator->allocate(params, &buf);
        if (ret != C2_OK) {
            ALOGE("[%s] %s: buffer allocation failed\n", mName, __func__);
            mInit = C2_CORRUPTED;
            return C2_CORRUPTED;
        }
        mBuffers.push_back({bufferId, std::move(buf), false});
    }
    mNextSlot = 0;
    mInit = C2_OK;
    return C2_OK;
}

c2_status_t C2CustomContiguousBufferManager::grabHandle(
    const AllocBasicParams &params, uint32_t *pId,
    uint64_t *uId, const native_handle_t** handle) {
    std::lock_guard<std::mutex> lock(mLock);
    int32_t size;
    if (mInit != C2_OK) {
        ALOGE("[%s] %s rejected. Not inited yet %u\n", mName, __func__, mInit);
        return mInit;
    }

    if (mNextSlot >= mBuffers.size()) {
        mNextSlot = 0;
    }

    if (!mBuffers[mNextSlot].ownedByClient) {
        mBuffers[mNextSlot].ownedByClient = true;
        *pId = mBuffers[mNextSlot].id;
        *handle = mBuffers[mNextSlot].buf->allocation()->handle();
        *uId = mBuffers[mNextSlot].buf->allocation()->uId();
        size = mBuffers[mNextSlot].buf->allocation()->size();
        ALOGV("[%s] %s: index %u, buf-id %llu, size %u\n",
            mName, __func__, *pId, *uId, size);
        mNextSlot++;
        mStats->onBufferRecycled(size);
        return C2_OK;
    }
    ALOGD("[%s] %s failed. index %u, buf-id %llu, clientOwn %u\n",
        mName, __func__, mNextSlot,
        mBuffers[mNextSlot].buf->allocation()->uId(),
        mBuffers[mNextSlot].ownedByClient);

    return C2_TIMED_OUT;
}

void C2CustomContiguousBufferManager::returnHandle(uint32_t pId, uint64_t uId) {
    std::lock_guard<std::mutex> lock(mLock);
    uint32_t size;

    if (mInit != C2_OK) {
        ALOGE("[%s] %s rejected. Not inited yet %u\n", mName, __func__, mInit);
        return;
    }

    if (pId >= mBuffers.size()) {
        ALOGE("[%s] %s: invalid index %u, buf-id %llu, capacity %u\n",
            mName, __func__, pId, uId, mBuffers.size());
        return;
    }

    if (mBuffers[pId].id != pId || mBuffers[pId].buf->allocation()->uId() != uId) {
        ALOGE("[%s] %s: param mismatch. [index, buf-id] actual[%u, %llu] expected[%u, %llu]\n",
            mName, __func__, pId, uId,
            mBuffers[pId].id, mBuffers[pId].buf->allocation()->uId());
        return;
    }

    if (!mBuffers[pId].ownedByClient) {
        ALOGE("[%s] %s: not owned by client: index %u, buf-id %llu",
            mName, __func__, pId, uId);
    }
    mBuffers[pId].ownedByClient = false;
    size = mBuffers[pId].buf->allocation()->uId();
    ALOGV("[%s] %s: index %u, buf-id %llu\n", mName, __func__, pId, uId);

    mStats->onBufferUnused(size);
}

// not supported
c2_status_t C2CustomContiguousBufferManager::grow(size_t newSize) {
    ALOGE("[%s] %s: not supported for contiguous pool\n",
        mName, __func__);

    return C2_OK;
}

c2_status_t C2CustomContiguousBufferManager::shrink(size_t newSize) {
    std::lock_guard<std::mutex> lock(mLock);

    if (mInit != C2_OK) {
        ALOGE("[%s] %s rejected. Not inited yet %u\n", mName, __func__, mInit);
        return mInit;
    }

    ALOGV("[%s] %s: contiguous pool %u -> %u\n",
        mName, __func__, mBuffers.size(), newSize);
    while (mBuffers.size() > newSize) {
        mBuffers.pop_back();
    }
    return C2_OK;
}

C2CustomNonContiguousBufferManager::C2CustomNonContiguousBufferManager(
    const std::shared_ptr<C2Allocator> &allocator,
    const std::shared_ptr<C2BufferStats> stats, const char *name)
    : mInit(C2_NO_INIT),
      mPoolCapacity(0),
      mStats(stats),
      mAllocator(std::make_unique<C2PoolAllocator>(allocator, stats)),
      mImplName(std::string(name) + ".CNCBMImpl"),
      mName(mImplName.c_str()) {}

c2_status_t C2CustomNonContiguousBufferManager::initialize(
    const AllocBasicParams &params, uint32_t capacity) {
    std::lock_guard<std::mutex> lock(mLock);
    mFreeBuffers.clear();
    mBuffersInUse.clear();
    mAllocator->resetAllocIndex();
    mStats->clearStats();
    mPoolCapacity = capacity;
    mInit = C2_OK;

    return C2_OK;
}

c2_status_t C2CustomNonContiguousBufferManager::grabHandle(
    const AllocBasicParams &params, uint32_t *pId,
    uint64_t *uId, const native_handle_t** handle) {
    std::lock_guard<std::mutex> lock(mLock);
    std::unique_ptr<InternalBuffer> buf;
    uint32_t bufferId, size;
    c2_status_t ret;

    if (mInit != C2_OK) {
        ALOGE("[%s] %s rejected. Not inited yet %u\n", mName, __func__, mInit);
        return mInit;
    }

    if (!mFreeBuffers.empty()) {
        mBuffersInUse.splice(mBuffersInUse.end(), mFreeBuffers, mFreeBuffers.begin());
        *pId = mBuffersInUse.back().id;
        *handle = mBuffersInUse.back().buf->allocation()->handle();
        *uId = mBuffersInUse.back().buf->allocation()->uId();
        size = mBuffersInUse.back().buf->allocation()->size();
        ALOGV("[%s] %s: index %u, buf-id %llu, size %u, free %u, inUse %u\n", mName, __func__,
            *pId, *uId, size, mFreeBuffers.size(), mBuffersInUse.size());
        mStats->onBufferRecycled(size);
        return C2_OK;
    }

    if (mBuffersInUse.size() < mPoolCapacity) {
        bufferId = mBuffersInUse.size();
        ret = mAllocator->allocate(params, &buf);
        if (ret != C2_OK) {
            ALOGE("[%s] %s: buffer allocation failed\n", mName, __func__);
            mInit = C2_CORRUPTED;
            return C2_CORRUPTED;
        }
        mBuffersInUse.push_back({bufferId, std::move(buf)});
        *pId = mBuffersInUse.back().id;
        *handle = mBuffersInUse.back().buf->allocation()->handle();
        *uId = mBuffersInUse.back().buf->allocation()->uId();
        size = mBuffersInUse.back().buf->allocation()->size();
        ALOGV("[%s] %s: index %u, buf-id %llu, size %u, free %u, inUse %u\n", mName, __func__,
            *pId, *uId, size, mFreeBuffers.size(), mBuffersInUse.size());
        return C2_OK;
    }

    ALOGD("[%s] %s failed. All handles were with client. free %u, inUse %u\n",
        mName, __func__, mFreeBuffers.size(), mBuffersInUse.size());

    return C2_TIMED_OUT;
}

void C2CustomNonContiguousBufferManager::returnHandle(uint32_t pId, uint64_t uId) {
    std::lock_guard<std::mutex> lock(mLock);
    int32_t size = 0;

    if (mInit != C2_OK) {
        ALOGE("[%s] %s rejected. Not inited yet %u\n", mName, __func__, mInit);
        return;
    }

    auto it = std::find_if(
            mBuffersInUse.begin(), mBuffersInUse.end(),
            [&](const Entry& entry) -> bool {
                return entry.id == pId && entry.buf->allocation()->uId() == uId;
            });
    if (it == mBuffersInUse.end()) {
        ALOGE("[%s] %s: not owned by client: index %lu, buf-id %llu",
            mName, __func__, pId, uId);
        return;
    }

    mFreeBuffers.splice(mFreeBuffers.end(), mBuffersInUse, it);
    size = (*it).buf->allocation()->size();

    ALOGV("[%s] %s: index %u, buf-id %llu, free %u, inUse %u\n",
        mName, __func__, (*it).id, (*it).buf->allocation()->uId(),
        mFreeBuffers.size(), mBuffersInUse.size());

    mStats->onBufferUnused(size);
}

c2_status_t C2CustomNonContiguousBufferManager::grow(size_t newSize) {
    std::lock_guard<std::mutex> lock(mLock);

    ALOGV("[%s] %s: increase pool capacity %u -> %u\n",
        mName, __func__, mPoolCapacity, newSize);
    mPoolCapacity = newSize;

    return C2_OK;
}

c2_status_t C2CustomNonContiguousBufferManager::shrink(size_t newSize) {
    std::lock_guard<std::mutex> lock(mLock);
    uint32_t totalAvailableBuffers;

    if (mInit != C2_OK) {
        ALOGE("[%s] %s rejected. Not inited yet %u\n", mName, __func__, mInit);
        return mInit;
    }

    totalAvailableBuffers = mFreeBuffers.size() + mBuffersInUse.size();

    if (totalAvailableBuffers <= newSize) {
        ALOGV("[%s] %s: capacity %u, free %u, inUse %u\n",
            mName, __func__, mPoolCapacity,
            mFreeBuffers.size(), mBuffersInUse.size());
        mPoolCapacity = newSize;
        return C2_OK;
    }

    while (totalAvailableBuffers > newSize) {
        if (!mFreeBuffers.empty()) {
            mFreeBuffers.pop_back();
            totalAvailableBuffers--;
        } else if (!mBuffersInUse.empty()) {
            mBuffersInUse.pop_back();
            totalAvailableBuffers--;
        }
    }
    ALOGV("[%s] %s: pool capacity %u -> %u, free %u, inUse %u\n",
        mName, __func__, mPoolCapacity, newSize,
        mFreeBuffers.size(), mBuffersInUse.size());

    mPoolCapacity = newSize;
    return C2_OK;
}

C2CustomBufferManager* getC2CustomBufferManager(
    const std::shared_ptr<C2Allocator> &allocator,
    const std::shared_ptr<C2BufferStats> stats,
    bool contiguous, const char* name) {
    C2CustomBufferManager* manager = nullptr;

    if(contiguous) {
        manager = new C2CustomContiguousBufferManager(allocator, stats, name);
    } else {
        manager = new C2CustomNonContiguousBufferManager(allocator, stats, name);
    }

    return manager;
}

class C2BasicBlockPool::Impl {
public:
    Impl(const std::shared_ptr<C2Allocator> &allocator,
        const char *name)
        : mImplName(std::string(name) + ".Impl"),
          mName(mImplName.c_str()),
          mStats(new C2BufferStats(mName)),
          mAllocator(allocator),
          mBufferPoolManager(new C2BasicBufferManager(allocator, mStats, mName)) {}

    virtual ~Impl() = default;

    c2_status_t fetchLinearBlock(
            uint32_t size,
            C2MemoryUsage usage,
            std::shared_ptr<C2LinearBlock> *block /* nonnull */) {
        std::shared_ptr<C2LinearAllocation> alloc;
        const native_handle_t *cHandle, *origHandle;
        uint64_t uniqueId;
        c2_status_t ret;
        block->reset();

        /* convert to BasicParam type */
        AllocBasicParams c2Linear(usage, size);

        /* allocate/acquire nativehandle */
        ret = mBufferPoolManager->grabHandle(c2Linear, &uniqueId, &origHandle);
        if (ret != C2_OK) {
            mStats->printStats();
            return ret;
        }

        /* check is it valid handle or not */
        bool isValidHandle = mAllocator->checkHandle(origHandle);
        if (!isValidHandle) {
            ALOGE("[%s] %s: invalid handle\n", mName, __func__);
            return C2_CORRUPTED;
        }

        /* clone nativehandle */
        cHandle = native_handle_clone(origHandle);

        /* import linear allocation using cloned handle */
        ret = mAllocator->priorLinearAllocation(cHandle, &alloc);
        if (ret == C2_OK && alloc) {
            *block = _C2BlockFactory::CreateLinearBlock(alloc,
                [manager = mBufferPoolManager,
                    uId = uniqueId](C2LinearBlock *blk) {
                    delete blk;
                    manager->returnHandle(uId);
                });
            if (*block) {
                return C2_OK;
            }
            ALOGE("[%s] %s: CreateLinearBlock failed\n", mName, __func__);
            return C2_NO_MEMORY;
        }

        return C2_CORRUPTED;
    }

    c2_status_t fetchGraphicBlock(
            uint32_t width,
            uint32_t height,
            uint32_t format,
            C2MemoryUsage usage,
            std::shared_ptr<C2GraphicBlock> *block) {
        std::shared_ptr<C2GraphicAllocation> alloc;
        const native_handle_t *cHandle, *origHandle;
        uint64_t uniqueId;
        c2_status_t ret;
        block->reset();

        /* convert to BasicParam type */
        AllocBasicParams c2Graphic(usage, width, height, format);

        /* allocate/acquire nativehandle */
        ret = mBufferPoolManager->grabHandle(c2Graphic, &uniqueId, &origHandle);
        if (ret != C2_OK) {
            mStats->printStats();
            return ret;
        }

        /* check is it valid handle or not */
        bool isValidHandle = mAllocator->checkHandle(origHandle);
        if (!isValidHandle) {
            ALOGE("[%s] %s: invalid handle\n", mName, __func__);
            return C2_CORRUPTED;
        }

        /* clone nativehandle */
        cHandle = native_handle_clone(origHandle);

        /* import graphic allocation using cloned handle */
        ret = mAllocator->priorGraphicAllocation(cHandle, &alloc);
        if (ret == C2_OK && alloc) {
            *block = _C2BlockFactory::CreateGraphicBlock(alloc,
                [manager = mBufferPoolManager,
                    uId = uniqueId](C2GraphicBlock *blk) {
                    delete blk;
                    manager->returnHandle(uId);
                });
            if (*block) {
                return C2_OK;
            }
            ALOGE("[%s] %s: CreateGraphicBlock failed\n", mName, __func__);
            return C2_NO_MEMORY;
        }

        return C2_CORRUPTED;
    }

private:
    std::string mImplName;
    const char* mName;
    const std::shared_ptr<C2BufferStats> mStats;
    std::shared_ptr<C2Allocator> mAllocator;
    std::shared_ptr<C2BasicBufferManager> mBufferPoolManager;
};

C2BasicBlockPool::C2BasicBlockPool(
        const std::shared_ptr<C2Allocator> &allocator,
        const uint32_t poolType, const local_id_t localId)
        : mAllocator(allocator),
          mLocalId(localId),
          mPoolType(poolType),
          mImpl(new Impl(allocator, std::string("BBP-" + getPoolTypeString(poolType) +
            std::string("-P") + std::to_string(localId) + "-A" +
            std::to_string(allocator->getId())).c_str())) {}

C2BasicBlockPool::~C2BasicBlockPool() {
}

c2_status_t C2BasicBlockPool::fetchLinearBlock(
        uint32_t size,
        C2MemoryUsage usage,
        std::shared_ptr<C2LinearBlock> *block /* nonnull */) {
    C2TRACED("C2Buffer::fetchLinearBlock");
    if (mImpl) {
        return mImpl->fetchLinearBlock(size, usage, block);
    }
    return C2_CORRUPTED;
}

c2_status_t C2BasicBlockPool::fetchGraphicBlock(
        uint32_t width,
        uint32_t height,
        uint32_t format,
        C2MemoryUsage usage,
        std::shared_ptr<C2GraphicBlock> *block) {
    C2TRACED("C2Buffer::fetchGraphicBlock");
    if (mImpl) {
        return mImpl->fetchGraphicBlock(width, height, format, usage, block);
    }
    return C2_CORRUPTED;
}

template<typename T>
bool isInBounds(const T& val, const T& min, const T& max) {
    return min <= val && val <= max;
}

class C2CustomBlockPool::Impl {
public:
    Impl(const std::shared_ptr<C2Allocator> &allocator,
        const bool contiguous, const char* name)
            : mInit(C2_NO_INIT),
              mImplName(std::string(name) + ".Impl"),
              mName(mImplName.c_str()),
              mIsContiguousPool(contiguous),
              mStats(new C2BufferStats(mName)),
              mAllocator(allocator),
              mBufferPoolManager(getC2CustomBufferManager(allocator, mStats, contiguous, mName)),
              mPoolCapacity(kMinPoolCapacity) {
    }

    virtual ~Impl() = default;

    c2_status_t initPool(const AllocBasicParams &params, uint32_t poolCapacity) {
        c2_status_t ret;

        if (params.data.allocType == AllocBasicParams::ALLOC_LINEAR) {
            mConfig = params;
            mPoolCapacity = poolCapacity;

            ret = mBufferPoolManager->initialize(mConfig, mPoolCapacity);
            if (ret != C2_OK) {
                ALOGE("[%s] %s: failed %d\n", mName, __func__, ret);
                mInit = ret;
                return ret;
            }
            mInit = C2_OK;
            ALOGD("[%s] Pool inited successfully. size %u, usage %#llx, count %d\n",
                mName, mConfig.data.params[0], mConfig.data.usage.expected, mPoolCapacity);
        } else if (params.data.allocType == AllocBasicParams::ALLOC_GRAPHIC) {
            mConfig = params;
            mPoolCapacity = poolCapacity;

            ret = mBufferPoolManager->initialize(mConfig, mPoolCapacity);
            if (ret != C2_OK) {
                ALOGE("[%s] %s failed %d\n", mName, __func__, ret);
                mInit = ret;
                return ret;
            }
            mInit = C2_OK;
            ALOGD("[%s] Pool inited successfully. w %u, h %u, f %#llx, u %#llx, count %d\n",
                mName,
                mConfig.data.params[0],
                mConfig.data.params[1],
                mConfig.data.params[2],
                mConfig.data.usage.expected, mPoolCapacity);

        }
        return C2_OK;
    };

    c2_status_t shrinkPool(const AllocBasicParams &params, uint32_t poolCapacity) {
        c2_status_t ret = C2_OK;
        const char *poolName;

        if (params.data.allocType == AllocBasicParams::ALLOC_LINEAR ||
            params.data.allocType == AllocBasicParams::ALLOC_GRAPHIC) {
            ret = mBufferPoolManager->shrink(poolCapacity);
            if (ret != C2_OK) {
                ALOGE("[%s] %s failed %u -> %u. ret %d\n",
                    mName, __func__, mPoolCapacity, poolCapacity, ret);
                mInit = ret;
                return ret;
            }
            poolName = (params.data.allocType ==
                            AllocBasicParams::ALLOC_LINEAR) ? "Linear" : "Graphic";
            ALOGD("[%s] shrink%sPool: successful. pool shrinked %u -> %u\n",
                mName, poolName, mPoolCapacity, poolCapacity);
            mPoolCapacity = poolCapacity;

            return ret;
        }
        return ret;
    };

    c2_status_t growPool(const AllocBasicParams &params, uint32_t poolCapacity) {
        c2_status_t ret = C2_OK;
        const char *poolName;

        if (params.data.allocType == AllocBasicParams::ALLOC_LINEAR ||
            params.data.allocType == AllocBasicParams::ALLOC_GRAPHIC) {
            ret = mBufferPoolManager->grow(poolCapacity);
            if (ret != C2_OK) {
                ALOGE("[%s] %s failed %u -> %u. ret %d\n",
                    mName, __func__, mPoolCapacity, poolCapacity, ret);
                mInit = ret;
                return ret;
            }
            poolName = (params.data.allocType ==
                            AllocBasicParams::ALLOC_LINEAR) ? "Linear" : "Graphic";
            ALOGD("[%s] grow%sPool: successful. pool grown %u -> %u\n",
                mName, poolName, mPoolCapacity, poolCapacity);
            mPoolCapacity = poolCapacity;
        }
        return ret;
    };

    c2_status_t init(const AllocBasicParams &c2Params, uint32_t poolCapacity) {
        std::lock_guard<std::mutex> lock(mLock);
        const char *func = __func__;
        c2_status_t ret = C2_OK;

        auto isReinitNeeded = [&](const AllocBasicParams &params) -> bool {
            bool ret = false;

            if (params.data.allocType != mConfig.data.allocType ||
                params.data.usage.expected != mConfig.data.usage.expected) {
                ret = true;
            } else if (params.data.allocType == AllocBasicParams::ALLOC_LINEAR) {
                ret = params.data.params[0] != mConfig.data.params[0];
            } else if (params.data.allocType == AllocBasicParams::ALLOC_GRAPHIC) {
                ret = (params.data.params[0] != mConfig.data.params[0] ||
                        params.data.params[1] != mConfig.data.params[1] ||
                        params.data.params[2] != mConfig.data.params[2]);
            }
            return ret;
        };

        auto isCountUpdateNeeded = [&](uint32_t poolCapacity) -> bool {
            return poolCapacity != mPoolCapacity;
        };

        auto isValidParam = [&](const AllocBasicParams &params, uint32_t capacity) -> bool {
            if (!isInBounds(capacity, kMinPoolCapacity, kMaxPoolCapacity)) {
                ALOGE("[%s] %s: invalid param. capacity %u\n",
                    mName, func, poolCapacity);
                return false;
            }
            if (params.data.allocType == AllocBasicParams::ALLOC_LINEAR) {
                if (!params.data.params[0]) {
                    ALOGE("[%s] %s: Linear pool. invalid param. size %u\n",
                        mName, func, poolCapacity);
                    return false;
                }
            } else if (params.data.allocType == AllocBasicParams::ALLOC_GRAPHIC) {
                if (!params.data.params[0] || !params.data.params[1] ||
                    !params.data.params[2]) {
                    ALOGE("[%s] %s: Graphic pool. invalid param."
                            " w %u, h %u, f %#llx, u %#llx, capacity %u\n",
                        mName, func, params.data.params[0],
                        params.data.params[1],
                        params.data.params[2],
                        params.data.usage.expected, poolCapacity);
                    return false;
                }
            }
            return true;
        };

        if (!isValidParam(c2Params, poolCapacity)) {
            mInit = C2_CORRUPTED;
            return C2_CORRUPTED;
        }

        /* init pool for basic param change */
        if (isReinitNeeded(c2Params)) {
            ret = initPool(c2Params, poolCapacity);
            if (ret != C2_OK) {
                return ret;
            }
        } else if (isCountUpdateNeeded(poolCapacity)) {
            /* shrink pool - allowed for both contiguous and non-contiguous pool */
            if (poolCapacity < mPoolCapacity) {
                ret = shrinkPool(c2Params, poolCapacity);
                if (ret != C2_OK) {
                    return ret;
                }
            } else {
                /* grow non-contiguous pool */
                if (!mIsContiguousPool) {
                    ret = growPool(c2Params, poolCapacity);
                    if (ret != C2_OK) {
                        return ret;
                    }
                } else {
                    /* growing contiguous pool not alloed. Reinit again */
                    ret = initPool(c2Params, poolCapacity);
                    if (ret != C2_OK) {
                        return ret;
                    }
                }
            }
        }
        mStats->printStats();

        return C2_OK;
    }

    c2_status_t fetchLinearBlock(
            uint32_t size, C2MemoryUsage usage,
            std::shared_ptr<C2LinearBlock> *block /* nonnull */) {
        std::lock_guard<std::mutex> lock(mLock);
        std::shared_ptr<C2LinearAllocation> alloc;
        const native_handle_t *cHandle, *origHandle;
        uint64_t uniqueId;
        uint32_t bufferId;
        c2_status_t ret;

        auto isParamMismatch = [&](uint32_t size, C2MemoryUsage usage) -> bool {
            return size != mConfig.data.params[0] || usage.expected != mConfig.data.usage.expected;
        };

        block->reset();

        /* convert to BasicParam type */
        AllocBasicParams c2Linear(usage, size);

        if (mInit == C2_CORRUPTED) {
            return mInit;
        } else if (mInit == C2_NO_INIT) {
            /* init pool if not inited so far */
            ret = initPool(c2Linear, mPoolCapacity);
            if (ret != C2_OK) {
                return ret;
            }
        }

        /* sanitize input param */
        if (isParamMismatch(size, usage)) {
            ALOGE("[%s] %s: invalid param[size, usage] %u, %#llx -> %u, %#llx\n",
                mName, __func__,
                mConfig.data.params[0],
                mConfig.data.usage.expected,
                size, usage.expected);
            mInit = C2_CORRUPTED;
            return C2_CORRUPTED;
        }

        /* allocate/acquire nativehandle */
        ret = mBufferPoolManager->grabHandle(c2Linear, &bufferId, &uniqueId, &origHandle);
        if (ret != C2_OK) {
            mStats->printStats();
            return ret;
        }

        /* check is it valid handle or not */
        bool isValidHandle = mAllocator->checkHandle(origHandle);
        if (!isValidHandle) {
            ALOGE("[%s] %s: invalid handle\n", mName, __func__);
            return C2_CORRUPTED;
        }

        /* clone nativehandle */
        cHandle = native_handle_clone(origHandle);

        /* import linear allocation using cloned handle */
        ret = mAllocator->priorLinearAllocation(cHandle, &alloc);
        if (ret == C2_OK && alloc) {
            *block = _C2BlockFactory::CreateLinearBlock(alloc,
                [manager = mBufferPoolManager, bId = bufferId,
                    uId = uniqueId](C2LinearBlock *blk) {
                    delete blk;
                    manager->returnHandle(bId, uId);
                });
            if (*block) {
                return C2_OK;
            }
            ALOGE("[%s] %s: CreateLinearBlock failed\n", mName, __func__);
            return C2_NO_MEMORY;
        }

        return C2_OK;
    }

    c2_status_t fetchGraphicBlock(uint32_t width, uint32_t height,
        uint32_t format, C2MemoryUsage usage,
        std::shared_ptr<C2GraphicBlock> *block) {
        std::lock_guard<std::mutex> lock(mLock);
        std::shared_ptr<C2GraphicAllocation> alloc;
        const native_handle_t *cHandle, *origHandle;
        uint64_t uniqueId;
        uint32_t bufferId;
        c2_status_t ret;

        auto isParamMismatch = [&](uint32_t width, uint32_t height,
            uint32_t format, C2MemoryUsage usage) -> bool {
            return width != mConfig.data.params[0] ||
                height != mConfig.data.params[1] ||
                format != mConfig.data.params[2] ||
                usage.expected != mConfig.data.usage.expected;
        };

        /* convert to BasicParam type */
        AllocBasicParams c2Graphic(usage, width, height, format);

        block->reset();
        if (mInit == C2_CORRUPTED) {
            return mInit;
        } else if (mInit == C2_NO_INIT) {
            /* init pool if not inited so far */
            ret = initPool(c2Graphic, mPoolCapacity);
            if (ret != C2_OK) {
                return ret;
            }
        }

        /* sanitize input param */
        if (isParamMismatch(width, height, format, usage)) {
            ALOGE("[%s] %s: invalid param[w, h, f, u] %u, %u, %#llx, %#llx -> %u, %u, %#llx, %#llx",
                mName, __func__, mConfig.data.params[0], mConfig.data.params[1],
                mConfig.data.params[2], mConfig.data.usage.expected,
                width, height, format, usage.expected);
            mInit = C2_CORRUPTED;
            return C2_CORRUPTED;
        }

        /* allocate/acquire nativehandle */
        ret = mBufferPoolManager->grabHandle(c2Graphic, &bufferId, &uniqueId, &origHandle);
        if (ret != C2_OK) {
            mStats->printStats();
            return ret;
        }

        /* check is it valid handle or not */
        bool isValidHandle = mAllocator->checkHandle(origHandle);
        if (!isValidHandle) {
            ALOGE("[%s] %s: invalid handle\n", mName, __func__);
            return C2_CORRUPTED;
        }

        /* clone nativehandle */
        cHandle = native_handle_clone(origHandle);

        /* import graphic allocation using cloned handle */
        ret = mAllocator->priorGraphicAllocation(cHandle, &alloc);
        if (ret == C2_OK && alloc) {
            *block = _C2BlockFactory::CreateGraphicBlock(alloc,
                [manager = mBufferPoolManager, bId = bufferId,
                    uId = uniqueId](C2GraphicBlock *blk) {
                    delete blk;
                    manager->returnHandle(bId, uId);
                });
            if (*block) {
                return C2_OK;
            }
            ALOGE("[%s] %s: CreateGraphicBlock failed\n", mName, __func__);
            return C2_NO_MEMORY;
        }

        return C2_OK;
    }

private:
    c2_status_t mInit;
    std::mutex mLock;
    uint32_t mPoolCapacity;
    AllocBasicParams mConfig;
    std::string mImplName;
    const char* mName;
    const bool mIsContiguousPool;
    const std::shared_ptr<C2BufferStats> mStats;
    const std::shared_ptr<C2Allocator> mAllocator;
    const std::shared_ptr<C2CustomBufferManager> mBufferPoolManager;
    static constexpr uint32_t kMinPoolCapacity = 2;
    static constexpr uint32_t kMaxPoolCapacity = 64;
};

C2CustomBlockPool::C2CustomBlockPool(
        const std::shared_ptr<C2Allocator> &allocator,
        const uint32_t poolType, const local_id_t localId, const bool contiguous)
        : mAllocator(allocator),
          mLocalId(localId),
          mPoolType(poolType),
          mImpl(new Impl(allocator, contiguous, std::string("CBP-" + getPoolTypeString(poolType) +
            std::string("-P") + std::to_string(localId) + "-A" +
            std::to_string(allocator->getId())).c_str())) {}

C2CustomBlockPool::~C2CustomBlockPool() {
}

c2_status_t C2CustomBlockPool::fetchLinearBlock(
        uint32_t size,
        C2MemoryUsage usage,
        std::shared_ptr<C2LinearBlock> *block /* nonnull */) {
    C2TRACED("C2Buffer::fetchLinearBlock");
    if (mImpl) {
        return mImpl->fetchLinearBlock(size, usage, block);
    }
    return C2_CORRUPTED;
}

c2_status_t C2CustomBlockPool::fetchGraphicBlock(
        uint32_t width,
        uint32_t height,
        uint32_t format,
        C2MemoryUsage usage,
        std::shared_ptr<C2GraphicBlock> *block) {
    C2TRACED("C2Buffer::fetchGraphicBlock");
    if (mImpl) {
        return mImpl->fetchGraphicBlock(width, height, format, usage, block);
    }
    return C2_CORRUPTED;
}

c2_status_t C2CustomBlockPool::init(const AllocBasicParams &c2Params, uint32_t poolCapacity) {
    if (mImpl) {
        return mImpl->init(c2Params, poolCapacity);
    }
    return C2_CORRUPTED;
}

/* ========================================== 2D BLOCK ========================================= */

/**
 * Implementation that is shared between all 2D blocks and views.
 *
 * For blocks' Impl's crop is always the allotted crop, even if it is a sub block.
 *
 * For views' Impl's crop is the mapped portion - which for now is always the
 * allotted crop.
 */
class C2_HIDE _C2Block2DImpl : public _C2PlanarSectionAspect {
public:
    /**
     * Impl's crop is always the or part of the allotted crop of the allocation.
     */
    _C2Block2DImpl(const std::shared_ptr<C2GraphicAllocation> &alloc,
            const C2Rect &allottedCrop = C2Rect(~0u, ~0u))
        : _C2PlanarSectionAspect(alloc.get(), allottedCrop),
          mAllocation(alloc) { }

    virtual ~_C2Block2DImpl() = default;

    /** returns native handle */
    const C2Handle *handle() const {
        return mAllocation ? mAllocation->handle() : nullptr;
    }

    /** returns the allocator's ID */
    C2Allocator::id_t getAllocatorId() const {
        // BAD_ID can only happen if this Impl class is initialized for a view - never for a block.
        return mAllocation ? mAllocation->getAllocatorId() : C2Allocator::BAD_ID;
    }

    std::shared_ptr<C2GraphicAllocation> getAllocation() const {
        return mAllocation;
    }

private:
    std::shared_ptr<C2GraphicAllocation> mAllocation;
};

class C2_HIDE _C2MappingBlock2DImpl
    : public _C2Block2DImpl, public std::enable_shared_from_this<_C2MappingBlock2DImpl> {
public:
    using _C2Block2DImpl::_C2Block2DImpl;

    virtual ~_C2MappingBlock2DImpl() override = default;

    /**
     * This class contains the mapped data pointer, and the potential error.
     */
    struct Mapped {
    private:
        friend class _C2MappingBlock2DImpl;

        Mapped(const std::shared_ptr<_C2Block2DImpl> &impl, bool writable, C2Fence *fence __unused)
            : mImpl(impl), mWritable(writable) {
            memset(mData, 0, sizeof(mData));
            const C2Rect crop = mImpl->crop();
            // gralloc requires mapping the whole region of interest as we cannot
            // map multiple regions
            mError = mImpl->getAllocation()->map(
                    crop,
                    { C2MemoryUsage::CPU_READ, writable ? C2MemoryUsage::CPU_WRITE : 0 },
                    nullptr,
                    &mLayout,
                    mData);
            if (mError != C2_OK) {
                memset(&mLayout, 0, sizeof(mLayout));
                memset(mData, 0, sizeof(mData));
                memset(mOffsetData, 0, sizeof(mData));
            } else {
                // TODO: validate plane layout and
                // adjust data pointers to the crop region's top left corner.
                // fail if it is not on a subsampling boundary
                for (size_t planeIx = 0; planeIx < mLayout.numPlanes; ++planeIx) {
                    const uint32_t colSampling = mLayout.planes[planeIx].colSampling;
                    const uint32_t rowSampling = mLayout.planes[planeIx].rowSampling;
                    if (crop.left % colSampling || crop.right() % colSampling
                            || crop.top % rowSampling || crop.bottom() % rowSampling) {
                        // cannot calculate data pointer
                        mImpl->getAllocation()->unmap(mData, crop, nullptr);
                        memset(&mLayout, 0, sizeof(mLayout));
                        memset(mData, 0, sizeof(mData));
                        memset(mOffsetData, 0, sizeof(mData));
                        mError = C2_BAD_VALUE;
                        return;
                    }
                    mOffsetData[planeIx] =
                        mData[planeIx] + (ssize_t)crop.left * mLayout.planes[planeIx].colInc
                                + (ssize_t)crop.top * mLayout.planes[planeIx].rowInc;
                }
            }
        }

        explicit Mapped(c2_status_t error)
            : mImpl(nullptr), mWritable(false), mError(error) {
            // CHECK(error != C2_OK);
            memset(&mLayout, 0, sizeof(mLayout));
            memset(mData, 0, sizeof(mData));
            memset(mOffsetData, 0, sizeof(mData));
        }

    public:
        ~Mapped() {
            if (mData[0] != nullptr) {
                mImpl->getAllocation()->unmap(mData, mImpl->crop(), nullptr);
            }
        }

        /** returns mapping status */
        c2_status_t error() const { return mError; }

        /** returns data pointer */
        uint8_t *const *data() const { return mOffsetData; }

        /** returns the plane layout */
        C2PlanarLayout layout() const { return mLayout; }

        /** returns whether the mapping is writable */
        bool writable() const { return mWritable; }

    private:
        const std::shared_ptr<_C2Block2DImpl> mImpl;
        bool mWritable;
        c2_status_t mError;
        uint8_t *mData[C2PlanarLayout::MAX_NUM_PLANES];
        uint8_t *mOffsetData[C2PlanarLayout::MAX_NUM_PLANES];
        C2PlanarLayout mLayout;
    };

    /**
     * Maps the allotted region.
     *
     * If already mapped and it is currently in use, returns the existing mapping.
     * If fence is provided, an acquire fence is stored there.
     */
    std::shared_ptr<Mapped> map(bool writable, C2Fence *fence) {
        std::lock_guard<std::mutex> lock(mMappedLock);
        std::shared_ptr<Mapped> existing = mMapped.lock();
        if (!existing) {
            existing = std::shared_ptr<Mapped>(new Mapped(shared_from_this(), writable, fence));
            mMapped = existing;
        } else {
            // if we mapped the region read-only, we cannot remap it read-write
            if (writable && !existing->writable()) {
                existing = std::shared_ptr<Mapped>(new Mapped(C2_CANNOT_DO));
            }
            if (fence != nullptr) {
                *fence = C2Fence();
            }
        }
        return existing;
    }

private:
    std::weak_ptr<Mapped> mMapped;
    std::mutex mMappedLock;
};

class C2_HIDE _C2MappedBlock2DImpl : public _C2Block2DImpl {
public:
    _C2MappedBlock2DImpl(const _C2Block2DImpl &impl,
                         std::shared_ptr<_C2MappingBlock2DImpl::Mapped> mapping)
        : _C2Block2DImpl(impl), mMapping(mapping) {
    }

    virtual ~_C2MappedBlock2DImpl() override = default;

    std::shared_ptr<_C2MappingBlock2DImpl::Mapped> mapping() const { return mMapping; }

private:
    std::shared_ptr<_C2MappingBlock2DImpl::Mapped> mMapping;
};

/**
 * Block implementation.
 */
class C2Block2D::Impl : public _C2MappingBlock2DImpl {
public:
    using _C2MappingBlock2DImpl::_C2MappingBlock2DImpl;
    virtual ~Impl() override = default;
};

const C2Handle *C2Block2D::handle() const {
    return mImpl->handle();
}

C2Allocator::id_t C2Block2D::getAllocatorId() const {
    return mImpl->getAllocatorId();
}

C2Block2D::C2Block2D(std::shared_ptr<Impl> impl, const _C2PlanarSectionAspect &section)
    // always clamp subsection to parent (impl) crop for safety
    : _C2PlanarSectionAspect(impl.get(), section.crop()), mImpl(impl) {
}

/**
 * Graphic view implementation.
 *
 * range of Impl is the mapped range of the underlying allocation. range of View is the current
 * crop.
 */
class C2GraphicView::Impl : public _C2MappedBlock2DImpl {
public:
    using _C2MappedBlock2DImpl::_C2MappedBlock2DImpl;
    virtual ~Impl() override = default;
};

C2GraphicView::C2GraphicView(std::shared_ptr<Impl> impl, const _C2PlanarSectionAspect &section)
    : _C2EditablePlanarSectionAspect(impl.get(), section.crop()), mImpl(impl) {
}

const uint8_t *const *C2GraphicView::data() const {
    return mImpl->mapping()->data();
}

uint8_t *const *C2GraphicView::data() {
    return mImpl->mapping()->data();
}

const C2PlanarLayout C2GraphicView::layout() const {
    return mImpl->mapping()->layout();
}

const C2GraphicView C2GraphicView::subView(const C2Rect &rect) const {
    return C2GraphicView(mImpl, C2PlanarSection(*mImpl, rect));
}

C2GraphicView C2GraphicView::subView(const C2Rect &rect) {
    return C2GraphicView(mImpl, C2PlanarSection(*mImpl, rect));
}

c2_status_t C2GraphicView::error() const {
    return mImpl->mapping()->error();
}

/**
 * Const graphic block implementation.
 */
C2ConstGraphicBlock::C2ConstGraphicBlock(
        std::shared_ptr<Impl> impl, const _C2PlanarSectionAspect &section, C2Fence fence)
    : C2Block2D(impl, section), mFence(fence) { }

C2Acquirable<const C2GraphicView> C2ConstGraphicBlock::map() const {
    C2Fence fence;
    std::shared_ptr<_C2MappingBlock2DImpl::Mapped> mapping =
        mImpl->map(false /* writable */, &fence);
    std::shared_ptr<GraphicViewBuddy::Impl> gvi =
        std::shared_ptr<GraphicViewBuddy::Impl>(new GraphicViewBuddy::Impl(*mImpl, mapping));
    return AcquirableConstGraphicViewBuddy(
            mapping->error(), fence, GraphicViewBuddy(gvi, C2PlanarSection(*mImpl, crop())));
}

C2ConstGraphicBlock C2ConstGraphicBlock::subBlock(const C2Rect &rect) const {
    return C2ConstGraphicBlock(mImpl, C2PlanarSection(*mImpl, crop().intersect(rect)), mFence);
}

/**
 * Graphic block implementation.
 */
C2GraphicBlock::C2GraphicBlock(
    std::shared_ptr<Impl> impl, const _C2PlanarSectionAspect &section)
    : C2Block2D(impl, section) { }

C2Acquirable<C2GraphicView> C2GraphicBlock::map() {
    C2Fence fence;
    std::shared_ptr<_C2MappingBlock2DImpl::Mapped> mapping =
        mImpl->map(true /* writable */, &fence);
    std::shared_ptr<GraphicViewBuddy::Impl> gvi =
        std::shared_ptr<GraphicViewBuddy::Impl>(new GraphicViewBuddy::Impl(*mImpl, mapping));
    return AcquirableGraphicViewBuddy(
            mapping->error(), fence, GraphicViewBuddy(gvi, C2PlanarSection(*mImpl, crop())));
}

C2ConstGraphicBlock C2GraphicBlock::share(const C2Rect &crop, C2Fence fence) {
    return ConstGraphicBlockBuddy(mImpl, C2PlanarSection(*mImpl, crop), fence);
}

/**
 * Basic block pool implementations.
 */
C2BasicGraphicBlockPool::C2BasicGraphicBlockPool(
        const std::shared_ptr<C2Allocator> &allocator)
  : mAllocator(allocator) {}

c2_status_t C2BasicGraphicBlockPool::fetchGraphicBlock(
        uint32_t width,
        uint32_t height,
        uint32_t format,
        C2MemoryUsage usage,
        std::shared_ptr<C2GraphicBlock> *block /* nonnull */) {
    C2TRACED("C2Buffer::fetchGraphicBlock");
    block->reset();

    std::shared_ptr<C2GraphicAllocation> alloc;
    c2_status_t err = mAllocator->newGraphicAllocation(width, height, format, usage, &alloc);
    if (err != C2_OK) {
        return err;
    }

    *block = _C2BlockFactory::CreateGraphicBlock(alloc);

    return C2_OK;
}

std::shared_ptr<C2GraphicBlock> _C2BlockFactory::CreateGraphicBlock(
        const std::shared_ptr<C2GraphicAllocation> &alloc,
        std::function<void(C2GraphicBlock*)> deleter) {
    std::shared_ptr<C2Block2D::Impl> impl =
        std::make_shared<C2Block2D::Impl>(alloc);
    return std::shared_ptr<C2GraphicBlock>(new C2GraphicBlock(impl, *impl), deleter);
}

std::shared_ptr<C2GraphicBlock> _C2BlockFactory::CreateGraphicBlock(
        const std::shared_ptr<C2GraphicAllocation> &alloc,
        const C2Rect &allottedCrop) {
    std::shared_ptr<C2Block2D::Impl> impl =
        std::make_shared<C2Block2D::Impl>(alloc, allottedCrop);
    return std::shared_ptr<C2GraphicBlock>(new C2GraphicBlock(impl, *impl));
}

/* ========================================== BUFFER ========================================= */

class C2BufferData::Impl {
public:
    explicit Impl(const std::vector<C2ConstLinearBlock> &blocks)
        : mType(blocks.size() == 1 ? LINEAR : LINEAR_CHUNKS),
          mLinearBlocks(blocks) {
    }

    explicit Impl(const std::vector<C2ConstGraphicBlock> &blocks)
        : mType(blocks.size() == 1 ? GRAPHIC : GRAPHIC_CHUNKS),
          mGraphicBlocks(blocks) {
    }

    type_t type() const { return mType; }
    const std::vector<C2ConstLinearBlock> &linearBlocks() const { return mLinearBlocks; }
    const std::vector<C2ConstGraphicBlock> &graphicBlocks() const { return mGraphicBlocks; }

private:
    type_t mType;
    std::vector<C2ConstLinearBlock> mLinearBlocks;
    std::vector<C2ConstGraphicBlock> mGraphicBlocks;
    friend class C2InfoBuffer;
};

C2BufferData::C2BufferData(const std::vector<C2ConstLinearBlock> &blocks) : mImpl(new Impl(blocks)) {}
C2BufferData::C2BufferData(const std::vector<C2ConstGraphicBlock> &blocks) : mImpl(new Impl(blocks)) {}

C2BufferData::type_t C2BufferData::type() const { return mImpl->type(); }

const std::vector<C2ConstLinearBlock> C2BufferData::linearBlocks() const {
    return mImpl->linearBlocks();
}

const std::vector<C2ConstGraphicBlock> C2BufferData::graphicBlocks() const {
    return mImpl->graphicBlocks();
}

C2InfoBuffer::C2InfoBuffer(
    C2Param::Index index, const std::vector<C2ConstLinearBlock> &blocks)
    : mIndex(index), mData(BufferDataBuddy(blocks)) {
}

C2InfoBuffer::C2InfoBuffer(
    C2Param::Index index, const std::vector<C2ConstGraphicBlock> &blocks)
    : mIndex(index), mData(BufferDataBuddy(blocks)) {
}

C2InfoBuffer::C2InfoBuffer(
    C2Param::Index index, const C2BufferData &data)
    : mIndex(index), mData(data) {
}

// static
C2InfoBuffer C2InfoBuffer::CreateLinearBuffer(
        C2Param::CoreIndex index, const C2ConstLinearBlock &block) {
    return C2InfoBuffer(index.coreIndex() | C2Param::Index::KIND_INFO | C2Param::Index::DIR_GLOBAL,
                        { block });
}

// static
C2InfoBuffer C2InfoBuffer::CreateGraphicBuffer(
        C2Param::CoreIndex index, const C2ConstGraphicBlock &block) {
    return C2InfoBuffer(index.coreIndex() | C2Param::Index::KIND_INFO | C2Param::Index::DIR_GLOBAL,
                        { block });
}

class C2Buffer::Impl {
public:
    Impl(C2Buffer *thiz, const std::vector<C2ConstLinearBlock> &blocks)
        : mThis(thiz), mData(blocks) {}
    Impl(C2Buffer *thiz, const std::vector<C2ConstGraphicBlock> &blocks)
        : mThis(thiz), mData(blocks) {}

    ~Impl() {
        for (const auto &pair : mNotify) {
            pair.first(mThis, pair.second);
        }
    }

    const C2BufferData &data() const { return mData; }

    c2_status_t registerOnDestroyNotify(OnDestroyNotify onDestroyNotify, void *arg) {
        auto it = std::find_if(
                mNotify.begin(), mNotify.end(),
                [onDestroyNotify, arg] (const auto &pair) {
                    return pair.first == onDestroyNotify && pair.second == arg;
                });
        if (it != mNotify.end()) {
            return C2_DUPLICATE;
        }
        mNotify.emplace_back(onDestroyNotify, arg);
        return C2_OK;
    }

    c2_status_t unregisterOnDestroyNotify(OnDestroyNotify onDestroyNotify, void *arg) {
        auto it = std::find_if(
                mNotify.begin(), mNotify.end(),
                [onDestroyNotify, arg] (const auto &pair) {
                    return pair.first == onDestroyNotify && pair.second == arg;
                });
        if (it == mNotify.end()) {
            return C2_NOT_FOUND;
        }
        mNotify.erase(it);
        return C2_OK;
    }

    std::vector<std::shared_ptr<const C2Info>> info() const {
        std::vector<std::shared_ptr<const C2Info>> result(mInfos.size());
        std::transform(
                mInfos.begin(), mInfos.end(), result.begin(),
                [] (const auto &elem) { return elem.second; });
        return result;
    }

    c2_status_t setInfo(const std::shared_ptr<C2Info> &info) {
        // To "update" you need to erase the existing one if any, and then insert.
        (void) mInfos.erase(info->coreIndex());
        (void) mInfos.insert({ info->coreIndex(), info });
        return C2_OK;
    }

    bool hasInfo(C2Param::Type index) const {
        return mInfos.count(index.coreIndex()) > 0;
    }

    std::shared_ptr<const C2Info> getInfo(C2Param::Type index) const {
        auto it = mInfos.find(index.coreIndex());
        if (it == mInfos.end()) {
            return nullptr;
        }
        return std::const_pointer_cast<const C2Info>(it->second);
    }

    std::shared_ptr<C2Info> removeInfo(C2Param::Type index) {
        auto it = mInfos.find(index.coreIndex());
        if (it == mInfos.end()) {
            return nullptr;
        }
        std::shared_ptr<C2Info> ret = it->second;
        (void) mInfos.erase(it);
        return ret;
    }

private:
    C2Buffer * const mThis;
    BufferDataBuddy mData;
    std::map<C2Param::CoreIndex, std::shared_ptr<C2Info>> mInfos;
    std::list<std::pair<OnDestroyNotify, void *>> mNotify;
};

C2Buffer::C2Buffer(const std::vector<C2ConstLinearBlock> &blocks)
    : mImpl(new Impl(this, blocks)) {}

C2Buffer::C2Buffer(const std::vector<C2ConstGraphicBlock> &blocks)
    : mImpl(new Impl(this, blocks)) {}

const C2BufferData C2Buffer::data() const { return mImpl->data(); }

c2_status_t C2Buffer::registerOnDestroyNotify(OnDestroyNotify onDestroyNotify, void *arg) {
    return mImpl->registerOnDestroyNotify(onDestroyNotify, arg);
}

c2_status_t C2Buffer::unregisterOnDestroyNotify(OnDestroyNotify onDestroyNotify, void *arg) {
    return mImpl->unregisterOnDestroyNotify(onDestroyNotify, arg);
}

const std::vector<std::shared_ptr<const C2Info>> C2Buffer::info() const {
    return mImpl->info();
}

c2_status_t C2Buffer::setInfo(const std::shared_ptr<C2Info> &info) {
    return mImpl->setInfo(info);
}

bool C2Buffer::hasInfo(C2Param::Type index) const {
    return mImpl->hasInfo(index);
}

std::shared_ptr<const C2Info> C2Buffer::getInfo(C2Param::Type index) const {
    return mImpl->getInfo(index);
}

std::shared_ptr<C2Info> C2Buffer::removeInfo(C2Param::Type index) {
    return mImpl->removeInfo(index);
}

// static
std::shared_ptr<C2Buffer> C2Buffer::CreateLinearBuffer(const C2ConstLinearBlock &block) {
    return std::shared_ptr<C2Buffer>(new C2Buffer({ block }));
}

// static
std::shared_ptr<C2Buffer> C2Buffer::CreateGraphicBuffer(const C2ConstGraphicBlock &block) {
    return std::shared_ptr<C2Buffer>(new C2Buffer({ block }));
}
