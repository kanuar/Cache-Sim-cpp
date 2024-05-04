#include <stdlib.h>
#include <iostream>
#include <unordered_map>
#include <bits/stdc++.h>
#include <omp.h>
#include <mutex>
#include <shared_mutex>

#define NUM_BLOCKS 8
#define SIZE_BLOCK 16
#define NUM_LINES 1
#define SIZE_LINE 2
#define NUM_CORES 4

// defining global state variables
std::shared_mutex cache_lock;
std::string source_dir="source.txt";

enum cache_status
{
    invalid=-1,
    shared=0,
    exclusive=1,
    modified=2
};

enum inst_type
{
    read=0,
    write=1,
    addition=2,
    subtraction=3
};

enum core_status
{
    available=0,
    busy=1
};

struct decoded_inst 
{
    inst_type type; // 0 is RD, 1 is WR, 2 is ADD, 3 is SUB
    int source1=0,source2=0; // common to add and sub
    int address=0; // common to both read and write
    short value=0; // Only used for WR 
};
typedef struct decoded_inst decoded;

struct cache_line
{
    cache_status line_state=invalid;
    int base;
    short *values=(short*)malloc(SIZE_LINE*sizeof(short));
};
typedef struct cache_line line;

struct cache_block
{
    line lines[NUM_LINES];
};
typedef struct cache_block CACHE_block;

// declaring functions

decoded decode_inst_line(const char * buffer);

std::string* get_files(int n)
{
    std::ifstream f(source_dir);
    std::string *files=new std::string[n];
    for (int i = 0; i < n; ++i)
    {
        getline(f,files[i]);
    }
    return files;
} 

decoded decode_inst_line(const char * buffer)
{
    decoded inst;
    char inst_type[2];
    sscanf(buffer, "%s", inst_type);
    if(!strcmp(inst_type, "RD")){
        inst.type = read;
        int addr = 0;
        sscanf(buffer, "%s %d", inst_type, &addr);
        inst.value = -1;
        inst.address = addr;
    } 
    else if(!strcmp(inst_type, "WR"))
    {
        inst.type = write;
        int addr = 0;
        int val = 0;
        sscanf(buffer, "%s %d %d", inst_type, &addr, &val);
        inst.address = addr;
        inst.value = (val);
    } 
    else if (!strcmp(inst_type, "ADD"))
    {
        inst.type = addition;
        int addr = 0;
        int val = 0;
        int d=0, s1=0, s2=0;
        sscanf(buffer, "%s %d %d %d", inst_type, &d, &s1, &s2);
        inst.address = addr;
        inst.value = (val);
        inst.source1=s1;
        inst.source2=s2;
        inst.address=d;
    }
    else if (!strcmp(inst_type, "SUB"))
    {
        inst.type = subtraction;
        int addr = 0;
        int val = 0;
        int d=0, s1=0, s2=0;
        sscanf(buffer, "%s %d %d %d", inst_type, &d, &s1, &s2);
        inst.address = addr;
        inst.value = (val);
        inst.source1=s1;
        inst.source2=s2;
        inst.address=d;
    }
    return inst;
}

class CPU_unit
{
private:
    CACHE_block caches[NUM_CORES];
    short *RAM=(short* )malloc(SIZE_BLOCK*NUM_BLOCKS*sizeof(short));
public:
    CPU_unit()
    {
        for (int i = 0; i < SIZE_BLOCK*NUM_BLOCKS; ++i)
        {
            *(RAM+i)=(short)(rand()%45);
        }
    }
    
    ~CPU_unit()
    {
        free(RAM);
    }

    void display_all()
    {
        for (int i = 0; i < NUM_CORES; ++i)
        {
            std::cout<<"displaying cache for core index = "<<i<<std::endl;
            display_cache(caches[i]);
        }
    }

    void display_cache(CACHE_block cache)
    {
        for (int i = 0; i < NUM_LINES; ++i)
        {
            line l=cache.lines[i];
            std::cout<<"line number "<<i<<"\nbase address "<<l.base<<" Line state = "<<l.line_state<<std::endl;
            for (int i = 0; i < SIZE_LINE; ++i)
            {
                std::cout<<*(l.values+i)<<"\t";
            }
            std::cout<<std::endl;
        }
    }

