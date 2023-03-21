/*
 * Copyright (C) 2017 The Android Open Source Project
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

#define LOG_TAG "C2Store"
#define LOG_NDEBUG 0
#include <C2Debug.h>

#include <C2DmaBufAllocator.h>
#include <C2AllocatorGBM.h>
#include <C2BufferPriv.h>
#include <C2Component.h>
#include <C2Config.h>
#include <C2PlatformSupport.h>
#include <cutils/properties.h>
#include <util/C2InterfaceHelper.h>
#include <cutils/properties.h>

#include <sys/stat.h>
#include <dlfcn.h>
#include <unistd.h> // getpagesize

#include <map>
#include <memory>
#include <mutex>

uint32_t gCodec2LogLevel;
uint32_t gCodec2TraceLevel;

namespace android {

/**
 * Returns the preferred component store in this process to access its interface.
 */
std::shared_ptr<C2ComponentStore> GetPreferredCodec2ComponentStore();

/**
 * The platform allocator store provides basic allocator-types for the framework based on ion and
 * gralloc. Allocators are not meant to be updatable.
 *
 * \todo Provide allocator based on ashmem
 * \todo Move ion allocation into its HIDL or provide some mapping from memory usage to ion flags
 * \todo Make this allocator store extendable
 */
class C2PlatformAllocatorStoreImpl : public C2PlatformAllocatorStore {
public:
    C2PlatformAllocatorStoreImpl();

    virtual c2_status_t fetchAllocator(
            id_t id, std::shared_ptr<C2Allocator> *const allocator) override;

    virtual std::vector<std::shared_ptr<const C2Allocator::Traits>> listAllocators_nb()
            const override {
        return std::vector<std::shared_ptr<const C2Allocator::Traits>>(); /// \todo
    }

    virtual C2String getName() const override {
        return "android.allocator-store";
    }

    void setComponentStore(std::shared_ptr<C2ComponentStore> store);

    ~C2PlatformAllocatorStoreImpl() override = default;

private:
    /// returns a shared-singleton dma allocator
    std::shared_ptr<C2Allocator> fetchDmaBufAllocator();

    /// returns a shared-singleton gbm allocator
    std::shared_ptr<C2Allocator> fetchGbmBufAllocator();

    /// component store to use
    std::mutex _mComponentStoreSetLock; // protects the entire updating _mComponentStore and its
                                        // dependencies
    std::mutex _mComponentStoreReadLock; // must protect only read/write of _mComponentStore
    std::shared_ptr<C2ComponentStore> _mComponentStore;
};

C2PlatformAllocatorStoreImpl::C2PlatformAllocatorStoreImpl() {
}

c2_status_t C2PlatformAllocatorStoreImpl::fetchAllocator(
        id_t id, std::shared_ptr<C2Allocator> *const allocator) {
    allocator->reset();
    switch (id) {
    // TODO: should we implement a generic registry for all, and use that?
    case C2AllocatorStore::DEFAULT_LINEAR:
    case C2PlatformAllocatorStore::DMABUFHEAP:
        *allocator = fetchDmaBufAllocator();
        break;
    case C2AllocatorStore::DEFAULT_GRAPHIC:
    case C2PlatformAllocatorStore::GBMBUFHEAP:
        *allocator = fetchGbmBufAllocator();
        break;
    default:
        break;
    }
    if (*allocator == nullptr) {
        return C2_NO_MEMORY;
    }
    return C2_OK;
}

