#include "knowhere/aio_context_pool.h"

#include "knowhere/io_context_pool.h"
#include "log/Log.h"

size_t AioContextPool::global_aio_pool_size = 0;
size_t AioContextPool::global_aio_max_events = 0;
std::mutex AioContextPool::global_aio_pool_mut;

bool
AioContextPool::InitGlobalAioPoolWithValidation(size_t num_ctx, size_t max_events) {
    if (num_ctx <= 0) {
        LOG_ERROR("num_ctx should be bigger than 0");
        return false;
    }
    if (max_events > default_max_events) {
        LOG_ERROR("max_events %d should not be larger than %d", max_events, default_max_events);
        return false;
    }
    if (global_aio_pool_size == 0) {
        std::scoped_lock lk(global_aio_pool_mut);
        if (global_aio_pool_size == 0) {
            global_aio_pool_size = num_ctx;
            global_aio_max_events = max_events;
            return true;
        }
    }
    LOG_WARN("Global AioContextPool has already been inialized with context num: %d", global_aio_pool_size);
    return true;
}

std::shared_ptr<AioContextPool>
AioContextPool::GetGlobalAioPoolDirect() {
    if (global_aio_pool_size == 0) {
        std::scoped_lock lk(global_aio_pool_mut);
        if (global_aio_pool_size == 0) {
            global_aio_pool_size = default_pool_size;
            global_aio_max_events = default_max_events;
            LOG_WARN("Global AioContextPool has not been inialized yet, init it now with context num: %d",
                     global_aio_pool_size);
        }
    }
    static auto pool = std::shared_ptr<AioContextPool>(new AioContextPool(global_aio_pool_size, global_aio_max_events));
    return pool;
}

bool
AioContextPool::InitGlobalAioPool(size_t num_ctx, size_t max_events) {
    IOContextPoolConfig cfg;
    cfg.prefer_io_uring = false;
    cfg.num_ctx = num_ctx;
    cfg.max_events = max_events;
    return IOContextPool::InitGlobal(cfg);
}

std::shared_ptr<AioContextPool>
AioContextPool::GetGlobalAioPool() {
    auto io_pool = IOContextPool::GetGlobal();
    if (io_pool == nullptr) {
        return nullptr;
    }
    auto legacy_pool = io_pool->GetAioPoolForLegacy();
    if (legacy_pool != nullptr) {
        return legacy_pool;
    }
    return GetGlobalAioPoolDirect();
}
