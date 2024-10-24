#include <stdlib.h>
#include <iostream>
#include <unordered_map>
#include <fstream>
#include <omp.h>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <chrono>
#include <thread>
#include <cstring>

#define NUM_BLOCKS 8
#define SIZE_BLOCK 16
#define NUM_LINES 2
#define SIZE_LINE 2
#define NUM_CORES 2

// defining global state variables
std::shared_mutex cache_lock;
std::string source_dir = "src/source.txt";

enum cache_status
{
    invalid = -1,
    shared = 0,
    exclusive = 1,
    modified = 2
};

enum inst_type
{
    read = 0,
    write = 1
};

struct decoded_inst
{
    inst_type type; // 0 is RD, 1 is WR
    int address;
    short value; // Only used for WR
};
typedef struct decoded_inst decoded;

struct cache_line
{
    cache_status line_state = invalid;
    int base;
    short *values = new short[SIZE_LINE]; // Use 'new' in C++
};
typedef struct cache_line line;

struct cache_block
{
    line lines[NUM_LINES];
};
typedef struct cache_block CACHE_block;

// declaring functions

decoded decode_inst_line(const char *buffer);

std::string *get_files(int n)
{
    std::ifstream f(source_dir);
    if (!f.is_open())
    {
        std::cerr << "Failed to open source file." << std::endl;
        return nullptr;
    }

    std::string *files = new std::string[n];
    for (int i = 0; i < n; ++i)
    {
        if (!getline(f, files[i]))
        {
            std::cerr << "Failed to read file path." << std::endl;
            break;
        }
    }
    return files;
}

decoded decode_inst_line(const char *buffer)
{
    decoded inst;
    char inst_type[2];
    sscanf(buffer, "%s", inst_type);
    if (!strcmp(inst_type, "RD"))
    {
        inst.type = read;
        int addr = 0;
        sscanf(buffer, "%s %d", inst_type, &addr);
        inst.value = -1;
        inst.address = addr;
    }
    else if (!strcmp(inst_type, "WR"))
    {
        inst.type = write;
        int addr = 0;
        int val = 0;
        sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
        inst.address = addr;
        inst.value = val;
    }
    return inst;
}

class CPU_unit
{
private:
    CACHE_block caches[NUM_CORES];
    short *RAM = new short[SIZE_BLOCK * NUM_BLOCKS]; // Use 'new' in C++

public:
    CPU_unit()
    {
        for (int i = 0; i < SIZE_BLOCK * NUM_BLOCKS; ++i)
        {
            RAM[i] = (short)(rand() % 45);
        }
    }

    ~CPU_unit()
    {
        delete[] RAM; // Free RAM correctly
    }

    void display_all()
    {
        for (int i = 0; i < NUM_CORES; ++i)
        {
            std::cout << "Displaying cache for core index = " << i << std::endl;
            display_cache(caches[i]);
        }
    }

    void display_cache(CACHE_block cache)
    {
        for (int i = 0; i < NUM_LINES; ++i)
        {
            line l = cache.lines[i];
            std::cout << "Line number " << i << "\nBase address " << l.base << " Line state = " << l.line_state << std::endl;
            for (int j = 0; j < SIZE_LINE; ++j)
            {
                std::cout << l.values[j] << "\t";
            }
            std::cout << std::endl;
        }
    }

    void core_run_file(std::string file, int core)
    {
        std::ifstream f(file);
        if (!f.is_open())
        {
            std::cerr << "Failed to open file: " << file << std::endl;
            return;
        }

        while (!f.eof())
        {
            std::string inst_line;
            // instruction fetch
            if (!getline(f, inst_line))
                break;
            const char *buff = inst_line.c_str();
            // instruction decode
            decoded inst = decode_inst_line(buff);
            // instruction execution
            instruction_execute(inst, core);
        }
    }

