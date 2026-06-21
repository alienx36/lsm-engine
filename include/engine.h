#ifndef ENGINE_H
#define ENGINE_H

#include "wal.h"
#include "memtable.h"
#include "sstable.h"
#include <string>
#include <vector>
#include <memory>

class Engine {
public:
    Engine(const std::string& log_path);
    ~Engine() = default;

    void put(const std::string& key, const std::string& value);
    std::string get(const std::string& key);
    
    // Scans, merges ALL uncompacted SSTables, and purges the WAL
    void compact();

private:
    Memtable memtable; // Cleanly isolated RAM layer
    WAL wal;           // Durability layer
    std::string wal_path;
    
    // Dynamic tracking of all active SSTable files on disk
    std::vector<std::string> sstable_files;
    std::vector<std::unique_ptr<SSTable>> sstables;
    
    const size_t MAX_MEMTABLE_KEYS = 3; // Strict flush limit
    
    void flush_memtable();
};

#endif // ENGINE_H