namespace {

std::mutex gDmaBufAllocatorMutex;
std::weak_ptr<C2DmaBufAllocator> gDmaBufAllocator;

void UseComponentStoreForDmaBufAllocator(const std::shared_ptr<C2DmaBufAllocator> allocator,
                                         std::shared_ptr<C2ComponentStore> store) {
    C2DmaBufAllocator::UsageMapperFn mapper;
    const size_t maxHeapNameLen = 128;
    uint64_t minUsage = 0;
    uint64_t maxUsage = C2MemoryUsage(C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE).expected;
    size_t blockSize = getpagesize();

    // query min and max usage as well as block size via supported values
    std::unique_ptr<C2StoreDmaBufUsageInfo> usageInfo;
    usageInfo = C2StoreDmaBufUsageInfo::AllocUnique(maxHeapNameLen);

    std::vector<C2FieldSupportedValuesQuery> query = {
            C2FieldSupportedValuesQuery::Possible(C2ParamField::Make(*usageInfo, usageInfo->m.usage)),
            C2FieldSupportedValuesQuery::Possible(
                    C2ParamField::Make(*usageInfo, usageInfo->m.capacity)),
    };
    c2_status_t res = store->querySupportedValues_sm(query);
    if (res == C2_OK) {
        if (query[0].status == C2_OK) {
            const C2FieldSupportedValues& fsv = query[0].values;
            if (fsv.type == C2FieldSupportedValues::FLAGS && !fsv.values.empty()) {
                minUsage = fsv.values[0].u64;
                maxUsage = 0;
                for (C2Value::Primitive v : fsv.values) {
                    maxUsage |= v.u64;
                }
            }
        }
        if (query[1].status == C2_OK) {
            const C2FieldSupportedValues& fsv = query[1].values;
            if (fsv.type == C2FieldSupportedValues::RANGE && fsv.range.step.u32 > 0) {
                blockSize = fsv.range.step.u32;
            }
        }

        mapper = [store](C2MemoryUsage usage, size_t capacity, C2String* heapName,
                         unsigned* flags) -> c2_status_t {
            if (capacity > UINT32_MAX) {
                return C2_BAD_VALUE;
            }

            std::unique_ptr<C2StoreDmaBufUsageInfo> usageInfo;
            usageInfo = C2StoreDmaBufUsageInfo::AllocUnique(maxHeapNameLen, usage.expected, capacity);
            std::vector<std::unique_ptr<C2SettingResult>> failures;  // TODO: remove

            c2_status_t res = store->config_sm({&*usageInfo}, &failures);
            if (res == C2_OK) {
                *heapName = C2String(usageInfo->m.heapName);
                *flags = usageInfo->m.allocFlags;
            }

            return res;
        };
    }

    allocator->setUsageMapper(mapper, minUsage, maxUsage, blockSize);
}

}

void C2PlatformAllocatorStoreImpl::setComponentStore(std::shared_ptr<C2ComponentStore> store) {
    // technically this set lock is not needed, but is here for safety in case we add more
    // getter orders
    std::lock_guard<std::mutex> lock(_mComponentStoreSetLock);
    {
        std::lock_guard<std::mutex> lock(_mComponentStoreReadLock);
        _mComponentStore = store;
    }
}

std::shared_ptr<C2Allocator> C2PlatformAllocatorStoreImpl::fetchDmaBufAllocator() {
    std::lock_guard<std::mutex> lock(gDmaBufAllocatorMutex);
    std::shared_ptr<C2DmaBufAllocator> allocator = gDmaBufAllocator.lock();
    if (allocator == nullptr) {
        std::shared_ptr<C2ComponentStore> componentStore;
        {
            std::lock_guard<std::mutex> lock(_mComponentStoreReadLock);
            componentStore = _mComponentStore;
        }
        allocator = std::make_shared<C2DmaBufAllocator>(C2PlatformAllocatorStore::DMABUFHEAP);
        UseComponentStoreForDmaBufAllocator(allocator, componentStore);
        gDmaBufAllocator = allocator;
    }
    return allocator;
}

std::shared_ptr<C2Allocator> C2PlatformAllocatorStoreImpl::fetchGbmBufAllocator() {
    std::shared_ptr<C2AllocatorGBM> allocator =
        std::make_shared<C2AllocatorGBM>((uint32_t)C2PlatformAllocatorStore::GBMBUFHEAP);
    c2_status_t ret = allocator->status();
    if (ret != C2_OK) {
        ALOGE("%s: failed\n", __func__);
        return nullptr;
    }

    return allocator;
}

namespace {
    std::mutex gPreferredComponentStoreMutex;
    std::shared_ptr<C2ComponentStore> gPreferredComponentStore;

    std::mutex gPlatformAllocatorStoreMutex;
    std::weak_ptr<C2PlatformAllocatorStoreImpl> gPlatformAllocatorStore;
}

std::shared_ptr<C2AllocatorStore> GetCodec2PlatformAllocatorStore() {
    std::lock_guard<std::mutex> lock(gPlatformAllocatorStoreMutex);

    {
        char codec2LoggingLevel[PROPERTY_VALUE_MAX];
        property_get("vendor.codec2.log.level", codec2LoggingLevel, "");
        gCodec2LogLevel = atoi(codec2LoggingLevel);
    }

    {
        char codec2TraceLevel[PROPERTY_VALUE_MAX];
        property_get("vendor.codec2.trace.level",codec2TraceLevel, "");
        gCodec2TraceLevel = static_cast<uint32_t>(strtoul(codec2TraceLevel, nullptr, 16));
        ALOGD("codec2 trace level %d",gCodec2TraceLevel);
    }

    std::shared_ptr<C2PlatformAllocatorStoreImpl> store = gPlatformAllocatorStore.lock();
    if (store == nullptr) {
        store = std::make_shared<C2PlatformAllocatorStoreImpl>();
        store->setComponentStore(GetPreferredCodec2ComponentStore());
        gPlatformAllocatorStore = store;
    }
    return store;
}

