# 数据库系统原理 · 课程项目

本仓库用于提交《数据库系统原理》课程的编程项目，每个 Project 一个目录。

| 项目 | 内容 | 状态 |
|---|---|---|
| [Project1](Project1/) | 存储引擎 · 内存管理（可扩展哈希表 / LRU-K 替换器 / 缓冲池管理器） | 已完成 |

## 环境

- Ubuntu 20.04+（WSL2 亦可）
- CMake ≥ 3.10、Clang 14（或 GCC 9+，需支持 C++17）

## 项目结构约定

每个 Project 目录下只保留该次作业要求提交的源文件，路径与 BusTub 框架保持一致，便于直接覆盖到框架中编译：

```
Project1/
├── README.md                      # 实现说明
├── project1-submission.zip        # 按 CMake 提交目标打包的文件集
└── src/
    ├── container/hash/extendible_hash_table.cpp
    ├── buffer/lru_k_replacer.cpp
    ├── buffer/buffer_pool_manager_instance.cpp
    └── include/...
```

## 使用方法

将 `Project1/src/` 下的文件覆盖到 BusTub 源码树的同名路径，然后：

```bash
mkdir -p build && cd build
cmake -DCMAKE_CXX_COMPILER=clang++-14 -DCMAKE_C_COMPILER=clang-14 ..
make -j"$(nproc)" extendible_hash_table_test lru_k_replacer_test buffer_pool_manager_instance_test
./test/extendible_hash_table_test
./test/lru_k_replacer_test
./test/buffer_pool_manager_instance_test
```

打包提交文件：

```bash
make submit-p1     # 生成 project1-submission.zip
```