    void instruction_execute(decoded inst, int core)
    {
        int addr = inst.address;
        int shared_cache = -1;
        short val;
        if (inst.type == read)
        {
            std::shared_lock<std::shared_mutex> lock(cache_lock);
            if (cache_hit(addr, core))
            {
                val = request_read(addr, core, false);
            }
            else if (shared_access(addr, &shared_cache))
            {
                val = request_read(addr, shared_cache, true);
            }
            else
            {
                val = replace_and_read(addr, core);
            }
            std::cout << "Value read on " << addr << " = " << val << " on core = " << core << std::endl;
        }
        else
        {
            std::unique_lock<std::shared_mutex> lock(cache_lock);
            val = inst.value;
            if (cache_hit(addr, core))
            {
                request_write(addr, val, core, true);
            }
            else if (shared_access(addr, &shared_cache))
            {
                invalidate_other_caches(addr);
                request_write(addr, val, shared_cache, true);
            }
            else
            {
                replace_and_write(addr, val, core);
            }
            std::cout << "Value written on " << addr << " = " << val << " on core = " << core << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void invalidate_other_caches(int addr)
    {
        for (int j = 0; j < NUM_CORES; ++j)
        {
            if (cache_hit(addr, j))
            {
                int line_no = (addr / SIZE_LINE) % NUM_LINES;
                caches[j].lines[line_no].line_state = invalid;
            }
        }
    }

    void auto_assign(std::string *paths, int n)
    {
        #pragma omp parallel for num_threads(NUM_CORES)
        for (int i = 0; i < n; ++i)
        {
            std::cout << "Running on core " << i << std::endl;
            core_run_file(paths[i], i);
        }
    }

    short request_read(int addr, int core, bool flag)
    {
        CACHE_block &c = caches[core];
        for (int i = 0; i < NUM_LINES; ++i)
        {
            line &l = c.lines[i];
            if (l.base == (addr - addr % SIZE_LINE))
            {
                l.line_state = flag ? shared : l.line_state;
                return l.values[addr % SIZE_LINE];
            }
        }
        return 0;
    }

    short replace_and_read(int addr, int core)
    {
        int line_no = (addr / SIZE_LINE) % NUM_LINES;
        int new_base = addr - (addr % SIZE_LINE);
        short k = 0;
        if (caches[core].lines[line_no].line_state == modified)
        {
            std::cout << "Replacing modified block, writing back to RAM" << std::endl;
            for (int i = 0; i < SIZE_LINE; ++i)
            {
                RAM[caches[core].lines[line_no].base + i] = caches[core].lines[line_no].values[i];
            }
        }

        caches[core].lines[line_no].line_state = exclusive;
        caches[core].lines[line_no].base = new_base;

        for (int i = 0; i < SIZE_LINE; ++i)
        {
            short val = RAM[new_base + i];
            if (i == addr - new_base)
                k = val;
            caches[core].lines[line_no].values[i] = val;
        }
        return k;
    }

    void replace_and_write(int addr, short val, int core)
    {
        int line_no = (addr / SIZE_LINE) % NUM_LINES;
        int new_base = addr - (addr % SIZE_LINE);
        replace_and_read(addr, core);
        caches[core].lines[line_no].line_state = modified;
        caches[core].lines[line_no].values[addr % SIZE_LINE] = val;
    }

    void request_write(int addr, short val, int core, bool flag)
    {
        CACHE_block &cache = caches[core];
        int line_no = (addr / SIZE_LINE) % NUM_LINES;
        int offset = addr % SIZE_LINE;
        cache.lines[line_no].line_state = flag ? modified : exclusive;
        cache.lines[line_no].values[offset] = val;
    }

    bool shared_access(int addr, int *shared)
    {
        if (NUM_CORES == 1)
            return false;
        for (int i = 0; i < NUM_CORES; ++i)
        {
            if (check_matching_base(caches[i], addr))
            {
                *shared = i;
                return true;
            }
        }
        return false;
    }

    bool cache_hit(int addr, int core)
    {
        return check_matching_base(caches[core], addr);
    }

    bool check_matching_base(CACHE_block &c, int addr)
    {
        for (int i = 0; i < NUM_LINES; ++i)
        {
            if (c.lines[i].base == (addr - addr % SIZE_LINE))
            {
                return true;
            }
        }
        return false;
    }
};

int main(int argc, char const *argv[])
{
    CPU_unit cpu;
    std::string *file_paths = get_files(NUM_CORES);
    if (!file_paths)
    {
        return 1; // Exit if file paths could not be retrieved
    }
    cpu.auto_assign(file_paths, NUM_CORES);
    std::cout << "Completed execution" << std::endl;
    cpu.display_all();
    std::cout << "Finished running" << std::endl;
    delete[] file_paths; // Free dynamically allocated memory
    return 0;
}

