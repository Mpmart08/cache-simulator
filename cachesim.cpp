#include "cachesim.hpp"
#include <cmath>
#include <list>
#include <iterator>

using namespace std;

enum class CacheID { L1, L2 };

struct Block
{
    bool valid;
    bool dirty;
    uint64_t tag;
    uint64_t address;

    Block(bool valid = false, bool dirty = false, uint64_t tag = 0, uint64_t address = 0)
        : valid(valid), dirty(dirty), tag(tag), address(address) {}

    bool operator==(const Block &other)
    {
        return (valid) && (other.valid) && (tag == other.tag);
    }
};

struct Set
{
    list<Block> blocks;
};

class VictimCache
{
private:

    uint64_t numberOfBlocks;
    uint64_t blockSize;
    list<Block> blocks;

public:

    VictimCache(uint64_t v, uint64_t b)
    {
        numberOfBlocks = v;
        blockSize = (uint64_t) pow(2, b);
        blocks = list<Block>(numberOfBlocks, Block());
    }

    ~VictimCache()
    {
        blocks.clear();
    }

    bool read(uint64_t address, cache_stats_t* p_stats)
    {
        p_stats->accesses_vc++;

        uint64_t tag = address / blockSize;

        for (list<Block>::iterator iterator = blocks.begin(); iterator != blocks.end(); ++iterator)
        {
            Block block = *iterator;
            if (block.valid && block.tag == tag)
            {
                p_stats->victim_hits++;
                //printf("M1HV**\n");
                blocks.remove(block);
                return true;
            }
        }

        return false;
    }

    void insert(uint64_t address)
    {
        uint64_t tag = address / blockSize;

        if (blocks.size() == numberOfBlocks)
        {
            blocks.pop_back();
        }

        blocks.push_front(Block(true, false, tag, address));
    }

    void insertInvalid()
    {
        if (blocks.size() != numberOfBlocks)
        {
            blocks.push_back(Block(false, false, 0, 0));
        }
    }
};

class Cache
{
private:

    CacheID cacheID;            // L! or L2
    uint64_t cacheSize;         // 2^C bytes total
    uint64_t blockSize;         // 2^B bytes per block
    uint64_t associativity;     // 2^S blocks per set
    Set *sets;                  // array of sets
    VictimCache *victimCache;   // victim cache
    Cache *memory;              // memory or cache below in the cache hierarchy
    static uint64_t V;

public:

    Cache(CacheID id, uint64_t c, uint64_t b, uint64_t s, uint64_t v = 0, Cache *cache = nullptr)
    {
        cacheID = id;
        cacheSize = (uint64_t) pow(2, c);
        blockSize = (uint64_t) pow(2, b);
        associativity = (uint64_t) pow(2, s);
        V = v;

        uint64_t numberOfSets = cacheSize / blockSize / associativity;
        sets = new Set[numberOfSets];

        for (uint64_t i = 0; i < numberOfSets; ++i)
        {
            sets[i].blocks = list<Block>(associativity, Block());
        }

        victimCache = (V == 0) ? nullptr : new VictimCache(v, b);
        memory = cache;
    }

    ~Cache()
    {
        uint64_t numberOfSets = cacheSize / blockSize / associativity;

        for (uint64_t i = 0; i < numberOfSets; ++i)
        {
            sets[i].blocks.clear();
        }

        delete[] sets;

        if (victimCache != nullptr)
        {
            delete victimCache;
        }
    }

