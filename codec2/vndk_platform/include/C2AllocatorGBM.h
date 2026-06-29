/* Copyright (c) 2021 The Linux Foundation. All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are
 met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following
    disclaimer in the documentation and/or other materials provided
    with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
    contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _CODEC2_ALLOCATOR_GBM_H_
#define _CODEC2_ALLOCATOR_GBM_H_

#include <C2Buffer.h>
#include "gbm.h"
#include "gbm_priv.h"

#include <list>

namespace android {

class C2HandleGBM : public C2Handle {
public:
    static bool IsValid(const C2Handle *const o);

    static int bufferFd(const C2Handle *const handle);

    static int metaFd(const C2Handle *const handle);

    static uint32_t bufferSize(const C2Handle *const o);

    static uint32_t width(const C2Handle *const o);

    static uint32_t height(const C2Handle *const o);

    static uint64_t usage(const C2Handle *const o);

    static uint32_t format(const C2Handle *const o);

    static uint32_t stride(const C2Handle *const o);

    static uint32_t sliceHeight(const C2Handle *const o);

    static uint32_t sliceWidth(const C2Handle *const o);

    static uint64_t uId(const C2Handle *const o);

    static uint32_t index(const C2Handle *const o);

    static uint64_t getUId(const native_handle_t *const handle);

    static C2HandleGBM* wrapAndMoveNativeHandle(
            uint32_t format, uint64_t flags, uint32_t index,
            const struct gbm_bo *const bufferObj,
            const native_handle_t *const cHandle);

    static const C2HandleGBM* Import(
            const C2Handle *const handle,
            uint32_t *width, uint32_t *height);

    struct GbmBuf {
        int buffer_fd; // shared ion buffer
        int meta_buffer_fd;
    };

    struct ExtraData {
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t usageLo;
        uint32_t usageHi;
        uint32_t stride;
        uint32_t sliceHeight;
        uint32_t sliceWidth;
        uint32_t size;
        uint32_t uIdLo;
        uint32_t uIdHi;
        uint32_t index;
        uint32_t magic;
        uint32_t id;
    };
    GbmBuf mFds;
    ExtraData mInts;

    enum {
        NUM_FDS = sizeof(mFds) / sizeof(int),
        NUM_INTS = sizeof(ExtraData) / sizeof(int),
        VERSION = sizeof(C2Handle)
    };

    static constexpr uint32_t MAGIC = '\xc2gb\x00';

private:
    static const ExtraData* GetExtraData(const C2Handle *const handle);

    static ExtraData *GetExtraData(C2Handle *const handle);
};

static_assert(offsetof(C2HandleGBM, data) == offsetof(C2HandleGBM, mFds));

class C2AllocatorGBM : public C2Allocator {
public:
    virtual id_t getId() const override;

    virtual C2String getName() const override;

    virtual std::shared_ptr<const Traits> getTraits() const override;

    virtual void resetAllocIndex() override;

    virtual c2_status_t newGraphicAllocation(
            uint32_t width, uint32_t height, uint32_t format, C2MemoryUsage usage,
            std::shared_ptr<C2GraphicAllocation> *allocation) override;

    virtual c2_status_t priorGraphicAllocation(
            const C2Handle *handle,
            std::shared_ptr<C2GraphicAllocation> *allocation) override;

    C2AllocatorGBM(id_t id);
    virtual ~C2AllocatorGBM() override;

    c2_status_t status() const;

    virtual bool checkHandle(const C2Handle* const o) const override { return CheckHandle(o); }

    static bool CheckHandle(const C2Handle* const o);

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

class C2AllocationGBM : public C2GraphicAllocation {
public:
    virtual ~C2AllocationGBM() override;

    virtual c2_status_t map(
            C2Rect c2Rect, C2MemoryUsage usage, C2Fence *fence,
            C2PlanarLayout *layout /* nonnull */, uint8_t **addr /* nonnull */) override;
    virtual c2_status_t unmap(
            uint8_t **addr /* nonnull */, C2Rect rect, C2Fence *fence /* nullable */) override;
    virtual C2Allocator::id_t getAllocatorId() const override { return mAllocatorId; }
    virtual const C2Handle *handle() const override { return reinterpret_cast<const C2Handle*>(mHandle); }
    virtual bool equals(const std::shared_ptr<const C2GraphicAllocation> &other) const override;
    virtual uint32_t allocSize() const { return mAllocSize; };

    C2AllocationGBM(
            uint32_t width, uint32_t height,
            const struct gbm_bo *bufferObj,
            const C2HandleGBM *const handle,
            C2Allocator::id_t allocatorId);
    c2_status_t status() const;

private:
    const C2HandleGBM *mHandle;
    const struct gbm_bo *mBufferObj;
    C2Allocator::id_t mAllocatorId;
    uint32_t mAllocSize;
    struct Mapping {
        void* addr;
        size_t size;
    };
    std::list<Mapping> mMappings;
};

} // namespace android

#endif // _CODEC2_ALLOCATOR_GBM_H_
