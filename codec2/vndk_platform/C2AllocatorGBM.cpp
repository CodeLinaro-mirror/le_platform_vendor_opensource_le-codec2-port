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

 Changes from Qualcomm Innovation Center are provided under the following license:

 Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted (subject to the limitations in the
 disclaimer below) provided that the following conditions are met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above
       copyright notice, this list of conditions and the following
       disclaimer in the documentation and/or other materials provided
       with the distribution.

     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
       contributors may be used to endorse or promote products derived
       from this software without specific prior written permission.

 NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// #define LOG_NDEBUG 0
#include <C2AllocatorGBM.h>
#include <C2ErrnoUtils.h>
#include <C2Debug.h>
#include <C2Buffer.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


namespace android {

/* ===================================== GBM ALLOCATION ==================================== */
bool native_handle_is_invalid(const native_handle_t *const handle) {
    // perform basic validation of a native handle
    if (handle == nullptr) {
        return true;
    }
    return ((size_t)handle->version != sizeof(native_handle_t) ||
            handle->numFds < 0 ||
            handle->numInts < 0 ||
            // for sanity assume handles must occupy less memory than INT_MAX bytes
            handle->numFds > int((INT_MAX - handle->version) / sizeof(int)) - handle->numInts);
}

const C2HandleGBM::ExtraData* C2HandleGBM::GetExtraData(const C2Handle *const handle) {
    if (handle == nullptr
            || native_handle_is_invalid(handle)
            || handle->numInts < NUM_INTS) {
        return nullptr;
    }
    return reinterpret_cast<const C2HandleGBM::ExtraData*>(
            &handle->data[handle->numFds + handle->numInts - NUM_INTS]);
}

C2HandleGBM::ExtraData *C2HandleGBM::GetExtraData(C2Handle *const handle) {
    return const_cast<C2HandleGBM::ExtraData *>(GetExtraData(const_cast<const C2Handle *const>(handle)));
}

bool C2HandleGBM::IsValid(const C2Handle *const o) {
    if (o == nullptr) {
        return false;
    }
    const ExtraData *xd = GetExtraData(o);
    // we cannot validate width/height/format/usage without accessing gbm driver
    return xd != nullptr && xd->magic == MAGIC;
}

int C2HandleGBM::bufferFd(const C2Handle *const handle) {
    if (!IsValid(handle)) {
        return -1;
    }

    return handle->data[0];
}

int C2HandleGBM::metaFd(const C2Handle *const handle) {
    if (!IsValid(handle)) {
        return -1;
    }

    return handle->data[1];
}

uint32_t C2HandleGBM::bufferSize(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return GetExtraData(o)->size;
}

uint32_t C2HandleGBM::width(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return GetExtraData(o)->width;
}

uint32_t C2HandleGBM::height(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return GetExtraData(o)->height;
}

uint32_t C2HandleGBM::format(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return GetExtraData(o)->format;
}

uint64_t C2HandleGBM::usage(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return (uint64_t(GetExtraData(o)->usage_hi) << 32) | GetExtraData(o)->usage_lo;
}

uint32_t C2HandleGBM::stride(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return GetExtraData(o)->stride;
}

uint32_t C2HandleGBM::sliceHeight(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return GetExtraData(o)->slice_height;
}

uint64_t C2HandleGBM::uId(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return (uint64_t(GetExtraData(o)->uIdHi) << 32) | GetExtraData(o)->uIdLo;
}

uint32_t C2HandleGBM::index(const C2Handle *const o) {
    if (!IsValid(o)) {
        return 0;
    }
    return GetExtraData(o)->index;
}

uint64_t C2HandleGBM::getUId(const native_handle_t *const handle) {
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

C2HandleGBM* C2HandleGBM::wrapAndMoveNativeHandle(
        uint32_t format, uint64_t flags, uint32_t index,
        const struct gbm_bo *const bufferObj,
        const native_handle_t *const cHandle) {
    uint32_t width, height, usageLo, usageHi, stride, sliceHeight, size, uIdLo, uIdHi;
    uint64_t uId;

    if (native_handle_is_invalid(cHandle) ||
        cHandle->numInts > int((INT_MAX - cHandle->version) / sizeof(int)) - NUM_INTS - cHandle->numFds) {
        ALOGE("%s: invalid handle\n", __func__);
        return nullptr;
    }

    width = bufferObj->width;
    height = bufferObj->height;
    format = format;
    usageLo = uint32_t(flags & 0xFFFFFFFF);
    usageHi = uint32_t(flags >> 32);
    stride = bufferObj->stride;
    sliceHeight = bufferObj->aligned_height;
    size = bufferObj->size;
    uId = getUId(cHandle);
    uIdLo = uint32_t(uId & 0xFFFFFFFF);
    uIdHi = uint32_t(uId >> 32);

    ExtraData xd = {
        width, height, format, usageLo, usageHi,
        stride, sliceHeight, size, uIdLo, uIdHi, index, MAGIC
    };
    native_handle_t *res = native_handle_create(cHandle->numFds, cHandle->numInts + NUM_INTS);
    if (res == nullptr) {
        ALOGE("%s: native handle create failed\n", __func__);
        return nullptr;
    }
    /* copy contents from raw handle to GBM handle */
    memcpy(&res->data, &cHandle->data, sizeof(int) * (cHandle->numFds + cHandle->numInts));
    *GetExtraData(res) = xd;

    /* free cloned handle */
    native_handle_delete(
            const_cast<native_handle_t *>(reinterpret_cast<const native_handle_t *>(cHandle)));

    return reinterpret_cast<C2HandleGBM *>(res);
}

const C2HandleGBM* C2HandleGBM::Import(
        const C2Handle *const handle,
        uint32_t *width, uint32_t *height) {
    const ExtraData *xd = GetExtraData(handle);
    if (xd == nullptr) {
        return nullptr;
    }
    *width = xd->width;
    *height = xd->height;
    return reinterpret_cast<const C2HandleGBM *>(handle);
}

C2AllocationGBM::C2AllocationGBM(
          uint32_t width, uint32_t height,
          const struct gbm_bo *bufferObj,
          const C2HandleGBM *const handle,
          C2Allocator::id_t allocatorId)
    : C2GraphicAllocation(width, height),
      mBufferObj(bufferObj),
      mHandle(handle),
      mAllocatorId(allocatorId),
      mAllocSize(C2HandleGBM::bufferSize(handle)) {
}

C2AllocationGBM::~C2AllocationGBM() {
    if (!mMappings.empty()) {
        ALOGD("Dangling mappings!\n");
        for (const Mapping& map : mMappings) {
            int err = munmap(map.addr, map.size);
            if (err) ALOGE("munmap failed\n");
        }
    }

    if (mBufferObj) {
        gbm_bo_destroy(const_cast<struct gbm_bo*>(mBufferObj));
        mBufferObj = nullptr;
    } else {
        native_handle_close(mHandle);
    }
    if (mHandle) {
        native_handle_delete(
                const_cast<native_handle_t *>(reinterpret_cast<const native_handle_t *>(mHandle)));
        mHandle = nullptr;
    }
}

c2_status_t C2AllocationGBM::map(
        C2Rect c2Rect, C2MemoryUsage usage, C2Fence *fence,
        C2PlanarLayout *layout /* nonnull */, uint8_t **addr /* nonnull */) {
    (void)c2Rect;
    (void)fence;
    void *base;
    if (!layout || !addr) {
        ALOGD("wrong param");
        return C2_BAD_VALUE;
    }
    c2_status_t err = C2_OK;
    *addr = nullptr;
    if (!mMappings.empty()) {
        ALOGE("%s: multiple map request\n", __func__);
    }

    int prot = PROT_NONE;
    int flags = MAP_SHARED;
    if (usage.expected & C2MemoryUsage::CPU_READ) {
        prot |= PROT_READ;
    }
    if (usage.expected & C2MemoryUsage::CPU_WRITE) {
        prot |= PROT_WRITE;
    }

    size_t mapSize = C2HandleGBM::bufferSize(mHandle);
    int fd = C2HandleGBM::bufferFd(mHandle);
    size_t mapOffset = 0;

    base = mmap(nullptr, mapSize, prot, flags, fd, mapOffset);
    if (base == MAP_FAILED) {
        ALOGE("nmap failed. fd %d, size %zu, ret %d\n", fd, mapSize, errno);
        *addr = nullptr;
        err = c2_map_errno<EINVAL>(errno);
    } else {
        ALOGV("successfully mapped: addr %p, size %zu, prot = %d, flags = %d, mapFd = %d\n",
                base, mapSize, prot, flags, fd);
        mMappings.push_back({base, mapSize});
    }

    if (err == C2_OK) {
        auto format = C2HandleGBM::format(mHandle);
        auto bufferlayout = mBufferObj->buf_lyt;
        switch (format) {
            case static_cast<uint32_t>(GBM_FORMAT_YCbCr_420_P010_VENUS):
                addr[C2PlanarLayout::PLANE_Y] = (uint8_t *)base;
                addr[C2PlanarLayout::PLANE_U] = (uint8_t *)((uint32_t*)base + bufferlayout.planes[1].offset);
                layout->type = C2PlanarLayout::TYPE_YUV;
                layout->numPlanes = bufferlayout.num_planes;
                layout->rootPlanes = bufferlayout.num_planes;
                layout->planes[C2PlanarLayout::PLANE_Y] = {
                        C2PlaneInfo::CHANNEL_Y,
                        bufferlayout.planes[0].h_increment,
                        bufferlayout.planes[0].v_increment,
                        2,2,
                        16,10,6,
                        C2PlaneInfo::NATIVE,
                        C2PlanarLayout::PLANE_Y,
                        0};
                layout->planes[C2PlanarLayout::PLANE_U] = {
                        C2PlaneInfo::CHANNEL_CB,
                        bufferlayout.planes[1].h_increment,
                        bufferlayout.planes[1].v_increment,
                        2,2,
                        16,10,6,
                        C2PlaneInfo::NATIVE,
                        C2PlanarLayout::PLANE_U,
                        0};
                layout->planes[C2PlanarLayout::PLANE_V] = {
                        C2PlaneInfo::CHANNEL_CR,
                        bufferlayout.planes[2].h_increment,
                        bufferlayout.planes[2].v_increment,
                        2,2,
                        16,10,6,
                        C2PlaneInfo::NATIVE,
                        C2PlanarLayout::PLANE_U,
                        2};
                break;
            case static_cast<uint32_t>(GBM_FORMAT_NV12):
                addr[C2PlanarLayout::PLANE_Y] = (uint8_t *)base;
                addr[C2PlanarLayout::PLANE_U] = (uint8_t *)((uint32_t*)base + bufferlayout.planes[1].offset);
                layout->type = C2PlanarLayout::TYPE_YUV;
                layout->numPlanes = bufferlayout.num_planes;
                layout->rootPlanes = bufferlayout.num_planes;
                layout->planes[C2PlanarLayout::PLANE_Y] = {
                        C2PlaneInfo::CHANNEL_Y,
                        bufferlayout.planes[0].h_increment,
                        bufferlayout.planes[0].v_increment,
                        1,1,
                        8,8,0,
                        C2PlaneInfo::NATIVE,
                        C2PlanarLayout::PLANE_Y,
                        0};
                layout->planes[C2PlanarLayout::PLANE_U] = {
                        C2PlaneInfo::CHANNEL_CB,
                        bufferlayout.planes[1].h_increment,
                        bufferlayout.planes[1].v_increment,
                        1,1,
                        8,8,0,
                        C2PlaneInfo::NATIVE,
                        C2PlanarLayout::PLANE_U,
                        0};
                layout->planes[C2PlanarLayout::PLANE_V] = {
                        C2PlaneInfo::CHANNEL_CR,
                        bufferlayout.planes[2].h_increment,
                        bufferlayout.planes[2].v_increment,
                        1,1,
                        8,8,0,
                        C2PlaneInfo::NATIVE,
                        C2PlanarLayout::PLANE_U,
                        1};
                break;
            default:
                err = c2_map_errno<EINVAL>(errno);
                break;
        }
    }

    return err;
}

c2_status_t C2AllocationGBM::unmap(
        uint8_t **addr, C2Rect rect, C2Fence *fence /* nullable */) {
    (void)rect;
    (void)fence;
    if (mMappings.empty()) {
        ALOGD("tried to unmap unmapped buffer");
        return C2_NOT_FOUND;
    }
    for (auto it = mMappings.begin(); it != mMappings.end(); ++it) {
        if (*addr != (uint8_t*)it->addr) {
            continue;
        }
        int err = munmap(it->addr, it->size);
        if (err != 0) {
            ALOGE("munmap failed. addr %p, size %zu, ret %d\n", it->addr, it->size, errno);
            return c2_map_errno<EINVAL>(errno);
        }
        if (fence) {
            *fence = C2Fence();  // not using fences
        }
        (void)mMappings.erase(it);
        ALOGV("successfully unmapped: addr %p, size %zu", it->addr, it->size);
        return C2_OK;
    }
    ALOGD("unmap failed to find specified map");
    return C2_BAD_VALUE;
}

bool C2AllocationGBM::equals(const std::shared_ptr<const C2GraphicAllocation> &other) const {
    return other && other->handle() == handle();
}

/* ===================================== GBM ALLOCATOR ==================================== */
class C2AllocatorGBM::Impl {
public:
    explicit Impl(id_t id)
        : mInit(C2_NO_INIT),
          mAllocationIndex(0) {
        C2MemoryUsage minUsage = { 0, 0 };
        C2MemoryUsage maxUsage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
        Traits traits = { "linux.allocator.gbm", id, C2Allocator::GRAPHIC, minUsage, maxUsage };
        mTraits = std::make_shared<C2Allocator::Traits>(traits);

        mDriverFd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        if (mDriverFd < 0) {
            ALOGE("%s: opening dri device node failed\n", __func__);
            return;
        }
        mGBM = gbm_create_device(mDriverFd);
        if (mGBM == nullptr) {
            ALOGE("%s: gbm create device failed\n", __func__);
            close(mDriverFd);
            mDriverFd = -1;
            return;
        }
        mInit = C2_OK;
    }

    virtual ~Impl() {
        if (mGBM != nullptr) {
            gbm_device_destroy(mGBM);
            mGBM = nullptr;
        }

        if (mDriverFd != -1) {
            close(mDriverFd);
            mDriverFd = -1;
        }
        mInit = C2_NO_INIT;
    }

    id_t getId() const {
        std::lock_guard<std::mutex> lock(mLock);
        return mTraits->id;
    }

    C2String getName() const {
        std::lock_guard<std::mutex> lock(mLock);
        return mTraits->name;
    }

    std::shared_ptr<const C2Allocator::Traits> getTraits() const {
        std::lock_guard<std::mutex> lock(mLock);
        return mTraits;
    }

    c2_status_t status() const {
        std::lock_guard<std::mutex> lock(mLock);
        return mInit;
    }

    void resetAllocIndex() {
        std::lock_guard<std::mutex> lock(mLock);
        ALOGV("%s: %u -> 0\n", __func__, mAllocationIndex);
        mAllocationIndex = 0;
    }

    c2_status_t allocateRawHandle_l(
        uint32_t width, uint32_t height,
        uint32_t format, uint64_t flags,
        struct gbm_bo **bufferObj,
        native_handle_t **handle) {
        c2_status_t ret;
        struct gbm_bo *bo;
        int metaFd = -1;

        *bufferObj = nullptr;
        *handle = nullptr;

        bo = gbm_bo_create(mGBM, width, height, format, flags);
        if (bo == nullptr) {
            ALOGE("%s: failed. width %u, height %u, format %#x, flags %#zx\n",
                __func__, width, height, format, flags);
            return C2_CORRUPTED;
        }

        gbm_perform(GBM_PERFORM_GET_METADATA_ION_FD, bo, &metaFd);

        if (bo->ion_fd < 0 || metaFd < 0) {
            ALOGE("%s: invalid fd. raw %d, meta %d\n", __func__, bo->ion_fd, metaFd);
            gbm_bo_destroy(bo);
            return C2_CORRUPTED;
        }

        native_handle_t *res = native_handle_create(2 /* raw + meta fd */, 0);
        if (res == nullptr) {
            ALOGE("%s: native handle alloc failed\n", __func__);
            gbm_bo_destroy(bo);
            return C2_CORRUPTED;
        }
        res->data[0] = bo->ion_fd;
        res->data[1] = metaFd;
        *bufferObj = bo;
        *handle = res;

        return C2_OK;
    }

    c2_status_t newGraphicAllocation(
            uint32_t width, uint32_t height, uint32_t format, const C2MemoryUsage &usage,
            std::shared_ptr<C2GraphicAllocation> *allocation) {
        std::lock_guard<std::mutex> lock(mLock);
        struct gbm_bo *bufferObj;
        native_handle_t *handle;
        uint64_t flags = usage.expected;

        if (mInit != C2_OK) {
            ALOGE("%s: gbm device not created\n", __func__);
            return C2_NO_INIT;
        }

        c2_status_t ret = allocateRawHandle_l(width, height, format, flags, &bufferObj, &handle);
        if (ret != C2_OK) {
            ALOGE("%s: allocate raw handle failed\n", __func__);
            return C2_CORRUPTED;
        }

        C2HandleGBM *gbmHandle = C2HandleGBM::wrapAndMoveNativeHandle(
                format, flags, mAllocationIndex++, bufferObj, handle);
        if (gbmHandle == nullptr) {
            ALOGE("%s: wrap and move native handle failed\n", __func__);
            return C2_CORRUPTED;
        }

        ALOGV("%s: fd %d, metaFd %d, width %u, height %u, format %#x, usage %#zx, "
            "stride %u, sliceHeight %u, size %u, uId %zu, index %u\n", __func__,
            gbmHandle->data[0], gbmHandle->data[1], gbmHandle->data[2], gbmHandle->data[3],
            gbmHandle->data[4], (gbmHandle->data[5] | ((uint64_t)gbmHandle->data[6] << 32)),
            gbmHandle->data[7], gbmHandle->data[8], gbmHandle->data[9], (gbmHandle->data[10] |
            ((uint64_t)gbmHandle->data[11] << 32)), gbmHandle->data[12]);

        allocation->reset(new C2AllocationGBM(
                width, height, bufferObj, gbmHandle, mTraits->id));
        return C2_OK;
    }

    c2_status_t priorGraphicAllocation(
            const C2Handle *cHandle,
            std::shared_ptr<C2GraphicAllocation> *allocation) {
        std::lock_guard<std::mutex> lock(mLock);
        uint32_t width;
        uint32_t height;

        if (mInit != C2_OK) {
            ALOGE("%s: gbm device not created\n", __func__);
            return C2_NO_INIT;
        }

        const C2HandleGBM *gbmHandle = C2HandleGBM::Import(
                cHandle, &width, &height);
        if (gbmHandle == nullptr) {
            ALOGE("%s: import gbmHandle failed\n", __func__);
            return C2_CORRUPTED;
        }

        ALOGV("%s: fd %d, metaFd %d, width %u, height %u, format %#x, usage %#zx, "
            "stride %u, sliceHeight %u, size %u, uId %zu, index %u\n", __func__,
            gbmHandle->data[0], gbmHandle->data[1], gbmHandle->data[2], gbmHandle->data[3],
            gbmHandle->data[4], (gbmHandle->data[5] | ((uint64_t)gbmHandle->data[6] << 32)),
            gbmHandle->data[7], gbmHandle->data[8], gbmHandle->data[9], (gbmHandle->data[10] |
            ((uint64_t)gbmHandle->data[11] << 32)), gbmHandle->data[12]);

        allocation->reset(new C2AllocationGBM(
                width, height, nullptr, gbmHandle, mTraits->id));
        return C2_OK;
    }

private:
    std::shared_ptr<C2Allocator::Traits> mTraits;
    c2_status_t mInit;
    uint32_t mAllocationIndex;
    mutable std::mutex mLock;
    struct gbm_device *mGBM;
    int mDriverFd;
};

C2AllocatorGBM::C2AllocatorGBM(id_t id)
    : mImpl(new Impl(id)) {}

C2AllocatorGBM::~C2AllocatorGBM() {
}

C2Allocator::id_t C2AllocatorGBM::getId() const {
    return mImpl->getId();
}

C2String C2AllocatorGBM::getName() const {
    return mImpl->getName();
}

std::shared_ptr<const C2Allocator::Traits> C2AllocatorGBM::getTraits() const {
    return mImpl->getTraits();
}

void C2AllocatorGBM::resetAllocIndex() {
    return mImpl->resetAllocIndex();
}

c2_status_t C2AllocatorGBM::newGraphicAllocation(
        uint32_t width, uint32_t height, uint32_t format, C2MemoryUsage usage,
        std::shared_ptr<C2GraphicAllocation> *allocation) {
    return mImpl->newGraphicAllocation(width, height, format, usage, allocation);
}

c2_status_t C2AllocatorGBM::priorGraphicAllocation(
        const C2Handle *handle,
        std::shared_ptr<C2GraphicAllocation> *allocation) {
    return mImpl->priorGraphicAllocation(handle, allocation);
}

c2_status_t C2AllocatorGBM::status() const {
    return mImpl->status();
}

// static
bool C2AllocatorGBM::CheckHandle(const C2Handle* const o) {
    return C2HandleGBM::IsValid(o);
}

} // namespace android

void _UnwrapNativeCodec2GBMMetadata(
        const C2Handle *const handle,
        uint32_t *width, uint32_t *height, uint32_t *format,uint64_t *usage, uint32_t *stride, uint32_t *size) {
    android::C2HandleGBM::Import(handle, width, height);
    *format = android::C2HandleGBM::format(handle);
    *usage = android::C2HandleGBM::usage(handle);
    *stride = android::C2HandleGBM::stride(handle);
    *size = android::C2HandleGBM::bufferSize(handle);
}
