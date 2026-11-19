#include "include/engine.h"
#include <cassert>
#include <filesystem>
#include <iostream>

int main() {
    try {
        // Clean previous run artifacts for a deterministic demo
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::current_path())) {
            std::string name = entry.path().filename().string();
            if (name == "wal.log" || name == "sstable_master.db" ||
                (name.rfind("sstable_", 0) == 0 && name.size() > 3 && name.substr(name.size() - 3) == ".db")) {
                std::filesystem::remove(entry.path());
            }
        }

        std::cout << "--- Booting Production-Grade LSM Engine ---" << std::endl;
        {
            Engine db("wal.log");

            std::cout << "\n[Write Tier 1] Writing base data blocks..." << std::endl;
            db.put("key_A", "val_1");
            db.put("key_B", "val_1");
            db.put("key_C", "val_1"); // Triggers sstable_1.db

            std::cout << "\n[Write Tier 2] Updating & deleting values..." << std::endl;
            db.put("key_A", "val_2"); // Update
            db.del("key_C");          // Delete tombstone
            db.put("key_E", "val_1"); // Triggers sstable_2.db

            std::cout << "\n[Write Tier 3] Unflushed writes before simulated reboot..." << std::endl;
            db.put("key_B", "val_3"); // Stays in WAL + Memtable
            db.put("key_F", "val_1");
        }

        std::cout << "\n--- Simulating Reboot & Crash Recovery ---" << std::endl;
        {
            Engine db("wal.log");

            assert(db.get("key_A") == "val_2");
            assert(db.get("key_B") == "val_3");
            assert(db.get("key_C") == "NOT_FOUND"); // Tombstone must shadow sstable_1.db
            assert(db.get("key_E") == "val_1");
            assert(db.get("key_F") == "val_1");

            std::cout << "\n--- Triggering Streaming K-Way Global Compaction ---" << std::endl;
            db.compact();

            std::cout << "--- Testing Post-Compaction Clean Reads ---" << std::endl;
            std::cout << "Query 'key_A' (Expect val_2): " << db.get("key_A") << std::endl;
            std::cout << "Query 'key_B' (Expect val_3): " << db.get("key_B") << std::endl;
            std::cout << "Query 'key_C' (Expect NOT_FOUND): " << db.get("key_C") << std::endl;
            std::cout << "Query 'key_E' (Expect val_1): " << db.get("key_E") << std::endl;
            std::cout << "Query 'key_F' (Expect val_1): " << db.get("key_F") << std::endl;

            assert(db.get("key_A") == "val_2");
            assert(db.get("key_B") == "val_3");
            assert(db.get("key_C") == "NOT_FOUND");
            assert(db.get("key_E") == "val_1");
            assert(db.get("key_F") == "val_1");
        }

        std::cout << "\nAll assertions passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Engine Crash: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}