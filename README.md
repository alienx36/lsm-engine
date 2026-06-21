# LSM-Lite: Production-Grade LSM-Tree Storage Engine from Scratch

An embedded, key-value storage engine built in C++17 utilizing the **Log-Structured Merge-Tree (LSM-Tree)** architecture. This project implements decoupled architectural layers to enforce data durability, high write throughput, and automatic storage optimization—mirroring the core internals of enterprise systems like RocksDB and Apache Cassandra.

---

## 🏗️ Architectural Overview

The engine separates responsibilities into four distinct, highly decoupled subsystems to adhere to SOLID principles and isolate memory management from physical disk I/O bottlenecks:

                  +-------------------+
                  |   Client API      |
                  |   (put / get)     |
                  +---------+---------+
                            |
                            v
                   +----------------+
                   |  Master Engine |
                   |  Orchestrator  |
                   +---+--------+---+
                       |        |
    +------------------+        +------------------+
    |                                              |
    v                                              v
+-------+--------+                             +-------+--------+
| Memory Tier    |                             | Disk Tier      |
|                |                             |                |
|  +----------+  |                             |  +----------+  |
|  | Memtable |  |                             |  |   WAL    |  |
|  | (RB-Tree)|  |                             |  | (Binary) |  |
|  +----------+  |                             |  +----------+  |
+----------------+                             +--------+-------+
| (Flush)
v
+--------+-------+
|    SSTables    |
| (Sparse Index)|
+--------+-------+
| (Compaction)
v
+--------+-------+
| Master SSTable |
+----------------+


1. **Master Engine Orchestrator (`Engine`)**: Serves as a unified facade protecting underlying components. Coordinates state playback transitions, routing writes concurrently to memory and disk logs, and scheduling compaction loops.
2. **Write-Ahead Log (`WAL`)**: An append-only binary logger offering crash resiliency. Operates at $O(1)$ complexity by bypassing filesystem newline formatting using strict binary serialization layouts.
3. **Memtable Cache (`Memtable`)**: A fast, in-memory component backed by a self-balancing Red-Black Tree (`std::map`). Arranges keys in strict alphabetical order to decouple ingestion timeline from physical storage structures.
4. **Sorted String Tables (`SSTable`)**: Immutable block-data files storing keys sequentially on disk. Employs an in-memory **Sparse Index Offset Map** allowing direct hardware `seekg()` jumps to give $O(\log N)$ disk read speeds.

---

## ⚡ Core Features

* **Strict Binary Serialization**: Custom data packing protocol preventing injection attacks and saving disk space over plaintext formats.
* **Point-in-Time Crash Recovery**: Automatic state reconstruction upon system reboots by processing historical byte logs chronologically.
* **Tiered Read Path Lookup**: Read requests resolve via an intelligent hierarchy: Checking high-speed RAM first, and defaulting to a reverse disk array scan only on cache misses.
* **Multi-Way Global Compaction**: A background garbage collector that runs a linear multi-stream merge-sort to remove stale data records, reclaim storage capacity, and truncate active transaction logs to 0 bytes.

---

## 📊 Component Specifications

| Component | Medium | Primary Responsibility | Complexity |
| :--- | :--- | :--- | :--- |
| **`WAL`** | Disk (`wal.log`) | Sequential data persistence and durability | Write: $O(1)$ |
| **`Memtable`** | RAM (`std::map`) | High-speed cache and lexicographical sorting | Write/Read: $O(\log N)$ |
| **`SSTable`** | Disk (`*.db`) | Permanent block-level data persistence | Read: $O(\log N)$ via Index |

---

## 🛠️ Compilation & Execution

This engine requires a standard **C++17** compiler. 

### Local Build Execution
To compile all decoupled storage subsystems and execute the benchmark test harness, run:

```bash
g++ -std=c++17 -I./include src/wal.cpp src/memtable.cpp src/sstable.cpp src/engine.cpp main.cpp -o lsm_engine.exe
.\lsm_engine.exe
Expected Output Behavior
Plaintext
--- Booting Production-Grade LSM Engine ---
[Engine] Initializing decoupled storage subsystems...
[Engine] Recovery complete. Decoupled Memtable ready.

[Write Tier 1] Writing base data blocks...
[Flush] Threshold hit! Writing sstable_1.db to disk...
[Flush] Flash-clear RAM complete.

[Write Tier 2] Updating values...
[Flush] Threshold hit! Writing sstable_2.db to disk...

--- Triggering Global Multi-Way Compaction ---
[Compaction] Dynamic Global Worker Active!
[Compaction] Merging ALL 3 outstanding SSTables...
  (Deleted obsolete file: sstable_1.db)
  (Deleted obsolete file: sstable_2.db)
  (Deleted obsolete file: sstable_3.db)
  (Truncated Write-Ahead Log: 'wal.log' wiped to 0 bytes)
[Compaction] Success! Global compaction consolidated to 'sstable_master.db'.

--- Testing Post-Compaction Clean Reads ---
Query 'key_A' (Should print val_2):   (Hit Disk SSTable) -> val_2