# LSM-Lite: Production-Grade LSM-Tree Storage Engine from Scratch

An embedded, key-value storage engine built in C++17 utilizing the **Log-Structured Merge-Tree (LSM-Tree)** architecture. This project implements decoupled architectural layers to enforce data durability, high write throughput, and automatic storage optimization—mirroring the core internals of enterprise systems like RocksDB and Apache Cassandra.

---

## 🏗️ Architectural Overview

The engine separates responsibilities into four distinct, highly decoupled subsystems to adhere to SOLID principles and isolate memory management from physical disk I/O bottlenecks:

```text
                  +-------------------+
                  |    Client API     |
                  |    (put / get)    |
                  +---------+---------+
                            |
                            v
                   +----------------+
                   | Master Engine  |
                   | Orchestrator   |
                   +---+--------+---+
                       |        |
     +-----------------+        +-----------------+
     |                                        |
     v                                        v

+----------------+                 +----------------+
|  Memory Tier   |                 |   Disk Tier    |
|                |                 |                |
| +------------+ |                 | +------------+ |
| |  Memtable  | |                 | |    WAL     | |
| | (RB-Tree)  | |                 | |  (Binary)  | |
| +------------+ |                 | +------------+ |
+----------------+                 +--------+-------+
                                           |
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

Serves as a unified facade protecting underlying components. Coordinates state playback transitions, routes writes concurrently to memory and disk logs, and schedules compaction operations.

#### 2. Write-Ahead Log (`WAL`)

An append-only binary logger offering crash resiliency. Operates at **O(1)** complexity by bypassing filesystem newline formatting using strict binary serialization layouts.

#### 3. Memtable Cache (`Memtable`)

A fast, in-memory component backed by a self-balancing Red-Black Tree (`std::map`). Arranges keys in strict alphabetical order to decouple ingestion timeline from physical storage structures.

#### 4. Sorted String Tables (`SSTable`)

Immutable block-data files storing keys sequentially on disk. Employs an in-memory **Sparse Index Offset Map** allowing direct hardware `seekg()` jumps to provide **O(log N)** disk read speeds.

---

## ⚡ Core Features

* **Strict Binary Serialization** – Custom data packing protocol preventing injection attacks and reducing storage overhead compared to plaintext formats.
* **Point-in-Time Crash Recovery** – Automatic state reconstruction upon system reboots by processing historical byte logs chronologically.
* **Tiered Read Path Lookup** – Read requests resolve through an intelligent hierarchy by checking high-speed RAM first and defaulting to reverse SSTable scans only on cache misses.
* **Multi-Way Global Compaction** – Background garbage collection through linear multi-stream merge-sort operations to eliminate stale records, reclaim disk space, and truncate obsolete transaction logs.

## 🔄 Multi-Way Global Compaction Engine

One of the defining characteristics of an LSM-Tree is that data is never updated in-place. Instead, updates are continuously appended to the Write-Ahead Log (WAL), accumulated in the Memtable, and periodically flushed into immutable SSTable files.

Over time, multiple SSTables may contain historical versions of the same key:

```text
sstable_1.db
user_id -> 1001
status  -> inactive

sstable_2.db
user_id -> 9999
status  -> active
```

Searching through an ever-growing collection of SSTables would eventually degrade read performance and waste disk space with obsolete records.

To solve this problem, LSM-Lite implements a **Multi-Way Global Compaction Engine**.

### Compaction Workflow

The compaction worker performs the following steps:

1. Discovers all active SSTable files.
2. Opens each SSTable as an ordered input stream.
3. Performs a linear multi-stream merge-sort.
4. Resolves duplicate keys by retaining only the newest version.
5. Discards stale historical values and deleted records.
6. Writes the consolidated result into a new master SSTable.
7. Deletes obsolete SSTable files.
8. Truncates the Write-Ahead Log once all live data has been safely persisted.

### Example

Before Compaction:

```text
sstable_1.db
key_A -> val_1
key_B -> val_1

sstable_2.db
key_A -> val_2
key_C -> val_1

sstable_3.db
key_D -> val_1
```

After Compaction:

```text
sstable_master.db
key_A -> val_2
key_B -> val_1
key_C -> val_1
key_D -> val_1
```

### Benefits

* Eliminates obsolete key versions.
* Reclaims storage space consumed by stale records.
* Reduces the number of SSTables participating in lookups.
* Improves read performance by minimizing disk traversals.
* Enables safe WAL truncation after durable persistence.
* Maintains predictable long-term storage growth.

### Complexity Characteristics

| Operation                | Complexity       |
| ------------------------ | ---------------- |
| SSTable Discovery        | O(N)             |
| Multi-Way Merge          | O(Total Records) |
| Duplicate Resolution     | O(1) per record  |
| Final SSTable Generation | O(Total Records) |

Where **N** is the number of SSTables participating in the compaction cycle.

The compaction engine serves as the storage optimization layer of the system, continuously transforming fragmented immutable SSTables into a compact, query-efficient representation while preserving the durability guarantees established by the WAL.


---

## 📊 Component Specifications

| Component    | Medium           | Primary Responsibility                       | Complexity                          |
| ------------ | ---------------- | -------------------------------------------- | ----------------------------------- |
| **WAL**      | Disk (`wal.log`) | Sequential data persistence and durability   | Write: **O(1)**                     |
| **Memtable** | RAM (`std::map`) | High-speed cache and lexicographical sorting | Read/Write: **O(log N)**            |
| **SSTable**  | Disk (`*.db`)    | Permanent block-level persistence            | Read: **O(log N)** via Sparse Index |

---

## 🛠️ Compilation & Execution

This engine requires a standard **C++17** compiler.

### Local Build Execution

```bash
g++ -std=c++17 -I./include \
src/wal.cpp \
src/memtable.cpp \
src/sstable.cpp \
src/engine.cpp \
main.cpp \
-o lsm_engine.exe
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

[Compaction] Success!
Global compaction consolidated to 'sstable_master.db'.

--- Testing Post-Compaction Clean Reads ---

Query 'key_A' (Should print val_2):
(Hit Disk SSTable) -> val_2
```

---

## 🎯 Design Goals

* High write throughput through append-only logging
* Crash-safe durability guarantees
* Efficient point lookups
* Immutable on-disk storage structures
* Predictable compaction behavior
* Clear separation of concerns between memory and disk subsystems
* Educational implementation of modern LSM-tree internals

---