void SetPreferredCodec2ComponentStore(std::shared_ptr<C2ComponentStore> componentStore) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex); // don't interleve set-s

    // update preferred store
    {
        std::lock_guard<std::mutex> lock(gPreferredComponentStoreMutex);
        gPreferredComponentStore = componentStore;
    }

    // update platform allocator's store as well if it is alive
    std::shared_ptr<C2PlatformAllocatorStoreImpl> allocatorStore;
    {
        std::lock_guard<std::mutex> lock(gPlatformAllocatorStoreMutex);
        allocatorStore = gPlatformAllocatorStore.lock();
    }
    if (allocatorStore) {
        allocatorStore->setComponentStore(componentStore);
    }
}

std::shared_ptr<C2ComponentStore> GetPreferredCodec2ComponentStore() {
    std::lock_guard<std::mutex> lock(gPreferredComponentStoreMutex);
    return gPreferredComponentStore ? gPreferredComponentStore : GetCodec2PlatformComponentStore();
}

namespace {

class _C2BlockPoolCache {
public:
    _C2BlockPoolCache() : mBlockPoolSeqId(C2BlockPool::PLATFORM_START + 1) {}

private:
    c2_status_t _createBlockPool(
            C2PlatformAllocatorStore::id_t allocatorId,
            std::vector<std::shared_ptr<const C2Component>> components,
            C2BlockPool::local_id_t poolId,
            std::shared_ptr<C2BlockPool> *pool) {
        std::shared_ptr<C2AllocatorStore> allocatorStore =
                GetCodec2PlatformAllocatorStore();
        std::shared_ptr<C2Allocator> allocator;
        c2_status_t res = C2_NOT_FOUND;

        auto deleter = [this, poolId](C2BlockPool *pool) {
            std::unique_lock lock(mMutex);
            mBlockPools.erase(poolId);
            mComponents.erase(poolId);
            delete pool;
        };
        switch(allocatorId) {
            case C2AllocatorStore::DEFAULT_LINEAR:
                res = allocatorStore->fetchAllocator(
                        C2PlatformAllocatorStore::DMABUFHEAP, &allocator);
                if (res == C2_OK) {
                    std::shared_ptr<C2BlockPool> ptr(
                            new C2BasicBlockPool(allocator, allocatorId, poolId),
                            deleter);
                    *pool = ptr;
                    mBlockPools[poolId] = ptr;
                    mComponents[poolId].insert(
                           mComponents[poolId].end(),
                           components.begin(), components.end());
                }
                break;
            case C2AllocatorStore::DEFAULT_GRAPHIC:
                res = allocatorStore->fetchAllocator(
                        C2PlatformAllocatorStore::GBMBUFHEAP, &allocator);
                if (res == C2_OK) {
                    std::shared_ptr<C2BlockPool> ptr(
                            new C2BasicBlockPool(allocator, allocatorId, poolId),
                            deleter);
                    *pool = ptr;
                    mBlockPools[poolId] = ptr;
                    mComponents[poolId].insert(
                           mComponents[poolId].end(),
                           components.begin(), components.end());
                }
                break;
            case C2AllocatorStore::LINEAR_CONTIGUOUS:
                res = allocatorStore->fetchAllocator(
                        C2PlatformAllocatorStore::DMABUFHEAP, &allocator);
                if (res == C2_OK) {
                    std::shared_ptr<C2BlockPool> ptr(
                            new C2CustomBlockPool(allocator, allocatorId, poolId, true),
                            deleter);
                    *pool = ptr;
                    mBlockPools[poolId] = ptr;
                    mComponents[poolId].insert(
                           mComponents[poolId].end(),
                           components.begin(), components.end());
                }
                break;
            case C2AllocatorStore::LINEAR_NON_CONTIGUOUS:
                res = allocatorStore->fetchAllocator(
                        C2PlatformAllocatorStore::DMABUFHEAP, &allocator);
                if (res == C2_OK) {
                    std::shared_ptr<C2BlockPool> ptr(
                            new C2CustomBlockPool(allocator, allocatorId, poolId, false),
                            deleter);
                    *pool = ptr;
                    mBlockPools[poolId] = ptr;
                    mComponents[poolId].insert(
                           mComponents[poolId].end(),
                           components.begin(), components.end());
                }
                break;
            case C2AllocatorStore::GRAPHIC_CONTIGUOUS:
                res = allocatorStore->fetchAllocator(
                        C2PlatformAllocatorStore::GBMBUFHEAP, &allocator);
                if (res == C2_OK) {
                    std::shared_ptr<C2BlockPool> ptr(
                            new C2CustomBlockPool(allocator, allocatorId, poolId, true),
                            deleter);
                    *pool = ptr;
                    mBlockPools[poolId] = ptr;
                    mComponents[poolId].insert(
                           mComponents[poolId].end(),
                           components.begin(), components.end());
                }
                break;
            case C2AllocatorStore::GRAPHIC_NON_CONTIGUOUS:
                res = allocatorStore->fetchAllocator(
                        C2PlatformAllocatorStore::GBMBUFHEAP, &allocator);
                if (res == C2_OK) {
                    std::shared_ptr<C2BlockPool> ptr(
                            new C2CustomBlockPool(allocator, allocatorId, poolId, false),
                            deleter);
                    *pool = ptr;
                    mBlockPools[poolId] = ptr;
                    mComponents[poolId].insert(
                           mComponents[poolId].end(),
                           components.begin(), components.end());
                }
                break;
            default:
                break;
        }
        return res;
    }

public:
    c2_status_t createBlockPool(
            C2PlatformAllocatorStore::id_t allocatorId,
            std::vector<std::shared_ptr<const C2Component>> components,
            std::shared_ptr<C2BlockPool> *pool) {
        std::unique_lock lock(mMutex);
        return _createBlockPool(allocatorId, components, mBlockPoolSeqId++, pool);
    }

