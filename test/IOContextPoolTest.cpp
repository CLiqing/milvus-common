#include <gtest/gtest.h>

#include <memory>

#include "knowhere/io_context_pool.h"
#include "knowhere/io_reader.h"
#ifdef MILVUS_COMMON_WITH_LIBAIO
#include "knowhere/aio_context_pool.h"
#endif
#ifdef WITH_IO_URING
#include "knowhere/uring_context_pool.h"
#endif

TEST(IOContextPoolTest, BackendIsSelectedAtInit) {
    IOContextPoolConfig cfg;
    cfg.num_ctx = 2;
    cfg.max_events = 128;

    ASSERT_TRUE(IOContextPool::InitGlobal(cfg));

    auto pool = IOContextPool::GetGlobal();
    ASSERT_NE(pool, nullptr);

    auto backend = pool->Backend();
#ifdef WITH_IO_URING
    ASSERT_EQ(backend, IOBackend::IO_URING);
#else
    ASSERT_EQ(backend, IOBackend::AIO);
#endif
}

TEST(IOContextPoolTest, InvalidConfigRejected) {
    IOContextPoolConfig cfg;
    cfg.num_ctx = 0;
    cfg.max_events = 128;

    ASSERT_FALSE(IOContextPool::InitGlobal(cfg));
}

TEST(IOContextPoolTest, ReinitWithDifferentConfigShouldFail) {
    IOContextPoolConfig cfg;
    cfg.num_ctx = 2;
    cfg.max_events = 128;
    ASSERT_TRUE(IOContextPool::InitGlobal(cfg));

    IOContextPoolConfig mismatch = cfg;
    mismatch.num_ctx = 4;

    ASSERT_FALSE(IOContextPool::InitGlobal(mismatch));
}

#ifdef MILVUS_COMMON_WITH_LIBAIO
TEST(IOContextPoolTest, DefaultConfigShouldMatchLegacyAioPoolSize) {
    IOContextPoolConfig cfg;
    ASSERT_EQ(cfg.num_ctx, default_pool_size);
    ASSERT_EQ(cfg.max_events, default_max_events);
}
#endif

TEST(IOContextPoolTest, ReaderCanBeConstructed) {
    IOContextPoolConfig cfg;
    cfg.num_ctx = 2;
    cfg.max_events = 128;
    ASSERT_TRUE(IOContextPool::InitGlobal(cfg));

    IOReader reader;
#ifdef WITH_IO_URING
    ASSERT_EQ(reader.Backend(), IOBackend::IO_URING);
#else
    ASSERT_EQ(reader.Backend(), IOBackend::AIO);
#endif
}

#ifdef MILVUS_COMMON_WITH_LIBAIO
TEST(IOContextPoolTest, LegacyAioInitStillWorksViaUnifiedPath) {
#ifdef WITH_IO_URING
    ASSERT_FALSE(AioContextPool::InitGlobalAioPool(2, 128));
    ASSERT_EQ(AioContextPool::GetGlobalAioPool(), nullptr);
#else
    ASSERT_TRUE(AioContextPool::InitGlobalAioPool(2, 128));
    auto p = AioContextPool::GetGlobalAioPool();
    ASSERT_NE(p, nullptr);
#endif
}
#endif

#ifdef WITH_IO_URING
TEST(IOContextPoolTest, LegacyUringInitStillWorksViaUnifiedPath) {
    ASSERT_TRUE(UringContextPool::InitGlobalUringPool(1, 128));
    auto p = UringContextPool::GetGlobalUringPool();
    ASSERT_NE(p, nullptr);
}
#endif
