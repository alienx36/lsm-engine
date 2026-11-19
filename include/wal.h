#ifndef WAL_H
#define WAL_H

#include <string>
#include <fstream>
#include <map> 

class WAL {
public:
    // Constructor: Opens the file stream in strict binary append mode
    WAL(const std::string& log_path);
    
    // Destructor: Safely closes the file stream when the object is destroyed
    ~WAL();

    // Appends a single key-value record to our log file
    void append(const std::string& key, const std::string& value);

    // Reads the file sequentially and fills the provided map
    void recover(std::map<std::string, std::string>& memtable);

private:
    std::ofstream write_stream;
    std::string file_path;
};

#endif // WAL_H