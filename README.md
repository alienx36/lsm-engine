# LSM-Lite: Production-Grade LSM-Tree Storage Engine from Scratch

An embedded, key-value storage engine built in C++17 utilizing the **Log-Structured Merge-Tree (LSM-Tree)** architecture. This project implements decoupled architectural layers to enforce data durability, high write throughput, and automatic storage optimization—mirroring the core internals of enterprise systems like RocksDB and Apache Cassandra.

---

## 🏗️ Architectural Overview

The engine separates responsibilities into four distinct, highly decoupled subsystems to adhere to SOLID principles and isolate memory management from physical disk I/O bottlenecks:

```text
                  +-----------------------+
                  |      Client API       |
                  |   (put / get / del)   |
                  +-----------+-----------+
                              |
                              v
                   +--------------------+
                   |   Master Engine    |
                   |    Orchestrator    |
                   +-----+--------+-----+
                         |        |
     +-------------------+        +-------------------+
     |                                                |
     v                                                v

+----------------+                         +----------------+
|  Memory Tier   |                         |   Disk Tier    |
|                |                         |                |
| +------------+ |                         | +------------+ |
| |  Memtable  | |                         | |    WAL     | |
| | (RB-Tree)  | |                         | |  (Binary)  | |
| +------------+ |                         | +------------+ |
+----------------+                         +--------+-------+
                                                    |
                                                 (Flush)
                                                    |
                                                    v
                                           +----------------+
                                           |   SSTables     |
                                           | (Sparse Index) |
                                           +--------+-------+
                                                    |
                                              (Compaction)
                                                    |
                                                    v
                                           +----------------+
                                           | Master SSTable |
                                           +----------------+
```

### Core Architectural Layers

#### 1. Master Engine Orchestrator (`Engine`)

Serves as a unified facade protecting underlying components. Coordinates startup recovery (reloading existing `*.db` SSTables in chronological order and replaying `wal.log`), routes `put`/`del` writes concurrently to memory and disk logs, triggers flushes at `MAX_MEMTABLE_KEYS`, and schedules compaction operations.

#### 2. Write-Ahead Log (`WAL`)

An append-only binary logger offering crash resiliency. Operates at **O(1)** complexity by bypassing filesystem newline formatting using strict binary serialization layouts (`[4B key_len][4B val_len][key][value]`).

#### 3. Memtable Cache (`Memtable`)

A fast, in-memory component backed by a self-balancing Red-Black Tree (`std::map`). Arranges keys in strict alphabetical order to decouple ingestion timeline from physical storage structures, and records `TOMBSTONE` markers for deleted keys.

#### 4. Sorted String Tables (`SSTable`)

Immutable block-data files storing keys sequentially on disk. Employs an in-memory **Sparse Index Offset Map** (sampling every `SPARSE_INDEX_INTERVAL`-th key) with `std::upper_bound` binary search and direct hardware `seekg()` jumps to provide **O(log N)** disk read speeds.

---

## ⚡ Core Features

- **Strict Binary Serialization** – Custom `[key_len][val_len][key][value]` packing protocol preventing injection issues and reducing storage overhead compared to plaintext formats.
- **Point-in-Time Crash Recovery** – Automatic state reconstruction upon system reboots by reloading existing SSTable files in chronological order and replaying `wal.log` into the Memtable.
- **Tiered Read Path & Tombstone Deletes** – Read requests check high-speed RAM first and fall back to newest-to-oldest SSTable scans on cache misses; `del()` writes a `TOMBSTONE` marker that shadows older SSTable entries until compaction purges them.
- **Streaming Multi-Way Global Compaction** – Background garbage collection using a min-heap (`std::priority_queue`) K-way merge across ordered SSTable streams to retain only the newest live values, physically remove tombstones, reclaim disk space, and truncate obsolete WAL logs.

---

## 🔄 Multi-Way Global Compaction Engine

One of the defining characteristics of an LSM-Tree is that data is never updated or deleted in-place. Instead, updates and `TOMBSTONE` deletes are continuously appended to the Write-Ahead Log (WAL), accumulated in the Memtable, and periodically flushed into immutable SSTable files.

Over time, multiple SSTables may contain historical versions or tombstones for the same key:

```text
sstable_1.db
user_id -> 1001
status  -> inactive

sstable_2.db
user_id -> 9999
status  -> <TOMBSTONE>
```

Searching through an ever-growing collection of SSTables would eventually degrade read performance and waste disk space with obsolete records.

To solve this problem, LSM-Lite implements a **Streaming Multi-Way Global Compaction Engine**.

### Compaction Workflow

The compaction worker performs the following steps:

1. Flushes any remaining Memtable entries so the WAL can be safely truncated.
2. Discovers all active SSTable files in chronological order.
3. Opens each SSTable as an ordered binary input stream.
4. Performs a streaming K-way merge-sort using a min-heap (`std::priority_queue`).
5. Resolves duplicate keys by retaining only the newest version (highest SSTable index).
6. Discards stale historical values and `TOMBSTONE` deleted records.
7. Writes the consolidated result into a temporary master file and promotes it to `sstable_master.db`.
8. Deletes obsolete SSTable files.
9. Truncates the Write-Ahead Log (`wal.log`) to `0` bytes once all live data has been safely persisted.

### Example

Before Compaction:

```text
sstable_1.db
key_A -> val_1
key_B -> val_1
key_C -> val_1

sstable_2.db
key_A -> val_2
key_C -> <TOMBSTONE>
key_E -> val_1

sstable_3.db
key_B -> val_3
key_F -> val_1
```

After Compaction:

