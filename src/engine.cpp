#include "engine.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <map>
#include <cstdint>

Engine::Engine(const std::string& log_path) : wal(log_path), wal_path(log_path) {
    std::cout << "[Engine] Initializing decoupled storage subsystems..." << std::endl;
    
    // Temporary map wrapper to pass into WAL recovery protocol
    std::map<std::string, std::string> temp_map;
    wal.recover(temp_map);
    
    for (const auto& [k, v] : temp_map) {
        memtable.put(k, v);
    }
    std::cout << "[Engine] Recovery complete. Decoupled Memtable ready." << std::endl;
}

void Engine::put(const std::string& key, const std::string& value) {
    wal.append(key, value);
    memtable.put(key, value);

    if (memtable.size() >= MAX_MEMTABLE_KEYS) {
        flush_memtable();
    }
}

std::string Engine::get(const std::string& key) {
    // 1. Search fast decoupled RAM Memtable
    std::string mem_res = memtable.get(key);
    if (mem_res != "NOT_FOUND") {
        std::cout << "  (Hit RAM Memtable)" << std::endl;
        return mem_res;
    }

    // 2. Search Immutable SSTables on Disk (Iterate newest files to oldest files)
    for (auto sstable_it = sstables.rbegin(); sstable_it != sstables.rend(); ++sstable_it) {
        std::string res = (*sstable_it)->get(key);
        if (res != "NOT_FOUND") {
            std::cout << "  (Hit Disk SSTable)" << std::endl;
            return res;
        }
    }
    return "NOT_FOUND";
}

void Engine::flush_memtable() {
    std::string filename = "sstable_" + std::to_string(sstable_files.size() + 1) + ".db";
    std::cout << "\n[Flush] Threshold hit! Writing " << filename << " to disk..." << std::endl;

    // Flush the underlying internal map structure out to the file
    SSTable::write_to_disk(filename, memtable.get_all_data());
    
    // Track file names dynamically for multi-way deletion later
    sstable_files.push_back(filename);
    sstables.push_back(std::make_unique<SSTable>(filename));

    memtable.clear();
    std::cout << "[Flush] Flash-clear RAM complete.\n" << std::endl;
}

void Engine::compact() {
    if (sstable_files.empty()) {
        std::cout << "[Compaction] Zero SSTables found. Skipping." << std::endl;
        return;
    }

    std::cout << "\n[Compaction] Dynamic Global Worker Active!" << std::endl;
    std::cout << "[Compaction] Merging ALL " << sstable_files.size() << " outstanding SSTables..." << std::endl;

    std::map<std::string, std::string> master_merge_map;

    // Step 1: Scan every tracked file sequentially from oldest to newest
    for (const auto& file : sstable_files) {
        std::ifstream in(file, std::ios::binary);
        while (in.good()) {
            uint32_t key_len = 0, val_len = 0;
            in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (in.eof()) break;
            in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));

            std::string k(key_len, '\0');
            std::string v(val_len, '\0');
            in.read(&k[0], key_len);
            in.read(&v[0], val_len);

            // Timeline rule: Chronological overwriting removes stale entries cleanly
            master_merge_map[k] = v;
        }
        in.close();
    }

    // Step 2: Unload open memory descriptors so the filesystem unlocks the raw files
    sstables.clear();

    // Step 3: Stream the combined deduplicated data into a single master file
    std::string master_file = "sstable_master.db";
    SSTable::write_to_disk(master_file, master_merge_map);

    // Step 4: Physically purge ALL old intermediate SSTable files from the drive
    for (const auto& file : sstable_files) {
        std::remove(file.c_str());
        std::cout << "  (Deleted obsolete file: " << file << ")" << std::endl;
    }
    sstable_files.clear();

    // Step 5: Truncate (wipe) the old WAL log file completely
    std::ofstream truncate_wal(wal_path, std::ios::trunc | std::ios::binary);
    truncate_wal.close();
    std::cout << "  (Truncated Write-Ahead Log: '" << wal_path << "' wiped to 0 bytes)" << std::endl;

    // Step 6: Re-register the master table as our single point of truth on disk
    sstable_files.push_back(master_file);
    sstables.push_back(std::make_unique<SSTable>(master_file));

    std::cout << "[Compaction] Success! Global compaction consolidated to 'sstable_master.db'.\n" << std::endl;
}