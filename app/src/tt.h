#pragma once

#define __PRAGMA_PACKED__ __attribute__ ((__packed__))

typedef enum { NOTVALID = 0, EXACT = 1, LOWERBOUND = 2, UPPERBOUND = 3 } tt_entry_flag;

typedef struct __PRAGMA_PACKED__
{
        uint64_t hash;

        union u {
                struct {
                        int16_t score;
                        uint8_t flags : 2;
                        uint8_t age : 6;
                        uint8_t depth : 8;
                        uint32_t m;
                } _data;

                uint64_t data;
        } data_;
} tt_entry;

#define N_TE_PER_HASH_GROUP 2

typedef struct __PRAGMA_PACKED__
{
        tt_entry entries[N_TE_PER_HASH_GROUP];
} tt_hash_group;

#ifdef linux
constexpr uint64_t n_entries { 256 * 1024 * 1024 / sizeof(tt_hash_group) };
#else
constexpr uint64_t n_entries { 65536 / sizeof(tt_hash_group) };
#endif

class tt
{
private:
	tt_hash_group entries[n_entries];

	int age;

public:
	tt();
	~tt();

	void inc_age();

	std::optional<tt_entry> lookup(const uint64_t board_hash);
	void store(const uint64_t hash, const tt_entry_flag f, const int d, const int score, const libchess::Move & m);
};
