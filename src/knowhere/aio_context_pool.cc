#include "knowhere/aio_context_pool.h"

#include "knowhere/io_context_pool.h"
#include "log/Log.h"

std::atomic<size_t> AioContextPool::global_aio_pool_size{0};
std::atomic<size_t> AioContextPool::global_aio_max_events{0};
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
    if (!InitGlobalAioPoolWithValidation(num_ctx, max_events)) {
        return false;
    }
    auto io_pool = IOContextPool::GetGlobal();
    if (io_pool == nullptr || !io_pool->IsInitialized()) {
        return false;
    }
    if (io_pool->Backend() != IOBackend::AIO) {
        LOG_ERROR("Global IOContextPool backend is %s, legacy AIO API is unavailable", io_pool->BackendName().c_str());
        return false;
    }
    return true;
}

std::shared_ptr<AioContextPool>
AioContextPool::GetGlobalAioPool() {
    auto io_pool = IOContextPool::GetGlobal();
    if (io_pool == nullptr || !io_pool->IsInitialized()) {
        return nullptr;
    }
    if (io_pool->Backend() != IOBackend::AIO) {
        return nullptr;
    }
    return io_pool->GetAioPoolForLegacy();
}
