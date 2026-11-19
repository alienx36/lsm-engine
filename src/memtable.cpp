#include "memtable.h"

const std::string TOMBSTONE = std::string("\0__LSM_TOMBSTONE__\0", 20);

void Memtable::put(const std::string& key, const std::string& value) {
    table[key] = value;
}

void Memtable::del(const std::string& key) {
    table[key] = TOMBSTONE;
}

std::string Memtable::get(const std::string& key) const {
    auto it = table.find(key);
    if (it != table.end()) {
        return it->second;
    }
    return "NOT_FOUND";
}

size_t Memtable::size() const {
    return table.size();
}

void Memtable::clear() {
    table.clear();
}

const std::map<std::string, std::string>& Memtable::get_all_data() const {
    return table;
}