    c2_status_t getBlockPool(
            C2BlockPool::local_id_t blockPoolId,
            std::shared_ptr<const C2Component> component,
            std::shared_ptr<C2BlockPool> *pool) {
        std::unique_lock lock(mMutex);
        // TODO: use one iterator for multiple blockpool type scalability.
        std::shared_ptr<C2BlockPool> ptr;
        auto it = mBlockPools.find(blockPoolId);
        if (it != mBlockPools.end()) {
            ptr = it->second.lock();
            if (!ptr) {
                mBlockPools.erase(it);
                mComponents.erase(blockPoolId);
            } else {
                auto found = std::find_if(
                        mComponents[blockPoolId].begin(),
                        mComponents[blockPoolId].end(),
                        [component](const std::weak_ptr<const C2Component> &ptr) {
                            return component == ptr.lock();
                        });
                if (found != mComponents[blockPoolId].end()) {
                    *pool = ptr;
                    return C2_OK;
                }
            }
        }
        return C2_NOT_FOUND;
    }

private:
    // Deleter needs to hold this mutex, and there is a small chance that deleter
    // is invoked while the mutex is held.
    std::recursive_mutex mMutex;
    C2BlockPool::local_id_t mBlockPoolSeqId;

    std::map<C2BlockPool::local_id_t, std::weak_ptr<C2BlockPool>> mBlockPools;
    std::map<C2BlockPool::local_id_t, std::vector<std::weak_ptr<const C2Component>>> mComponents;
};

static std::unique_ptr<_C2BlockPoolCache> sBlockPoolCache =
    std::make_unique<_C2BlockPoolCache>();

} // anynymous namespace

c2_status_t GetCodec2BlockPool(
        C2BlockPool::local_id_t id, std::shared_ptr<const C2Component> component,
        std::shared_ptr<C2BlockPool> *pool) {
    pool->reset();
    std::shared_ptr<C2AllocatorStore> allocatorStore = GetCodec2PlatformAllocatorStore();
    std::shared_ptr<C2Allocator> allocator;
    c2_status_t res = C2_NOT_FOUND;

    if (id >= C2BlockPool::PLATFORM_START) {
        return sBlockPoolCache->getBlockPool(id, component, pool);
    }

    switch (id) {
    case C2BlockPool::BASIC_LINEAR:
        res = allocatorStore->fetchAllocator(C2AllocatorStore::DEFAULT_LINEAR, &allocator);
        if (res == C2_OK) {
            *pool = std::make_shared<C2BasicLinearBlockPool>(allocator);
        }
        break;
    case C2BlockPool::BASIC_GRAPHIC:
        res = allocatorStore->fetchAllocator(C2AllocatorStore::DEFAULT_GRAPHIC, &allocator);
        if (res == C2_OK) {
            *pool = std::make_shared<C2BasicGraphicBlockPool>(allocator);
        }
        break;
    default:
        break;
    }
    return res;
}

c2_status_t CreateCodec2BlockPool(
        C2PlatformAllocatorStore::id_t allocatorId,
        const std::vector<std::shared_ptr<const C2Component>> &components,
        std::shared_ptr<C2BlockPool> *pool) {
    pool->reset();

    return sBlockPoolCache->createBlockPool(allocatorId, components, pool);
}

c2_status_t CreateCodec2BlockPool(
        C2PlatformAllocatorStore::id_t allocatorId,
        std::shared_ptr<const C2Component> component,
        std::shared_ptr<C2BlockPool> *pool) {
    pool->reset();

    return sBlockPoolCache->createBlockPool(allocatorId, {component}, pool);
}

