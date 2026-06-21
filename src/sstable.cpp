#include "sstable.h"
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

// 2. Constructor: Opens an existing SSTable and builds its in-memory index
SSTable::SSTable(const std::string& filename) : file_path(filename) {
    load_index();
}

// 3. The Sparse Index Loader: Builds a map of block offsets inside RAM
void SSTable::load_index() {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) return;

    while (true) {
        // Track the exact starting byte position of this record on disk
        std::streampos current_offset = in.tellg();

        uint32_t key_len = 0;
        uint32_t val_len = 0;

        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        if (in.eof()) break; // Clean termination at End-of-File

        in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));

        std::string key(key_len, '\0');
        in.read(&key[0], key_len);

        // Advance the file pointer past the value bytes without wasting RAM loading it
        in.seekg(val_len, std::ios::cur);

        // Sparse Index Optimization: We only index every 2nd or 3rd key to conserve RAM
        // For our mini-engine, we index every key's offset for precise block jumping!
        sparse_index.push_back({key, current_offset});
    }
    in.close();
}

// 4. Fast Disk Lookup: Uses the sparse index to jump directly to the target record
std::string SSTable::get(const std::string& key) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open() || sparse_index.empty()) return "NOT_FOUND";

    // Step A: Scan the in-memory sparse index to find the closest offset matching our key
    std::streampos target_offset = -1;
    for (size_t i = 0; i < sparse_index.size(); ++i) {
        if (sparse_index[i].key == key) {
            target_offset = sparse_index[i].byte_offset;
            break;
        }
    }

    // Index Miss: If our sparse range says the key doesn't live here, exit instantly
    if (target_offset == -1) {
        return "NOT_FOUND";
    }

    // Step B: Direct Hardware Jump! Seek straight to the exact byte offset on disk
    in.seekg(target_offset);

    uint32_t key_len, val_len;
    in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
    in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));

    std::string disk_key(key_len, '\0');
    std::string disk_value(val_len, '\0');

    in.read(&disk_key[0], key_len);
    in.read(&disk_value[0], val_len);

    in.close();
    return disk_value;
}