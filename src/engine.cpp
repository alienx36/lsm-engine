#include "engine.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <stdexcept>
#include <utility>

namespace {
bool read_record(std::ifstream& in, std::string& key, std::string& value) {
    uint32_t key_len = 0;
    uint32_t val_len = 0;
    in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
    if (in.eof() || !in.good()) return false;
    in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
    if (!in.good()) return false;

    key.assign(key_len, '\0');
    value.assign(val_len, '\0');
    in.read(&key[0], key_len);
    in.read(&value[0], val_len);
    return in.good() || !in.fail();
}

struct MergeItem {
    std::string key;
    std::string value;
    size_t file_idx; // Higher index = newer SSTable
};

struct MergeItemGreater {
    bool operator()(const MergeItem& a, const MergeItem& b) const {
        if (a.key != b.key) return a.key > b.key; // Min-heap by key
        return a.file_idx < b.file_idx;           // Max-heap by recency on ties
    }
};
} // namespace

Engine::Engine(const std::string& log_path) : wal(log_path), wal_path(log_path) {
    std::cout << "[Engine] Initializing decoupled storage subsystems..." << std::endl;

    // 1. Reload existing SSTables from disk in chronological order (oldest -> newest)
    load_existing_sstables();

    // 2. Replay WAL for any unflushed writes
    std::map<std::string, std::string> temp_map;
    wal.recover(temp_map);

    for (const auto& entry : temp_map) {
        memtable.put(entry.first, entry.second);
    }
    std::cout << "[Engine] Recovery complete. Loaded " << sstable_files.size()
              << " SSTable(s); Memtable has " << memtable.size() << " key(s)." << std::endl;
}

void Engine::load_existing_sstables() {
    bool has_master = false;
    std::vector<std::pair<size_t, std::string>> numbered_tables;

    DIR* dir = opendir(".");
    if (!dir) return;

    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name(ent->d_name);
        if (name == "sstable_master.db") {
            has_master = true;
        } else if (name.rfind("sstable_", 0) == 0 && name.size() > 11 &&
                   name.substr(name.size() - 3) == ".db") {
            std::string num_str = name.substr(8, name.size() - 11);
            try {
                size_t id = std::stoull(num_str);
                numbered_tables.emplace_back(id, name);
                if (id >= next_sstable_id) {
                    next_sstable_id = id + 1;
                }
            } catch (...) {
                // Ignore non-numeric sstable_*.db files
            }
        }
    }
    closedir(dir);

    std::sort(numbered_tables.begin(), numbered_tables.end(),
              [](const std::pair<size_t, std::string>& a, const std::pair<size_t, std::string>& b) {
                  return a.first < b.first;
              });

    if (has_master) {
        sstable_files.push_back("sstable_master.db");
        sstables.push_back(std::make_unique<SSTable>("sstable_master.db"));
    }
    for (const auto& item : numbered_tables) {
        sstable_files.push_back(item.second);
        sstables.push_back(std::make_unique<SSTable>(item.second));
    }
}

void Engine::put(const std::string& key, const std::string& value) {
    wal.append(key, value);
    memtable.put(key, value);

    if (memtable.size() >= MAX_MEMTABLE_KEYS) {
        flush_memtable();
    }
}

void Engine::del(const std::string& key) {
    wal.append(key, TOMBSTONE);
    memtable.del(key);

    if (memtable.size() >= MAX_MEMTABLE_KEYS) {
        flush_memtable();
    }
}

std::string Engine::get(const std::string& key) {
    // 1. Search fast decoupled RAM Memtable
    std::string mem_res = memtable.get(key);
    if (mem_res == TOMBSTONE) {
        return "NOT_FOUND";
    }
    if (mem_res != "NOT_FOUND") {
        std::cout << "  (Hit RAM Memtable)" << std::endl;
        return mem_res;
    }

    // 2. Search Immutable SSTables on Disk (newest to oldest)
    for (auto sstable_it = sstables.rbegin(); sstable_it != sstables.rend(); ++sstable_it) {
        std::string res = (*sstable_it)->get(key);
        if (res == TOMBSTONE) {
            return "NOT_FOUND";
        }
        if (res != "NOT_FOUND") {
            std::cout << "  (Hit Disk SSTable)" << std::endl;
            return res;
        }
    }
    return "NOT_FOUND";
}