class C2PlatformComponentStore : public C2ComponentStore {
public:
    virtual std::vector<std::shared_ptr<const C2Component::Traits>> listComponents() override;
    virtual std::shared_ptr<C2ParamReflector> getParamReflector() const override;
    virtual C2String getName() const override;
    virtual c2_status_t querySupportedValues_sm(
            std::vector<C2FieldSupportedValuesQuery> &fields) const override;
    virtual c2_status_t querySupportedParams_nb(
            std::vector<std::shared_ptr<C2ParamDescriptor>> *const params) const override;
    virtual c2_status_t query_sm(
            const std::vector<C2Param*> &stackParams,
            const std::vector<C2Param::Index> &heapParamIndices,
            std::vector<std::unique_ptr<C2Param>> *const heapParams) const override;
    virtual c2_status_t createInterface(
            C2String name, std::shared_ptr<C2ComponentInterface> *const interface) override;
    virtual c2_status_t createComponent(
            C2String name, std::shared_ptr<C2Component> *const component) override;
    virtual c2_status_t copyBuffer(
            std::shared_ptr<C2GraphicBuffer> src, std::shared_ptr<C2GraphicBuffer> dst) override;
    virtual c2_status_t config_sm(
            const std::vector<C2Param*> &params,
            std::vector<std::unique_ptr<C2SettingResult>> *const failures) override;
    C2PlatformComponentStore();

    // For testing only
    C2PlatformComponentStore(
            std::vector<std::tuple<C2String,
                                   C2ComponentFactory::CreateCodec2FactoryFunc,
                                   C2ComponentFactory::DestroyCodec2FactoryFunc>>);

    virtual ~C2PlatformComponentStore() override = default;

private:

    /**
     * An object encapsulating a loaded component module.
     *
     * \todo provide a way to add traits to known components here to avoid loading the .so-s
     * for listComponents
     */
    struct ComponentModule : public C2ComponentFactory,
            public std::enable_shared_from_this<ComponentModule> {
        virtual c2_status_t createComponent(
                c2_node_id_t id, std::shared_ptr<C2Component> *component,
                ComponentDeleter deleter = std::default_delete<C2Component>()) override;
        virtual c2_status_t createInterface(
                c2_node_id_t id, std::shared_ptr<C2ComponentInterface> *interface,
                InterfaceDeleter deleter = std::default_delete<C2ComponentInterface>()) override;

        /**
         * \returns the traits of the component in this module.
         */
        std::shared_ptr<const C2Component::Traits> getTraits();

        /**
         * Creates an uninitialized component module.
         *
         * \param name[in]  component name.
         *
         * \note Only used by ComponentLoader.
         */
        ComponentModule()
            : mInit(C2_NO_INIT),
              mLibHandle(nullptr),
              createFactory(nullptr),
              destroyFactory(nullptr),
              mComponentFactory(nullptr) {
        }

        /**
         * Creates an uninitialized component module.
         * NOTE: For testing only
         *
         * \param name[in]  component name.
         *
         * \note Only used by ComponentLoader.
         */
        ComponentModule(
                C2ComponentFactory::CreateCodec2FactoryFunc createFactory,
                C2ComponentFactory::DestroyCodec2FactoryFunc destroyFactory)
            : mInit(C2_NO_INIT),
              mLibHandle(nullptr),
              createFactory(createFactory),
              destroyFactory(destroyFactory),
              mComponentFactory(nullptr) {
        }

        /**
         * Initializes a component module with a given library path. Must be called exactly once.
         *
         * \note Only used by ComponentLoader.
         *
         * \param libPath[in] library path
         *
         * \retval C2_OK        the component module has been successfully loaded
         * \retval C2_NO_MEMORY not enough memory to loading the component module
         * \retval C2_NOT_FOUND could not locate the component module
         * \retval C2_CORRUPTED the component module could not be loaded (unexpected)
         * \retval C2_REFUSED   permission denied to load the component module (unexpected)
         * \retval C2_TIMED_OUT could not load the module within the time limit (unexpected)
         */
        c2_status_t init(std::string libPath);

        virtual ~ComponentModule() override;

    protected:
        std::recursive_mutex mLock; ///< lock protecting mTraits
        std::shared_ptr<C2Component::Traits> mTraits; ///< cached component traits

        c2_status_t mInit; ///< initialization result

        void *mLibHandle; ///< loaded library handle
        C2ComponentFactory::CreateCodec2FactoryFunc createFactory; ///< loaded create function
        C2ComponentFactory::DestroyCodec2FactoryFunc destroyFactory; ///< loaded destroy function
        C2ComponentFactory *mComponentFactory; ///< loaded/created component factory
    };

