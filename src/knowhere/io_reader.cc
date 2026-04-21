#include "knowhere/io_reader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "log/Log.h"

IOReader::IOReader() : io_pool_(IOContextPool::GetGlobal()) {
}

IOReader::IOReader(int fd) : fd_(fd), io_pool_(IOContextPool::GetGlobal()) {
}

IOReader::IOReader(int fd, std::shared_ptr<IOContextPool> io_pool) : fd_(fd), io_pool_(std::move(io_pool)) {
}

IOReader::IOReader(std::shared_ptr<IOContextPool> io_pool) : io_pool_(std::move(io_pool)) {
}

bool
IOReader::Read(IOReaderSpan<std::byte*> buf, size_t size, IOReaderSpan<off_t> offsets) const {
    if (buf.size() != offsets.size()) {
        throw std::invalid_argument("buffers and offsets must have same size");
    }

    std::vector<std::byte*> buffers(buf.size());
    std::vector<size_t> read_offsets(offsets.size());

    for (size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] == nullptr) {
            throw std::invalid_argument("buffer pointer should not be null");
        }
        if (offsets[i] < 0) {
            throw std::invalid_argument("offset should be non-negative");
        }
        buffers[i] = buf[i];
        read_offsets[i] = static_cast<size_t>(offsets[i]);
    }

    return ReadAsync(std::move(buffers), size, std::move(read_offsets)).get();
}

std::future<bool>
IOReader::ReadAsync(std::vector<std::byte*>&& buffers, size_t size, std::vector<size_t>&& offsets) const {
    if (size == 0) {
        throw std::invalid_argument("size should be greater than 0");
    }
    if (buffers.size() != offsets.size()) {
        throw std::invalid_argument("buffers and offsets must have same size");
    }
    if (buffers.empty()) {
        return std::async(std::launch::deferred, [] { return true; });
    }
    if (fd_ < 0) {
        throw std::invalid_argument("invalid file descriptor");
    }

    for (const auto* buffer : buffers) {
        if (buffer == nullptr) {
            throw std::invalid_argument("buffer pointer should not be null");
        }
    }

    auto pool = io_pool_ ? io_pool_ : IOContextPool::GetGlobal();
    if (pool == nullptr || !pool->IsInitialized()) {
        throw std::runtime_error("IOContextPool is not initialized");
    }

    return std::async(std::launch::async,
                      [fd = fd_,
                       size,
                       buffers = std::move(buffers),
                       offsets = std::move(offsets),
                       pool]() mutable -> bool {
                          switch (pool->Backend()) {
#ifdef MILVUS_COMMON_WITH_LIBAIO
                              case IOBackend::AIO: {
                                  auto ctx = pool->PopAio();
                                  if (ctx == nullptr) {
                                      return false;
                                  }

                                  std::vector<struct iocb> cbs(buffers.size());
                                  std::vector<struct iocb*> cb_ptrs(buffers.size());
                                  std::vector<struct io_event> events(buffers.size());

                                  for (size_t i = 0; i < buffers.size(); ++i) {
                                      io_prep_pread(&cbs[i], fd, reinterpret_cast<void*>(buffers[i]), size, offsets[i]);
                                      cb_ptrs[i] = &cbs[i];
                                  }

                                  const auto submitted = io_submit(ctx, static_cast<long>(cb_ptrs.size()), cb_ptrs.data());
                                  if (submitted != static_cast<long>(cb_ptrs.size())) {
                                      pool->PushAio(ctx);
                                      return false;
                                  }

                                  const auto completed = io_getevents(ctx,
                                                                      static_cast<long>(events.size()),
                                                                      static_cast<long>(events.size()),
                                                                      events.data(),
                                                                      nullptr);

                                  pool->PushAio(ctx);
                                  if (completed != static_cast<long>(events.size())) {
                                      return false;
                                  }

                                  for (const auto& event : events) {
                                      if (event.res < 0 || static_cast<size_t>(event.res) != size) {
                                          return false;
                                      }
                                  }

                                  return true;
                              }
#endif
#ifdef WITH_IO_URING
                              case IOBackend::IO_URING: {
                                  auto* ring = pool->PopUring();
                                  if (ring == nullptr) {
                                      return false;
                                  }

                                  size_t processed = 0;
                                  while (processed < buffers.size()) {
                                      size_t batch = 0;
                                      for (; processed + batch < buffers.size(); ++batch) {
                                          auto* sqe = io_uring_get_sqe(ring);
                                          if (sqe == nullptr) {
                                              break;
                                          }
                                          const auto idx = processed + batch;
                                          io_uring_prep_read(sqe, fd, reinterpret_cast<void*>(buffers[idx]), size, offsets[idx]);
                                          sqe->user_data = idx;
                                      }

                                      if (batch == 0) {
                                          pool->PushUring(ring);
                                          return false;
                                      }

                                      const auto submitted = io_uring_submit(ring);
                                      if (submitted < 0 || static_cast<size_t>(submitted) != batch) {
                                          pool->PushUring(ring);
                                          return false;
                                      }

                                      size_t completed = 0;
                                      while (completed < batch) {
                                          io_uring_cqe* cqe = nullptr;
                                          if (io_uring_wait_cqe(ring, &cqe) < 0 || cqe == nullptr) {
                                              pool->PushUring(ring);
                                              return false;
                                          }
                                          if (cqe->res < 0 || static_cast<size_t>(cqe->res) != size) {
                                              io_uring_cqe_seen(ring, cqe);
                                              pool->PushUring(ring);
                                              return false;
                                          }
                                          io_uring_cqe_seen(ring, cqe);
                                          ++completed;
                                      }

                                      processed += batch;
                                  }

                                  pool->PushUring(ring);
                                  return true;
                              }
#endif
                              default:
                                  return false;
                          }
                      });
}

IOBackend
IOReader::Backend() const {
    return io_pool_ ? io_pool_->Backend() : IOBackend::UNKNOWN;
}

std::string
IOReader::BackendName() const {
    return io_pool_ ? io_pool_->BackendName() : "unknown";
}

bool
IOReader::IsReady() const {
    return fd_ >= 0 && io_pool_ != nullptr && io_pool_->IsInitialized();
}
