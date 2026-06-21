#include "include/engine.h"
#include <iostream>

int main() {
    try {
        std::cout << "--- Booting Production-Grade LSM Engine ---" << std::endl;
        Engine db("wal.log");

        std::cout << "\n[Write Tier 1] Writing base data blocks..." << std::endl;
        db.put("key_A", "val_1");
        db.put("key_B", "val_1");
        db.put("key_C", "val_1"); // Triggers sstable_1.db

        std::cout << "\n[Write Tier 2] Updating values..." << std::endl;
        db.put("key_A", "val_2"); // Update
        db.put("key_D", "val_1");
        db.put("key_E", "val_1"); // Triggers sstable_2.db

        std::cout << "\n[Write Tier 3] Final touches..." << std::endl;
        db.put("key_B", "val_3"); // Update
        db.put("key_F", "val_1");
        db.put("key_G", "val_1"); // Triggers sstable_3.db

        std::cout << "\n--- Triggering Global Multi-Way Compaction ---" << std::endl;
        db.compact();

        std::cout << "--- Testing Post-Compaction Clean Reads ---" << std::endl;
        std::cout << "Query 'key_A' (Should print val_2): " << db.get("key_A") << std::endl;
        std::cout << "Query 'key_B' (Should print val_3): " << db.get("key_B") << std::endl;
        std::cout << "Query 'key_C' (Should print val_1): " << db.get("key_C") << std::endl;
        std::cout << "--------------------------------------------" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Engine Crash: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}