    /**
     * An object encapsulating a loadable component module.
     *
     * \todo make this also work for enumerations
     */
    struct ComponentLoader {
        /**
         * Load the component module.
         *
         * This method simply returns the component module if it is already currently loaded, or
         * attempts to load it if it is not.
         *
         * \param module[out] pointer to the shared pointer where the loaded module shall be stored.
         *                    This will be nullptr on error.
         *
         * \retval C2_OK        the component module has been successfully loaded
         * \retval C2_NO_MEMORY not enough memory to loading the component module
         * \retval C2_NOT_FOUND could not locate the component module
         * \retval C2_CORRUPTED the component module could not be loaded
         * \retval C2_REFUSED   permission denied to load the component module
         */
        c2_status_t fetchModule(std::shared_ptr<ComponentModule> *module) {
            c2_status_t res = C2_OK;
            std::lock_guard<std::mutex> lock(mMutex);
            std::shared_ptr<ComponentModule> localModule = mModule.lock();
            if (localModule == nullptr) {
                if(mCreateFactory) {
                    // For testing only
                    localModule = std::make_shared<ComponentModule>(mCreateFactory,
                                                                    mDestroyFactory);
                } else {
                    localModule = std::make_shared<ComponentModule>();
                }
                res = localModule->init(mLibPath);
                if (res == C2_OK) {
                    mModule = localModule;
                }
            }
            *module = localModule;
            return res;
        }

        /**
         * Creates a component loader for a specific library path (or name).
         */
        ComponentLoader(std::string libPath)
            : mLibPath(libPath) {}

        // For testing only
        ComponentLoader(std::tuple<C2String,
                          C2ComponentFactory::CreateCodec2FactoryFunc,
                          C2ComponentFactory::DestroyCodec2FactoryFunc> func)
            : mLibPath(std::get<0>(func)),
              mCreateFactory(std::get<1>(func)),
              mDestroyFactory(std::get<2>(func)) {}

    private:
        std::mutex mMutex; ///< mutex guarding the module
        std::weak_ptr<ComponentModule> mModule; ///< weak reference to the loaded module
        std::string mLibPath; ///< library path

        // For testing only
        C2ComponentFactory::CreateCodec2FactoryFunc mCreateFactory = nullptr;
        C2ComponentFactory::DestroyCodec2FactoryFunc mDestroyFactory = nullptr;
    };

    struct Interface : public C2InterfaceHelper {
        std::shared_ptr<C2StoreIonUsageInfo> mIonUsageInfo;
        std::shared_ptr<C2StoreDmaBufUsageInfo> mDmaBufUsageInfo;

        Interface(std::shared_ptr<C2ReflectorHelper> reflector)
            : C2InterfaceHelper(reflector) {
            setDerivedInstance(this);

            struct Setter {
                static C2R setDmaBufUsage(bool /* mayBlock */, C2P<C2StoreDmaBufUsageInfo> &me) {
                    long long usage = (long long)me.get().m.usage;
                    if (C2DmaBufAllocator::system_uncached_supported() &&
                        !(usage & (C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE))) {
                        strlcpy(me.set().m.heapName, "system-uncached", me.v.flexCount());
                    } else {
                        strlcpy(me.set().m.heapName, "system", me.v.flexCount());
                    }
                    me.set().m.allocFlags = 0;
                    return C2R::Ok();
                };
            };

            addParameter(
                DefineParam(mDmaBufUsageInfo, "dmabuf-usage")
                .withDefault(C2StoreDmaBufUsageInfo::AllocShared(0))
                .withFields({
                    C2F(mDmaBufUsageInfo, m.usage).flags({C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE}),
                    C2F(mDmaBufUsageInfo, m.capacity).inRange(0, UINT32_MAX, 1024),
                    C2F(mDmaBufUsageInfo, m.allocFlags).flags({}),
                    C2F(mDmaBufUsageInfo, m.heapName).any(),
                })
                .withSetter(Setter::setDmaBufUsage)
                .build());
        }
    };

    /**
     * Retrieves the component module for a component.
     *
     * \param module pointer to a shared_pointer where the component module will be stored on
     *               success.
     *
     * \retval C2_OK        the component loader has been successfully retrieved
     * \retval C2_NO_MEMORY not enough memory to locate the component loader
     * \retval C2_NOT_FOUND could not locate the component to be loaded
     * \retval C2_CORRUPTED the component loader could not be identified due to some modules being
     *                      corrupted (this can happen if the name does not refer to an already
     *                      identified component but some components could not be loaded due to
     *                      bad library)
     * \retval C2_REFUSED   permission denied to find the component loader for the named component
     *                      (this can happen if the name does not refer to an already identified
     *                      component but some components could not be loaded due to lack of
     *                      permissions)
     */
    c2_status_t findComponent(C2String name, std::shared_ptr<ComponentModule> *module);

    /**
     * Loads each component module and discover its contents.
     */
    void visitComponents();

    std::mutex mMutex; ///< mutex guarding the component lists during construction
    bool mVisited; ///< component modules visited
    std::map<C2String, ComponentLoader> mComponents; ///< path -> component module
    std::map<C2String, C2String> mComponentNameToPath; ///< name -> path
    std::vector<std::shared_ptr<const C2Component::Traits>> mComponentList;

