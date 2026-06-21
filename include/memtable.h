#ifndef MEMTABLE_H
#define MEMTABLE_H

#include <string>
#include <map>

class Memtable {
public:
    Memtable() = default;
    ~Memtable() = default;

    // Core Mutators & Accessors
    void put(const std::string& key, const std::string& value);
    std::string get(const std::string& key) const;
    
    // Management Helpers
    size_t size() const;
    void clear();
    const std::map<std::string, std::string>& get_all_data() const;

private:
    std::map<std::string, std::string> table; // The actual Red-Black Tree cache
};

#endif // MEMTABLE_H