```text
sstable_master.db
key_A -> val_2
key_B -> val_3
key_E -> val_1
key_F -> val_1
```

### Benefits

- Eliminates obsolete key versions and physically purges deleted records.
- Reclaims storage space consumed by stale records.
- Reduces the number of SSTables participating in lookups.
- Improves read performance by minimizing disk traversals.
- Enables safe WAL truncation after durable persistence.
- Maintains predictable long-term storage growth.

### Complexity Characteristics

| Operation                | Complexity               |
| ------------------------ | ------------------------ |
| SSTable Discovery        | O(N)                     |
| Multi-Way Merge          | O(Total Records * log K) |
| Duplicate Resolution     | O(1) per record          |
| Final SSTable Generation | O(Total Records)         |

Where **N** is the number of files in the directory and **K** is the number of SSTables participating in the compaction cycle.

---

## 📊 Component Specifications

| Component    | Medium           | Primary Responsibility                                      | Complexity                          |
| ------------ | ---------------- | ----------------------------------------------------------- | ----------------------------------- |
| **WAL**      | Disk (`wal.log`) | Sequential data persistence and durability                  | Write: **O(1)**                     |
| **Memtable** | RAM (`std::map`) | High-speed cache and lexicographical sorting                | Read/Write: **O(log N)**            |
| **SSTable**  | Disk (`*.db`)    | Immutable sorted storage with sparse index + `seekg()` jump | Read: **O(log N)** via Sparse Index |

---

## 📈 Benchmark Results (`benchmark.cpp`)

Measured locally on Windows (`g++ -std=c++17 -O2`, 300 mixed `put`/`update`/`del` operations across 100 unique keys with `MAX_MEMTABLE_KEYS = 3` producing 100 SSTables prior to compaction, followed by 1,000 point reads):

| Metric | Measured Result |
| :--- | :--- |
| **Storage Before Compaction** | `18,518 bytes` (uncompacted SSTables + `wal.log`) |
| **Storage After Compaction** | `2,070 bytes` (`sstable_master.db`, `wal.log` = `0 bytes`) |
| **Disk Space Reclaimed** | **88.82%** |
| **K-Way Compaction Latency** | **15.54 ms** (merging 100 SSTables) |
| **Post-Compaction Point Reads** | **29.01 µs/read** (`~34,476 reads/sec` across 1,000 queries) |

### Run the Benchmark

```bash
g++ -std=c++17 -O2 -I./include src/wal.cpp src/memtable.cpp src/sstable.cpp src/engine.cpp benchmark.cpp -o lsm_bench.exe
./lsm_bench.exe
```

---

## 🛠️ Compilation & Execution

This engine requires a standard **C++17** compiler (and is compatible with older Windows MinGW `g++` builds).

### Local Build Execution

```bash
g++ -std=c++17 -Wall -Wextra -I./include src/wal.cpp src/memtable.cpp src/sstable.cpp src/engine.cpp main.cpp -o lsm_engine.exe
```

### Run

```bash
./lsm_engine.exe
```

---

## Expected Output Behavior

```text
--- Booting Production-Grade LSM Engine ---
[Engine] Initializing decoupled storage subsystems...
[Engine] Recovery complete. Loaded 0 SSTable(s); Memtable has 0 key(s).

[Write Tier 1] Writing base data blocks...

[Flush] Threshold hit! Writing sstable_1.db to disk...
[Flush] Flash-clear RAM complete.


[Write Tier 2] Updating & deleting values...

[Flush] Threshold hit! Writing sstable_2.db to disk...
[Flush] Flash-clear RAM complete.


[Write Tier 3] Unflushed writes before simulated reboot...

--- Simulating Reboot & Crash Recovery ---
[Engine] Initializing decoupled storage subsystems...
[Engine] Recovery complete. Loaded 2 SSTable(s); Memtable has 5 key(s).
  (Hit RAM Memtable)
  (Hit RAM Memtable)
  (Hit RAM Memtable)
  (Hit RAM Memtable)

--- Triggering Streaming K-Way Global Compaction ---

[Flush] Threshold hit! Writing sstable_3.db to disk...
[Flush] Flash-clear RAM complete.


[Compaction] Dynamic Global Worker Active!
[Compaction] Streaming K-way merge of 3 SSTable(s)...
  (Deleted obsolete file: sstable_1.db)
  (Deleted obsolete file: sstable_2.db)
  (Deleted obsolete file: sstable_3.db)
  (Truncated Write-Ahead Log: 'wal.log' wiped to 0 bytes)
[Compaction] Success! Global compaction consolidated to 'sstable_master.db'.

--- Testing Post-Compaction Clean Reads ---
  (Hit Disk SSTable)
Query 'key_A' (Expect val_2): val_2
  (Hit Disk SSTable)
Query 'key_B' (Expect val_3): val_3
Query 'key_C' (Expect NOT_FOUND): NOT_FOUND
  (Hit Disk SSTable)
Query 'key_E' (Expect val_1): val_1
  (Hit Disk SSTable)
Query 'key_F' (Expect val_1): val_1
  (Hit Disk SSTable)
  (Hit Disk SSTable)
  (Hit Disk SSTable)
  (Hit Disk SSTable)

All assertions passed!
```

---

## 🎯 Design Goals

- High write throughput through append-only logging
- Crash-safe durability guarantees across SSTables and WAL
- Efficient point lookups via sparse indexing and binary search
- Immutable on-disk storage structures with tombstone deletes
- Predictable streaming K-way compaction behavior
- Clear separation of concerns between memory and disk subsystems
- Educational implementation of modern LSM-tree internals