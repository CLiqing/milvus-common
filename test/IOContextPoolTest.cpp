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
    cfg.prefer_io_uring = true;
    cfg.num_ctx = 2;
    cfg.max_events = 128;

    ASSERT_TRUE(IOContextPool::InitGlobal(cfg));

    auto pool = IOContextPool::GetGlobal();
    ASSERT_NE(pool, nullptr);

    auto b1 = pool->BackendName();
    auto b2 = pool->BackendName();
    ASSERT_EQ(b1, b2);
    ASSERT_TRUE(b1 == "io_uring" || b1 == "aio");
}

TEST(IOContextPoolTest, InvalidConfigRejected) {
    IOContextPoolConfig cfg;
    cfg.prefer_io_uring = true;
    cfg.num_ctx = 0;
    cfg.max_events = 128;

    ASSERT_FALSE(IOContextPool::InitGlobal(cfg));
}

TEST(IOContextPoolTest, ReaderCanBeConstructed) {
    IOContextPoolConfig cfg;
    cfg.prefer_io_uring = true;
    cfg.num_ctx = 1;
    cfg.max_events = 128;
    ASSERT_TRUE(IOContextPool::InitGlobal(cfg));

    IOReader reader;
    ASSERT_TRUE(reader.BackendName() == "io_uring" || reader.BackendName() == "aio");
}

#ifdef MILVUS_COMMON_WITH_LIBAIO
TEST(IOContextPoolTest, LegacyAioInitStillWorksViaUnifiedPath) {
    ASSERT_TRUE(AioContextPool::InitGlobalAioPool(2, 128));
    auto p = AioContextPool::GetGlobalAioPool();
    ASSERT_NE(p, nullptr);
}
#endif

#ifdef WITH_IO_URING
TEST(IOContextPoolTest, LegacyUringInitStillWorksViaUnifiedPath) {
    ASSERT_TRUE(UringContextPool::InitGlobalUringPool(1, 128));
    auto p = UringContextPool::GetGlobalUringPool();
    ASSERT_NE(p, nullptr);
}
#endif
