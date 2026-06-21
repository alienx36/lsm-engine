#include "wal.h"
#include <cstdint>   
#include <stdexcept>

WAL::WAL(const std::string& log_path) : file_path(log_path) {
    // std::ios::app = Append mode (always write to the end of the file)
    // std::ios::binary = Binary mode (stops the OS from altering newline characters)
    write_stream.open(log_path, std::ios::app | std::ios::binary);
    
    if (!write_stream.is_open()) {
        throw std::runtime_error("Failed to initialize WAL file at: " + log_path);
    }
}

WAL::~WAL() {
    if (write_stream.is_open()) {
        write_stream.close();
    }
}

void WAL::append(const std::string& key, const std::string& value) {
    // Convert lengths to fixed-size 32-bit unsigned integers (4 bytes each)
    uint32_t key_len = static_cast<uint32_t>(key.size());
    uint32_t val_len = static_cast<uint32_t>(value.size());

    // 1. Write the 4-byte Key Length header
    write_stream.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
    
    // 2. Write the 4-byte Value Length header
    write_stream.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));

    // 3. Write the actual Key string bytes
    write_stream.write(key.data(), key_len);
    
    // 4. Write the actual Value string bytes
    write_stream.write(value.data(), val_len);

    // Flush forces the data out of the C++ program buffer into the Operating System
    write_stream.flush();
}

void WAL::recover(std::map<std::string, std::string>& memtable) {
    // Open an input file stream in binary mode to read the bytes
    std::ifstream read_stream(file_path, std::ios::binary);
    
    // If the file doesn't exist yet (first time running), just exit silently
    if (!read_stream.is_open()) {
        return;
    }

    // Loop until we read the entire file
    while (true) {
        uint32_t key_len = 0;
        uint32_t val_len = 0;

        // 1. Try to read the 4-byte Key Length
        read_stream.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        
        // If we hit the End of the File naturally, break out of the loop
        if (read_stream.eof()) {
            break; 
        }

        // 2. Read the 4-byte Value Length
        read_stream.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));

        // 3. Allocate empty string buffers of exact sizes
        std::string key(key_len, '\0');
        std::string value(val_len, '\0');

        // 4. Read the raw characters into our strings
        read_stream.read(&key[0], key_len);
        read_stream.read(&value[0], val_len);

        // 5. Store them inside our in-memory map
        memtable[key] = value;
    }

    read_stream.close();
}