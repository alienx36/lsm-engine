#include "sstable.h"
#include <algorithm>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <stdexcept>

// 1. Static helper function to flush Memtable data down to a raw disk file
void SSTable::write_to_disk(const std::string& filename, const std::map<std::string, std::string>& memtable) {
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to create SSTable file: " + filename);
    }

    for (const auto& [key, value] : memtable) {
        uint32_t key_len = static_cast<uint32_t>(key.size());
        uint32_t val_len = static_cast<uint32_t>(value.size());

        // Write the structured binary record layout (identical to our WAL protocol)
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        out.write(key.data(), key_len);
        out.write(value.data(), val_len);
    }
    out.close();
}

// 2. Constructor: Opens an existing SSTable and builds its sparse in-memory index
SSTable::SSTable(const std::string& filename) : file_path(filename) {
    load_index();
}

// 3. The Sparse Index Loader: Indexes every SPARSE_INDEX_INTERVAL-th key in RAM
void SSTable::load_index() {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) return;

    size_t record_idx = 0;
    while (true) {
        std::streampos current_offset = in.tellg();

        uint32_t key_len = 0;
        uint32_t val_len = 0;

        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        if (in.eof()) break;

        in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));

        std::string key(key_len, '\0');
        in.read(&key[0], key_len);

        // Advance past the value bytes without loading them into RAM
        in.seekg(val_len, std::ios::cur);

        if (record_idx % SPARSE_INDEX_INTERVAL == 0) {
            sparse_index.push_back({key, current_offset});
        }
        ++record_idx;
    }
    in.close();
}

// 4. Fast Disk Lookup: O(log N) binary search on sparse_index, then bounded block scan
std::string SSTable::get(const std::string& key) {
    if (sparse_index.empty()) return "NOT_FOUND";

    // Find the first index entry strictly greater than `key`
    auto upper = std::upper_bound(
        sparse_index.begin(),
        sparse_index.end(),
        key,
        [](const std::string& target, const IndexEntry& entry) {
            return target < entry.key;
        }
    );

    // Key is smaller than the very first key in this SSTable
    if (upper == sparse_index.begin()) {
        return "NOT_FOUND";
    }

    // Step back one entry to get the closest offset <= key
    --upper;
    std::streampos start_offset = upper->byte_offset;

    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) return "NOT_FOUND";

    in.seekg(start_offset);

    while (true) {
        uint32_t key_len = 0;
        uint32_t val_len = 0;
        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        if (in.eof()) break;
        in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));

        std::string disk_key(key_len, '\0');
        in.read(&disk_key[0], key_len);

        if (disk_key == key) {
            std::string disk_value(val_len, '\0');
            in.read(&disk_value[0], val_len);
            in.close();
            return disk_value;
        }

        // Keys on disk are sorted; if we passed `key`, it's not in this SSTable
        if (disk_key > key) {
            break;
        }

        // Skip value bytes for intermediate keys in the sparse block
        in.seekg(val_len, std::ios::cur);
    }

    in.close();
    return "NOT_FOUND";
}