void Engine::flush_memtable() {
    if (memtable.size() == 0) return;

    std::string filename = "sstable_" + std::to_string(next_sstable_id++) + ".db";
    std::cout << "\n[Flush] Threshold hit! Writing " << filename << " to disk..." << std::endl;

    SSTable::write_to_disk(filename, memtable.get_all_data());

    sstable_files.push_back(filename);
    sstables.push_back(std::make_unique<SSTable>(filename));

    memtable.clear();
    std::cout << "[Flush] Flash-clear RAM complete.\n" << std::endl;
}

void Engine::compact() {
    // Ensure any unflushed Memtable keys are persisted before truncating WAL
    if (memtable.size() > 0) {
        flush_memtable();
    }

    if (sstable_files.empty()) {
        std::cout << "[Compaction] Zero SSTables found. Skipping." << std::endl;
        return;
    }

    std::cout << "\n[Compaction] Dynamic Global Worker Active!" << std::endl;
    std::cout << "[Compaction] Streaming K-way merge of " << sstable_files.size()
              << " SSTable(s)..." << std::endl;

    // Step 1: Open all active SSTables as ordered input streams
    std::vector<std::ifstream> streams(sstable_files.size());
    std::priority_queue<MergeItem, std::vector<MergeItem>, MergeItemGreater> min_heap;

    for (size_t i = 0; i < sstable_files.size(); ++i) {
        streams[i].open(sstable_files[i], std::ios::binary);
        if (!streams[i].is_open()) continue;
        std::string k, v;
        if (read_record(streams[i], k, v)) {
            min_heap.push({std::move(k), std::move(v), i});
        }
    }

    // Step 2: Stream merged, deduplicated records into a temporary master file
    std::string temp_master = "sstable_master.db.tmp";
    std::ofstream out(temp_master, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to create temporary master SSTable: " + temp_master);
    }

    std::string last_written_key;
    bool has_last_key = false;

    while (!min_heap.empty()) {
        MergeItem top = min_heap.top();
        min_heap.pop();

        // Advance the stream that produced `top`
        std::string next_k, next_v;
        if (read_record(streams[top.file_idx], next_k, next_v)) {
            min_heap.push({std::move(next_k), std::move(next_v), top.file_idx});
        }

        // Because ties pop highest `file_idx` first, the first occurrence of a key is the newest
        if (has_last_key && top.key == last_written_key) {
            continue;
        }
        last_written_key = top.key;
        has_last_key = true;

        // Physically discard deleted records (tombstones)
        if (top.value == TOMBSTONE) {
            continue;
        }

        uint32_t key_len = static_cast<uint32_t>(top.key.size());
        uint32_t val_len = static_cast<uint32_t>(top.value.size());
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        out.write(top.key.data(), key_len);
        out.write(top.value.data(), val_len);
    }

    out.close();
    for (auto& s : streams) {
        if (s.is_open()) s.close();
    }

    // Step 3: Unload open SSTable objects, purge old files, and promote temp master
    sstables.clear();
    for (const auto& file : sstable_files) {
        std::remove(file.c_str());
        std::cout << "  (Deleted obsolete file: " << file << ")" << std::endl;
    }
    sstable_files.clear();

    std::string master_file = "sstable_master.db";
    std::remove(master_file.c_str()); // Windows rename fails if destination exists
    if (std::rename(temp_master.c_str(), master_file.c_str()) != 0) {
        throw std::runtime_error("Failed to promote " + temp_master + " to " + master_file);
    }
    next_sstable_id = 1;

    // Step 4: Truncate the WAL now that all live data is persisted in sstable_master.db
    std::ofstream truncate_wal(wal_path, std::ios::trunc | std::ios::binary);
    truncate_wal.close();
    std::cout << "  (Truncated Write-Ahead Log: '" << wal_path << "' wiped to 0 bytes)" << std::endl;

    // Step 5: Re-register the master table
    sstable_files.push_back(master_file);
    sstables.push_back(std::make_unique<SSTable>(master_file));

    std::cout << "[Compaction] Success! Global compaction consolidated to 'sstable_master.db'.\n" << std::endl;
}