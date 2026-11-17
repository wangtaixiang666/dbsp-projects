# Project 1 · 存储引擎内存管理

对应 BusTub Project 1，实现存储引擎的内存管理三部分：**可扩展哈希表**、**LRU-K 页替换器**、**缓冲池管理器实例**。

所有实现遵循两条约束：只改动指定的 6 个文件；不使用 `std::unordered_map` 等现成哈希容器，全部并发访问由 `std::mutex` 保护。

---

## 1. 可扩展哈希表（Extendible Hash Table）

`src/include/container/hash/extendible_hash_table.h`
`src/container/hash/extendible_hash_table.cpp`

在缓冲池中维护 `page_id → frame_id` 的映射，目录项数随全局深度按 2 的幂增长。

| 要点 | 做法 |
|---|---|
| 初始化 | 构造时目录只放 1 个局部深度为 0 的桶，否则首次 `Find` 会解引用空目录 |
| 定位 | `IndexOf` 取 `std::hash<K>(key)` 的低 `global_depth` 位作为目录下标 |
| 插入 | 先尝试写入；写不进（桶满且键是新键）才分裂，并循环直到插入成功 |
| 目录翻倍 | 局部深度 == 全局深度时，把目录整体 `push_back` 复制一份，`global_depth++` |
| 兄弟指针 | 局部深度 < 全局深度时目录不扩，只把 `split_bit` 位为 1 的目录项改指向新桶 |
| 重分配 | 分裂后把原桶所有键值对按新目录重新落桶；此时目标桶可能暂时超容量，正好触发下一轮分裂 |
| 容量判断 | `Bucket::Insert` 用 `>=` 而非 `==`（框架的 `IsFull()`），因为重分配后桶可能已超额 |

关键点：**分裂发生在插入之前**（先探测桶是否已满），而不是插入之后再补救；这样任何时刻桶内都不会超过 `bucket_size` 条有效数据。

---

## 2. LRU-K 页替换器（LRU-K Replacer）

`src/include/buffer/lru_k_replacer.h`
`src/buffer/lru_k_replacer.cpp`

为每个 frame 维护最近的 K 次访问时间戳，按**后向 K 距离**淘汰。

| 接口 | 语义 |
|---|---|
| `RecordAccess` | 追加当前时间戳；历史超过 K 条时弹出最旧一条，只保留 K 条 |
| `SetEvictable` | 改变可淘汰状态，同步维护 `curr_size_`（可淘汰帧数） |
| `Evict` | 在可淘汰帧中选出 victim 并移出替换器 |
| `Remove` | 仅在可淘汰时移除；被 pin 住的帧不动 |
| `Size` | 返回当前可淘汰帧数 |

淘汰顺序（这是本题的核心）：

1. 访问次数不足 K 次的帧，后向 K 距离为 **+∞**，优先级高于任何有限距离；
2. 同为 +∞ 时，淘汰**最早一次访问**的帧（历史首元素时间戳最小）；
3. 都有 K 次访问时，后向 K 距离最大者优先被淘汰，即保留的 K 条历史中**最旧时间戳最小**者——恰好就是历史首元素最小的帧；
4. 时间戳相同则按 frame_id 升序打破平局，保证多次运行结果一致。

---

## 3. 缓冲池管理器实例（Buffer Pool Manager Instance）

`src/include/buffer/buffer_pool_manager_instance.h`
`src/buffer/buffer_pool_manager_instance.cpp`

`pool_size` 个物理帧 + `page_table_`（可扩展哈希表）+ `free_list_`（空闲帧）+ `replacer_`（LRU-K）。

**取帧 `AcquireFrame`**：优先从 `free_list_` 取；空闲帧耗尽才向 replacer 要 victim。拿到 victim 后，若该页为脏页先 `WritePage` 落盘，再从 `page_table_` 删除旧映射。所有帧都被 pin 住时返回失败，上层接口返回 `nullptr`。

| 接口 | 行为 |
|---|---|
| `NewPage` | 取帧 → `ResetMemory` → 分配新 `page_id` → `pin_count = 1`、`is_dirty = false` → 建映射 → 记录访问 → 置为不可淘汰 |
| `FetchPage` | 命中则 `pin_count++` 并刷新访问历史；未命中则取帧、从磁盘读入、建映射、pin 住 |
| `UnpinPage` | 页不在池中或 `pin_count <= 0` 返回 false；否则减 1，`is_dirty` 只置位不清零，归零时才设为可淘汰 |
| `FlushPage` | 写回磁盘并清除脏位；不存在返回 false |
| `FlushAllPages` | 遍历所有有效帧写回并清脏位 |
| `DeletePage` | 未缓存视为已删除返回 true；仍被 pin 住返回 false；否则移除映射、`ResetMemory`、归还 `free_list_` |
| `FlushAllPages` 顺序 | 跳过 `INVALID_PAGE_ID` 的帧，避免把空帧写脏 |

两个容易出错的地方：脏位**只在 unpin 时由 `is_dirty` 参数置位**，且不能把已有的 `true` 覆盖成 `false`；淘汰脏页必须**先写回再清帧**，否则数据丢失。

---

## 验证

```bash
mkdir -p build && cd build
cmake -DCMAKE_CXX_COMPILER=clang++-14 -DCMAKE_C_COMPILER=clang-14 ..
make -j"$(nproc)" extendible_hash_table_test lru_k_replacer_test buffer_pool_manager_instance_test

./test/extendible_hash_table_test
./test/lru_k_replacer_test
./test/buffer_pool_manager_instance_test
```

无参数运行即执行全部用例；也可用 `--gtest_filter=` 指定单个用例。

> 若运行时偶发 `exit 139` 段错误，是 ASAN 与地址随机化冲突的已知问题，重跑即可；`sudo sysctl -w kernel.randomize_va_space=0` 可彻底消除。