    void core_run_file(std::string file,int core)
    {    
        std::ifstream f(file);
        while(!f.eof())
        {
            std::string inst_line;
            // instruction fetch
            getline(f,inst_line);
            const char *buff=inst_line.c_str();
            // instruction decode
            decoded inst = decode_inst_line(buff);
            // instruction execution
            instruction_execute(inst,core);
        }
    }

    void instruction_execute(decoded inst,int core)
    {
        int addr=inst.address;
        int shared_cache=-1;
        short val;
        if (inst.type==read)
        {
            std::shared_lock<std::shared_mutex> lock(cache_lock);
            // std::cout<<"locked by core = "<<core<<std::endl;
            if (cache_hit(addr,core))
            {
                val=request_read(addr,core,false);
            }
            else if(shared_access(addr,&shared_cache))
            {
                val=request_read(addr,shared_cache,true);
            }
            else
            {
                // std::cout<<"cache is exclusive"<<std::endl;
                val=replace_and_read(addr,core);
            }
            std::cout<<"value read on "<<addr<<" = "<<val<<" on core = "<<core<<std::endl;
        }
        else if(inst.type==write)
        {
            std::unique_lock<std::shared_mutex> lock(cache_lock);
            // std::cout<<"locked by core = "<<core<<std::endl;
            val=inst.value;
            if(cache_hit(addr,core))
            {
                request_write(addr,val,core,true);
            }
            else if(shared_access(addr,&shared_cache))
            {
                invalidate_other_caches(addr,core);
                request_write(addr,val,shared_cache,true);
            }
            else
            {
                replace_and_write(addr,val,core);
            }
            std::cout<<"value written on "<<addr<<" = "<<val<<" on core = "<<core<<std::endl;
        }
        else if(inst.type==addition)
        {
            std::unique_lock<std::shared_mutex> lock(cache_lock);
            int add_s1=inst.source1;
            int add_s2=inst.source2;
            int add_dest=inst.address;
            short s1,s2;
            if (cache_hit(add_s1,core))
            {
                s1=request_read(add_s1,core,false);
            }
            else if(shared_access(add_s1,&shared_cache))
            {
                s1=request_read(add_s1,shared_cache,true);
            }
            else
            {
                // std::cout<<"cache is exclusive"<<std::endl;
                s1=replace_and_read(addr,core);
            }

            if (cache_hit(add_s2,core))
            {
                s2=request_read(add_s2,core,false);
            }
            else if(shared_access(add_s2,&shared_cache))
            {
                s2=request_read(add_s2,shared_cache,true);
            }
            else
            {
                // std::cout<<"cache is exclusive"<<std::endl;
                s2=replace_and_read(addr,core);
            }

            short val=s1+s2;
            if(cache_hit(add_dest,core))
            {
                request_write(add_dest,val,core,true);
            }
            else if(shared_access(add_dest,&shared_cache))
            {
                invalidate_other_caches(add_dest,core);
                request_write(add_dest,val,shared_cache,true);
            }
            else
            {
                replace_and_write(add_dest,val,core);
            }
            std::cout<<"value written after addition on "<<addr<<" = "<<val<<" on core = "<<core<<std::endl;
        }

        else if(inst.type==subtraction)
        {
            std::unique_lock<std::shared_mutex> lock(cache_lock);
            int add_s1=inst.source1;
            int add_s2=inst.source2;
            int add_dest=inst.address;
            short s1,s2;
            if (cache_hit(add_s1,core))
            {
                s1=request_read(add_s1,core,false);
            }
            else if(shared_access(add_s1,&shared_cache))
            {
                s1=request_read(add_s1,shared_cache,true);
            }
            else
            {
                // std::cout<<"cache is exclusive"<<std::endl;
                s1=replace_and_read(addr,core);
            }

            if (cache_hit(add_s2,core))
            {
                s2=request_read(add_s2,core,false);
            }
            else if(shared_access(add_s2,&shared_cache))
            {
                s2=request_read(add_s2,shared_cache,true);
            }
            else
            {
                // std::cout<<"cache is exclusive"<<std::endl;
                s2=replace_and_read(addr,core);
            }

            short val=s1-s2;
            if(cache_hit(add_dest,core))
            {
                request_write(add_dest,val,core,true);
            }
            else if(shared_access(add_dest,&shared_cache))
            {
                invalidate_other_caches(add_dest,core);
                request_write(add_dest,val,shared_cache,true);
            }
            else
            {
                replace_and_write(add_dest,val,core);
            }
            std::cout<<"value written after subtraction on "<<addr<<" = "<<val<<" on core = "<<core<<std::endl;
        }

        // cache_lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void invalidate_other_caches(int addr,int core)
    {
        for (int j = 0; j < NUM_CORES; ++j)
        {
            if(core==j)
                continue;
            CACHE_block cache=caches[j];
            if(cache_hit(addr,j))
            {
                std::cout<<"[+] --> invalidating cache on core "<<j<<std::endl;
                int line_no=addr%NUM_LINES;
                caches[j].lines[line_no].line_state=invalid;
            }
        }
    }

    void auto_assign(std::string *paths,int n)
    {
        #pragma omp parallel for num_threads(NUM_CORES)
        for (int i = 0; i < n; ++i)
        {
            std::cout<<"running on core "<<i<<std::endl;
            core_run_file(*(paths+i),i);
        }
    }

    short request_read(int addr,int core,bool flag)
    {
        short val;
        CACHE_block c=caches[core];
        for (int i = 0; i < NUM_LINES; ++i)
        {
            line l=c.lines[i];
            if (l.base==(addr-addr%SIZE_LINE))
            {
                l.line_state=flag?shared:l.line_state;
                return *(l.values+addr%SIZE_LINE);
            }
        }
        return 0;
    }

    short replace_and_read(int addr,int core)
    {
        // std::cout<<"replace and read function "<<std::endl;
        int line_no=addr%NUM_LINES;
        int new_base=addr-(addr%SIZE_LINE);
        short k=0;
        if (caches[core].lines[line_no].line_state==modified)
        {
            std::cout<<"replacing modified block therefore writing back to RAM"<<std::endl;
            for (int i = 0; i < SIZE_LINE; ++i)
            {
                *(RAM+caches[core].lines[line_no].base+i)=*(caches[core].lines[line_no].values+i);
            }
        }

        caches[core].lines[line_no].line_state=exclusive;
        caches[core].lines[line_no].base=new_base;

        for (int i = 0; i < SIZE_LINE; ++i)
        {
            short val=*(RAM+new_base+i);
            if(i==addr-new_base)
                k=val;
            *(caches[core].lines[line_no].values+i)=val;
        }
        return k;
    }

    void replace_and_write(int addr, short val,int core)
    {
        // std::cout<<"replace and write function "<<std::endl;
        int line_no=addr%NUM_LINES;
        int new_base=addr-(addr%SIZE_LINE);
        short t=replace_and_read(addr,core);
        int i=addr-new_base;
        caches[core].lines[line_no].line_state=modified;
        *(caches[core].lines[line_no].values+i)=val;
    }

    void request_write(int addr,short val,int core,bool flag)
    {
        CACHE_block cache=caches[core];
        int line_no=addr%NUM_LINES;
        int offset=addr%SIZE_LINE;
        caches[core].lines[line_no].line_state=flag? modified:exclusive;
        *(caches[core].lines[line_no].values+offset)=val;
    }

    bool shared_access(int addr,int *shared)
    {
        if (NUM_CORES==1)
            return false;
        for (int i = 0; i < NUM_CORES; ++i)
        {
            CACHE_block cache=caches[i];
            if(check_matching_base(cache,addr))
            {
                *shared=i;
                return true;
            }
        }
        return false;
    }

    bool cache_hit(int addr,int core)
    {
        CACHE_block cache=caches[core];
        return check_matching_base(cache,addr);
    }

    bool check_matching_base(CACHE_block c,int addr)
    {
        for (int i = 0; i < NUM_LINES; ++i)
        {
            line l=c.lines[i];
            if (l.base==(addr-addr%SIZE_LINE))
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
    std::string* file_paths=get_files(NUM_CORES);
    cpu.auto_assign(file_paths,NUM_CORES);
    // std::cout<<"completed execution "<<std::endl;
    // cpu.display_all();
    // std::cout<<"finished running"<<std::endl;
    return 0;
}