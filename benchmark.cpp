#include "include/engine.h"
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <string>

static size_t file_size_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    return in.is_open() ? static_cast<size_t>(in.tellg()) : 0;
}

static size_t total_storage_bytes() {
    size_t total = 0;
    DIR* dir = opendir(".");
    if (!dir) return 0;
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name(ent->d_name);
        if (name == "wal.log" || name == "sstable_master.db" ||
            (name.rfind("sstable_", 0) == 0 && name.size() > 3 && name.substr(name.size() - 3) == ".db")) {
            total += file_size_bytes(name);
        }
    }
    closedir(dir);
    return total;
}

int main() {
    // Clean old artifacts
    DIR* dir = opendir(".");
    if (dir) {
        struct dirent* ent = nullptr;
        while ((ent = readdir(dir)) != nullptr) {
            std::string name(ent->d_name);
            if (name == "wal.log" || name == "sstable_master.db" ||
                (name.rfind("sstable_", 0) == 0 && name.size() > 3 && name.substr(name.size() - 3) == ".db")) {
                std::remove(name.c_str());
            }
        }
        closedir(dir);
    }

    // Silence std::cout during benchmark so console I/O doesn't skew timings
    std::streambuf* orig_buf = std::cout.rdbuf(nullptr);

    Engine db("wal.log");
    const int OPS = 300; // 100 unique keys, overwritten 2x + 100 deletes

    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < OPS; ++i) {
        std::string k = "key_" + std::to_string(i % 100);
        if (i >= 200 && i % 3 == 0) {
            db.del(k);
        } else {
            db.put(k, "value_payload_" + std::to_string(i));
        }
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    size_t bytes_before = total_storage_bytes();
    auto t3 = std::chrono::high_resolution_clock::now();
    db.compact();
    auto t4 = std::chrono::high_resolution_clock::now();
    size_t bytes_after = total_storage_bytes();

    auto t5 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 1000; ++i) {
        volatile auto res = db.get("key_" + std::to_string(i % 100));
        (void)res;
    }
    auto t6 = std::chrono::high_resolution_clock::now();

    std::cout.rdbuf(orig_buf);

    double write_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    double compact_ms = std::chrono::duration<double, std::milli>(t4 - t3).count();
    double read_ms = std::chrono::duration<double, std::milli>(t6 - t5).count();
    double saved_pct = bytes_before > 0 ? 100.0 * (bytes_before - bytes_after) / bytes_before : 0.0;

    std::cout << "Writes (" << OPS << " ops): " << write_ms << " ms ("
              << static_cast<int>(OPS / (write_ms / 1000.0)) << " ops/sec)\n";
    std::cout << "Storage before compaction: " << bytes_before << " bytes\n";
    std::cout << "Storage after compaction:  " << bytes_after << " bytes ("
              << saved_pct << "% reclaimed)\n";
    std::cout << "Compaction time: " << compact_ms << " ms\n";
    std::cout << "Reads (1000 ops): " << read_ms << " ms ("
              << (read_ms * 1000.0 / 1000.0) << " us/read)\n";
    return 0;
}