    void read(uint64_t address, cache_stats_t* p_stats)
    {
        (cacheID == CacheID::L1) ? p_stats->accesses++ : p_stats->accesses_l2++;

        uint64_t numberOfSets = cacheSize / blockSize / associativity;
        uint64_t tag = address / blockSize / numberOfSets;
        uint64_t index = address / blockSize % numberOfSets;

        Set *set = &sets[index];
        list<Block> *blocks = &set->blocks;

        for (list<Block>::iterator iterator = blocks->begin(); iterator != blocks->end(); ++iterator)
        {
            Block block = *iterator;
            if (block.valid && block.tag == tag)
            {
                // a hit has occurred if this point is reached
                blocks->remove(block);
                blocks->push_front(block);
                if (cacheID == CacheID::L1)
                {
                    //(V == 0) ? printf("H1**\n") : printf("H1****\n");
                } else {
                    //(V == 0) ? printf("M1H2\n") : printf("M1MVH2\n");
                }
                return;
            }
        }

        // a miss has a occurred if this point is reached
        (cacheID == CacheID::L1) ? p_stats->read_misses_l1++ : p_stats->read_misses_l2++;

        if (victimCache != nullptr)
        {
            bool hit = victimCache->read(address, p_stats);
            if (hit)
            {
                // evict block that was LRU
                Block block = blocks->back();
                blocks->pop_back();

                // determine if writeback is necessary
                if (block.dirty)
                {
                    (cacheID == CacheID::L1) ? p_stats->write_back_l1++ : p_stats->write_back_l2++;
                    if (memory != nullptr)
                    {
                        memory->write(block.address, p_stats, true);
                    }
                }

                if (block.valid)
                {
                    // insert evicted block into victim cache
                    victimCache->insert(block.address);
                }
                else
                {
                    victimCache->insertInvalid();
                }

                // insert block retrieved from victim cache
                blocks->push_front(Block(true, false, tag, address));
                return;
            }
        }

        // a cache miss and a victim cache miss have occurred if this point is reached
        // and we need to bring in block from next level cache or memory

        if (cacheID == CacheID::L2)
        {
            //(V == 0) ? printf("M1M2\n") : printf("M1MVM2\n");
        }

        if (memory != nullptr)
        {
            memory->read(address, p_stats);
        }

        // evict block that was LRU
        Block block = blocks->back();
        blocks->pop_back();

        // determine if writeback is necessary
        if (block.dirty)
        {
            (cacheID == CacheID::L1) ? p_stats->write_back_l1++ : p_stats->write_back_l2++;
            if (memory != nullptr)
            {
                memory->write(block.address, p_stats, true);
            }
        }

        if (victimCache != nullptr)
        {
            if (block.valid)
            {
                // insert evicted block into victim cache
                victimCache->insert(block.address);
            }
            else
            {
                victimCache->insertInvalid();
            }
        }
        
        // insert block retrieved from next level cache or memory
        blocks->push_front(Block(true, false, tag, address));
    }

    void write(uint64_t address, cache_stats_t* p_stats, bool isWriteBack)
    {
        (cacheID == CacheID::L1) ? p_stats->accesses++ : p_stats->accesses_l2++;

        uint64_t numberOfSets = cacheSize / blockSize / associativity;
        uint64_t tag = address / blockSize / numberOfSets;
        uint64_t index = address / blockSize % numberOfSets;

        Set *set = &sets[index];
        list<Block> *blocks = &set->blocks;

        for (list<Block>::iterator iterator = blocks->begin(); iterator != blocks->end(); ++iterator)
        {
            Block block = *iterator;
            if (block.valid && block.tag == tag)
            {
                // a hit has occurred if this point is reached
                blocks->remove(block);
                block.dirty = true;
                blocks->push_front(block);
                if (cacheID == CacheID::L1 && !isWriteBack)
                {
                    //(V == 0) ? printf("H1**\n") : printf("H1****\n");
                }
                else if (cacheID == CacheID::L2 && !isWriteBack)
                {
                    //(V == 0) ? printf("M1H2\n") : printf("M1MVH2\n");
                }
                return;
            }
        }

        // a miss has a occurred if this point is reached
        (cacheID == CacheID::L1) ? p_stats->write_misses_l1++ : p_stats->write_misses_l2++;

        if (victimCache != nullptr)
        {
            bool hit = victimCache->read(address, p_stats);
            if (hit)
            {
                // evict block that was LRU
                Block block = blocks->back();
                blocks->pop_back();

                // determine if writeback is necessary
                if (block.dirty)
                {
                    (cacheID == CacheID::L1) ? p_stats->write_back_l1++ : p_stats->write_back_l2++;
                    if (memory != nullptr)
                    {
                        memory->write(block.address, p_stats, true);
                    }
                }

                if (block.valid)
                {
                    // insert evicted block into victim cache
                    victimCache->insert(block.address);
                }
                else
                {
                    victimCache->insertInvalid();
                }

                // insert block retrieved from victim cache
                blocks->push_front(Block(true, true, tag, address));
                return;
            }
        }

        // a cache miss and a victim cache miss have occurred if this point is reached
        // and we need to bring in block from next level cache or memory

        if (cacheID == CacheID::L2 && !isWriteBack)
        {
            //(V == 0) ? printf("M1M2\n") : printf("M1MVM2\n");
        }

        if (memory != nullptr)
        {
            memory->read(address, p_stats);
        }

        Block block = blocks->back();
        blocks->pop_back();

        // determine if writeback is necessary
        if (block.dirty)
        {
            (cacheID == CacheID::L1) ? p_stats->write_back_l1++ : p_stats->write_back_l2++;
            if (memory != nullptr)
            {
                memory->write(block.address, p_stats, true);
            }
        }

        if (victimCache != nullptr)
        {
            if (block.valid)
            {
                // insert evicted block into victim cache
                victimCache->insert(block.address);
            }
            else
            {
                victimCache->insertInvalid();
            }
        }

        // insert block retrieved from next level cache or memory
        blocks->push_front(Block(true, true, tag, address));
    }
};

