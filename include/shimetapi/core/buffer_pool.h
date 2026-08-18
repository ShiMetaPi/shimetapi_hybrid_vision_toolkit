// Copyright 2026 ShiMetaPi. Licensed under the Apache License, Version 2.0.
#ifndef SHIMETA_CORE_BUFFER_POOL_H
#define SHIMETA_CORE_BUFFER_POOL_H
#include <cstddef>
#include <cstdint>
#include <memory>
namespace Shimeta {

/// 对池 slab 的非拥有只读视图。仅在其所属 Frame（持有 owner）存活时有效。
struct BufferView {
    const uint8_t* data = nullptr;
    size_t         size = 0;
};

/// 固定大小 slab 池。acquire() 返回引用计数 handle（shared_ptr<uint8_t[]>）；
/// 最后一个引用释放时 slab 自动归还池。Frame 持有 owner 以保证零拷贝内存生命周期。
class BufferPool {
public:
    BufferPool(size_t slab_size, size_t slab_count);
    ~BufferPool();
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    /// 获取一个 slab；池耗尽返回 nullptr。
    std::shared_ptr<uint8_t[]> acquire();

    size_t slab_size() const;
    size_t capacity() const;     // 总 slab 数
    size_t available() const;    // 当前空闲 slab 数
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;  // shared：outstanding buffer 经删除器捕获副本，保 Impl 与 slab 存活
};

} // namespace Shimeta
#endif // SHIMETA_CORE_BUFFER_POOL_H
