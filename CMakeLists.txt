cmake_minimum_required(VERSION 3.15)
project(LSMLiteStorageEngine VERSION 1.0 LANGUAGES CXX)

# Enforce strict standard compilation compliance
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Expose internal header directory paths
include_directories(include)

# Gather and group our decoupled engine source files
set(SOURCE_FILES
    src/wal.cpp
    src/memtable.cpp
    src/sstable.cpp
    src/engine.cpp
    main.cpp
)

# Instruct the compiler to output a clean binary executable
add_executable(lsm_engine ${SOURCE_FILES})