Cache *L1;
Cache *L2;
uint64_t s1;
uint64_t s2;
uint64_t v;
uint64_t Cache::V = 0;

/**
 * Subroutine for initializing the cache. You many add and initialize any global or heap
 * variables as needed.
 * XXX: You're responsible for completing this routine
 *
 * @c1 The total number of bytes for data storage in L1 is 2^c1
 * @b1 The size of L1's blocks in bytes: 2^b1-byte blocks.
 * @s1 The number of blocks in each set of L1: 2^s1 blocks per set.
 * @v Victim Cache's total number of blocks (blocks are of size 2^b1).
 *    v is in [0, 4].
 * @c2 The total number of bytes for data storage in L2 is 2^c2
 * @b2 The size of L2's blocks in bytes: 2^b2-byte blocks.
 * @s2 The number of blocks in each set of L2: 2^s2 blocks per set.
 * Note: c2 >= c1, b2 >= b1 and s2 >= s1.
 */
void setup_cache(uint64_t c1, uint64_t b1, uint64_t s1, uint64_t v,
                 uint64_t c2, uint64_t b2, uint64_t s2)
{
    ::s1 = s1;
    ::s2 = s2;
    ::v = v;

    L2 = new Cache(CacheID::L2, c2, b2, s2);
    L1 = new Cache(CacheID::L1, c1, b1, s1, v, L2);
}
/**
 * Subroutine that simulates the cache one trace event at a time.
 * XXX: You're responsible for completing this routine
 *
 * @type The type of event, can be READ or WRITE.
 * @arg  The target memory address
 * @p_stats Pointer to the statistics structure
 */
void cache_access(char type, uint64_t arg, cache_stats_t* p_stats)
{
    switch (type)
    {
    case READ:
        p_stats->reads++;
        L1->read(arg, p_stats);
        break;
    case WRITE:
        p_stats->writes++;
        L1->write(arg, p_stats, false);
        break;
    }
}

/**
 * Subroutine for cleaning up any outstanding memory operations and calculating overall statistics
 * such as miss rate or average access time.
 * XXX: You're responsible for completing this routine
 *
 * @p_stats Pointer to the statistics structure
 */
void complete_cache(cache_stats_t *p_stats)
{
    float mp2 = 500.0;
    float ht2 = 4.0 + (0.4 * (float) s2);
    float mr2 = ((float) p_stats->read_misses_l2 + p_stats->write_misses_l2) / ((float) p_stats->accesses_l2);
    float aat2 = ht2 + (mr2 * mp2);

    float mp1 = (v == 0) ? aat2 : ((((float) p_stats->accesses_vc - p_stats->victim_hits) / ((float) p_stats->accesses_vc)) * (float) aat2);
    float ht1 = 2.0 + (0.2 * (float) s1);
    float mr1 = ((float) p_stats->read_misses_l1 + p_stats->write_misses_l1) / (float) p_stats->accesses;
    float aat1 = ht1 + (mr1 * mp1);

    p_stats->avg_access_time_l1 = aat1;

    //printf("\n");
    
    delete L1;
    delete L2;
}