    std::shared_ptr<C2ReflectorHelper> mReflector;
    Interface mInterface;

    // For testing only
    std::vector<std::tuple<C2String,
                          C2ComponentFactory::CreateCodec2FactoryFunc,
                          C2ComponentFactory::DestroyCodec2FactoryFunc>> mCodec2FactoryFuncs;
};

c2_status_t C2PlatformComponentStore::ComponentModule::init(
        std::string libPath) {
    ALOGV("in %s", __func__);
    ALOGV("loading dll");

    if(!createFactory) {
        mLibHandle = dlopen(libPath.c_str(), RTLD_NOW|RTLD_NODELETE);
        LOG_ALWAYS_FATAL_IF(mLibHandle == nullptr,
                "could not dlopen %s: %s", libPath.c_str(), dlerror());

        createFactory =
            (C2ComponentFactory::CreateCodec2FactoryFunc)dlsym(mLibHandle, "CreateCodec2Factory");
        LOG_ALWAYS_FATAL_IF(createFactory == nullptr,
                "createFactory is null in %s", libPath.c_str());

        destroyFactory =
            (C2ComponentFactory::DestroyCodec2FactoryFunc)dlsym(mLibHandle, "DestroyCodec2Factory");
        LOG_ALWAYS_FATAL_IF(destroyFactory == nullptr,
                "destroyFactory is null in %s", libPath.c_str());
    }

    mComponentFactory = createFactory();
    if (mComponentFactory == nullptr) {
        ALOGD("could not create factory in %s", libPath.c_str());
        mInit = C2_NO_MEMORY;
    } else {
        mInit = C2_OK;
    }

    if (mInit != C2_OK) {
        return mInit;
    }

    std::shared_ptr<C2ComponentInterface> intf;
    c2_status_t res = createInterface(0, &intf);
    if (res != C2_OK) {
        ALOGD("failed to create interface: %d", res);
        return mInit;
    }

    std::shared_ptr<C2Component::Traits> traits(new (std::nothrow) C2Component::Traits);
    if (traits) {
        if (!C2InterfaceUtils::FillTraitsFromInterface(traits.get(), intf)) {
            ALOGD("Failed to fill traits from interface");
            return mInit;
        }

        // TODO: get this properly from the store during emplace
        switch (traits->domain) {
        case C2Component::DOMAIN_AUDIO:
            traits->rank = 8;
            break;
        default:
            traits->rank = 512;
        }
    }
    mTraits = traits;

    return mInit;
}

C2PlatformComponentStore::ComponentModule::~ComponentModule() {
    ALOGV("in %s", __func__);
    if (destroyFactory && mComponentFactory) {
        destroyFactory(mComponentFactory);
    }
    if (mLibHandle) {
        ALOGV("unloading dll");
        dlclose(mLibHandle);
    }
}

c2_status_t C2PlatformComponentStore::ComponentModule::createInterface(
        c2_node_id_t id, std::shared_ptr<C2ComponentInterface> *interface,
        std::function<void(::C2ComponentInterface*)> deleter) {
    interface->reset();
    if (mInit != C2_OK) {
        return mInit;
    }
    std::shared_ptr<ComponentModule> module = shared_from_this();
    c2_status_t res = mComponentFactory->createInterface(
            id, interface, [module, deleter](C2ComponentInterface *p) mutable {
                // capture module so that we ensure we still have it while deleting interface
                deleter(p); // delete interface first
                module.reset(); // remove module ref (not technically needed)
    });
    return res;
}

c2_status_t C2PlatformComponentStore::ComponentModule::createComponent(
        c2_node_id_t id, std::shared_ptr<C2Component> *component,
        std::function<void(::C2Component*)> deleter) {
    component->reset();
    if (mInit != C2_OK) {
        return mInit;
    }
    std::shared_ptr<ComponentModule> module = shared_from_this();
    c2_status_t res = mComponentFactory->createComponent(
            id, component, [module, deleter](C2Component *p) mutable {
                // capture module so that we ensure we still have it while deleting component
                deleter(p); // delete component first
                module.reset(); // remove module ref (not technically needed)
    });
    return res;
}

std::shared_ptr<const C2Component::Traits> C2PlatformComponentStore::ComponentModule::getTraits() {
    std::unique_lock<std::recursive_mutex> lock(mLock);
    return mTraits;
}

C2PlatformComponentStore::C2PlatformComponentStore()
    : mVisited(false),
      mReflector(std::make_shared<C2ReflectorHelper>()),
      mInterface(mReflector) {
}

