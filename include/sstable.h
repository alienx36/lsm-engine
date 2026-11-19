#ifndef SSTABLE_H
#define SSTABLE_H

#include <string>
#include <map>
#include <vector>
#include <ios>

class SSTable {
public:
    // Index every Nth key to keep the in-memory index sparse
    static constexpr size_t SPARSE_INDEX_INTERVAL = 2;

    // Creates an SSTable file from a sorted map (Memtable)
    static void write_to_disk(const std::string& filename, const std::map<std::string, std::string>& memtable);

    // Constructor to open an existing SSTable for reading
    SSTable(const std::string& filename);

    // Searches for a key inside this specific disk file using O(log N) binary search
    std::string get(const std::string& key);

private:
    std::string file_path;

    // Struct to represent our In-Memory Sparse Index
    struct IndexEntry {
        std::string key;
        std::streampos byte_offset;
    };

    std::vector<IndexEntry> sparse_index;

    // Helper to build the sparse index when reading an existing file from disk
    void load_index();
};

#endif // SSTABLE_H