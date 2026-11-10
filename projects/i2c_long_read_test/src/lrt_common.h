/*
 * Shared by the controller and target sides of the I2C long-read bench test.
 */
#ifndef LRT_COMMON_H_
#define LRT_COMMON_H_

#include <stdint.h>

#define LRT_TGT_ADDR		0x51
/* Nothing answers here; used for the NACK tests */
#define LRT_ABSENT_ADDR		0x52

/* One byte written before a read: an SMBus block read command */
#define LRT_CMD_BLOCK		0xF6
/* One byte written before a read: return the last transaction's record */
#define LRT_CMD_STATS		0xA5

#define LRT_HASH_INIT		2166136261U

/*
 * What the target streams on every read. It restarts at index 0 on every
 * read address match, so a read split by a repeated START shows up as a
 * restart of the pattern.
 */
static inline uint8_t lrt_pat(uint32_t i)
{
	return (uint8_t)((i & 0xFF) ^ ((i >> 8) * 0xA5));
}

/* FNV-1a over the bytes written in one transaction */
static inline uint32_t lrt_hash(uint32_t h, uint8_t b)
{
	return (h ^ b) * 16777619U;
}

/*
 * One transaction (START ... STOP) as the target saw it. Stats queries are
 * not recorded. Sent raw; both sides are little-endian Cortex-M.
 */
struct lrt_record {
	uint32_t seq;		/* transactions recorded so far */
	uint32_t rd_match;	/* read address matches (>1: split read) */
	uint32_t wr_match;	/* write address matches */
	uint32_t wr_bytes;	/* bytes the controller wrote */
	uint32_t wr_hash;	/* lrt_hash() over those bytes */
	uint32_t wr_first;	/* first byte written */
	uint32_t done;		/* INTFL0.done count */
	uint32_t errs;		/* INTFL0 error bits seen */
};

#endif /* LRT_COMMON_H_ */
