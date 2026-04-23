#include <gtest/gtest.h>

#include <memory>
#include <thread>

#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include <future>
#include <vector>
#include <sys/wait.h>

#include "knowhere/io_context_pool.h"
#include "knowhere/io_reader.h"
#ifdef MILVUS_COMMON_WITH_LIBAIO
#include "knowhere/aio_context_pool.h"
#endif
#ifdef WITH_IO_URING
#include "knowhere/uring_context_pool.h"
#endif

#ifdef WITH_IO_URING
#ifdef MILVUS_COMMON_WITH_LIBAIO
TEST(IOContextPoolTest, InitShouldFallbackToAioWhenUringUnavailable) {
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        struct rlimit lim;
        lim.rlim_cur = 3;
        lim.rlim_max = 3;
        if (setrlimit(RLIMIT_NOFILE, &lim) != 0) {
            _exit(20);
        }

        IOContextPoolConfig cfg;
        cfg.num_ctx = 1;
        cfg.max_events = 128;
        const bool ok = IOContextPool::InitGlobal(cfg);
        if (!ok) {
            _exit(21);
        }

        auto pool = IOContextPool::GetGlobal();
        if (pool == nullptr || pool->Backend() != IOBackend::AIO) {
            _exit(22);
        }
        _exit(0);
    }

    int status = 0;
    ASSERT_EQ(waitpid(pid, &status, 0), pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
}
#endif
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

TEST(IOContextPoolTest, ReadAsyncShouldBeDeferredFuture) {
    const char path[] = "/tmp/io_reader_async_mode_test.bin";
    int fd = ::open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    ASSERT_GE(fd, 0);

    constexpr size_t kSize = 4096;
    std::vector<std::byte> content(kSize, std::byte{0x3});
    ASSERT_EQ(::write(fd, content.data(), static_cast<size_t>(kSize)), static_cast<ssize_t>(kSize));

    auto reader = IOReader(fd);
    std::vector<std::byte> buffer(kSize);
    std::vector<std::byte*> buffers{buffer.data()};
    std::vector<size_t> offsets{0};

    auto fut = reader.ReadAsync(std::move(buffers), kSize, std::move(offsets));
    ASSERT_EQ(fut.wait_for(std::chrono::milliseconds(0)), std::future_status::deferred);

    ::close(fd);
    ::unlink(path);
}
#endif