// For testing only
C2PlatformComponentStore::C2PlatformComponentStore(
    std::vector<std::tuple<C2String,
                C2ComponentFactory::CreateCodec2FactoryFunc,
                C2ComponentFactory::DestroyCodec2FactoryFunc>> funcs)
    : mVisited(false),
      mReflector(std::make_shared<C2ReflectorHelper>()),
      mInterface(mReflector),
      mCodec2FactoryFuncs(funcs) {

    for(auto const& func: mCodec2FactoryFuncs) {
        mComponents.emplace(std::get<0>(func), func);
    }
}

c2_status_t C2PlatformComponentStore::copyBuffer(
        std::shared_ptr<C2GraphicBuffer> src, std::shared_ptr<C2GraphicBuffer> dst) {
    (void)src;
    (void)dst;
    return C2_OMITTED;
}

c2_status_t C2PlatformComponentStore::query_sm(
        const std::vector<C2Param*> &stackParams,
        const std::vector<C2Param::Index> &heapParamIndices,
        std::vector<std::unique_ptr<C2Param>> *const heapParams) const {
    return mInterface.query(stackParams, heapParamIndices, C2_MAY_BLOCK, heapParams);
}

c2_status_t C2PlatformComponentStore::config_sm(
        const std::vector<C2Param*> &params,
        std::vector<std::unique_ptr<C2SettingResult>> *const failures) {
    return mInterface.config(params, C2_MAY_BLOCK, failures);
}

void C2PlatformComponentStore::visitComponents() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mVisited) {
        return;
    }
    for (auto &pathAndLoader : mComponents) {
        const C2String &path = pathAndLoader.first;
        ComponentLoader &loader = pathAndLoader.second;
        std::shared_ptr<ComponentModule> module;
        if (loader.fetchModule(&module) == C2_OK) {
            std::shared_ptr<const C2Component::Traits> traits = module->getTraits();
            if (traits) {
                mComponentList.push_back(traits);
                mComponentNameToPath.emplace(traits->name, path);
                for (const C2String &alias : traits->aliases) {
                    mComponentNameToPath.emplace(alias, path);
                }
            }
        }
    }
    mVisited = true;
}

std::vector<std::shared_ptr<const C2Component::Traits>> C2PlatformComponentStore::listComponents() {
    // This method SHALL return within 500ms.
    visitComponents();
    return mComponentList;
}

c2_status_t C2PlatformComponentStore::findComponent(
        C2String name, std::shared_ptr<ComponentModule> *module) {
    (*module).reset();
    visitComponents();

    auto pos = mComponentNameToPath.find(name);
    if (pos != mComponentNameToPath.end()) {
        return mComponents.at(pos->second).fetchModule(module);
    }
    return C2_NOT_FOUND;
}

c2_status_t C2PlatformComponentStore::createComponent(
        C2String name, std::shared_ptr<C2Component> *const component) {
    // This method SHALL return within 100ms.
    component->reset();
    std::shared_ptr<ComponentModule> module;
    c2_status_t res = findComponent(name, &module);
    if (res == C2_OK) {
        // TODO: get a unique node ID
        res = module->createComponent(0, component);
    }
    return res;
}

c2_status_t C2PlatformComponentStore::createInterface(
        C2String name, std::shared_ptr<C2ComponentInterface> *const interface) {
    // This method SHALL return within 100ms.
    interface->reset();
    std::shared_ptr<ComponentModule> module;
    c2_status_t res = findComponent(name, &module);
    if (res == C2_OK) {
        // TODO: get a unique node ID
        res = module->createInterface(0, interface);
    }
    return res;
}

c2_status_t C2PlatformComponentStore::querySupportedParams_nb(
        std::vector<std::shared_ptr<C2ParamDescriptor>> *const params) const {
    return mInterface.querySupportedParams(params);
}

c2_status_t C2PlatformComponentStore::querySupportedValues_sm(
        std::vector<C2FieldSupportedValuesQuery> &fields) const {
    return mInterface.querySupportedValues(fields, C2_MAY_BLOCK);
}

C2String C2PlatformComponentStore::getName() const {
    return "android.componentStore.platform";
}

std::shared_ptr<C2ParamReflector> C2PlatformComponentStore::getParamReflector() const {
    return mReflector;
}

std::shared_ptr<C2ComponentStore> GetCodec2PlatformComponentStore() {
    static std::mutex mutex;
    static std::weak_ptr<C2ComponentStore> platformStore;
    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<C2ComponentStore> store = platformStore.lock();
    if (store == nullptr) {
        store = std::make_shared<C2PlatformComponentStore>();
        platformStore = store;
    }
    return store;
}

// For testing only
std::shared_ptr<C2ComponentStore> GetTestComponentStore(
        std::vector<std::tuple<C2String,
        C2ComponentFactory::CreateCodec2FactoryFunc,
        C2ComponentFactory::DestroyCodec2FactoryFunc>> funcs) {
    return std::shared_ptr<C2ComponentStore>(new C2PlatformComponentStore(funcs));
}
} // namespace android
