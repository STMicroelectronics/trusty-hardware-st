#ifndef STM32_UTIL_H
#define STM32_UTIL_H

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifdef __ASSEMBLER__
#define BIT32(nr)           (1 << (nr))
#define BIT64(nr)           (1 << (nr))
#define SHIFT_U32(v, shift) ((v) << (shift))
#define SHIFT_U64(v, shift) ((v) << (shift))
#else
#define BIT32(nr)           (UINT32_C(1) << (nr))
#define BIT64(nr)           (UINT64_C(1) << (nr))
#define SHIFT_U32(v, shift) ((uint32_t)(v) << (shift))
#define SHIFT_U64(v, shift) ((uint64_t)(v) << (shift))
#endif
#define BIT(nr)     BIT32(nr)

/*
 * Create a contiguous bitmask starting at bit position @l and ending at
 * position @h. For example
 * GENMASK_64(39, 21) gives us the 64bit vector 0x000000ffffe00000.
 */
#define GENMASK_32(h, l) \
    (((~UINT32_C(0)) << (l)) & (~UINT32_C(0) >> (32 - 1 - (h))))

#define GENMASK_64(h, l) \
    (((~UINT64_C(0)) << (l)) & (~UINT64_C(0) >> (64 - 1 - (h))))

/*
 * Checking overflow for addition, subtraction and multiplication. Result
 * of operation is stored in res which is a pointer to some kind of
 * integer.
 *
 * The macros return true if an overflow occurred and *res is undefined.
 */
#define ADD_OVERFLOW(a, b, res) __builtin_add_overflow((a), (b), (res))
#define SUB_OVERFLOW(a, b, res) __builtin_sub_overflow((a), (b), (res))
#define MUL_OVERFLOW(a, b, res) __builtin_mul_overflow((a), (b), (res))

/**
 * @brief Functions to get and set bit fields in a 32/64-bit register.
 *
 * These inline functions allow setting and extracting specific bits in
 * a register (`reg`) according to a given mask (`mask`). The mask
 * specifies which bits in the register should be updated or extracted.
 *
 * - `set_field`: Modifies specific bits in a register by clearing the
 *   bits specified by the mask and then setting these bits to the new
 *   value (`val`).
 * - `get_field`: Extracts the value of specific bits in a register by
 *   isolating the bits specified by the mask and then shifting them to
 *   the rightmost position.
 *
 * @param reg The original 32-bit or 64-bit register value.
 * @param mask A bitmask indicating which bits in the register should be
 * updated or extracted.
 * @param val The new value to be inserted into the register at the
 * positions specified by the mask (only for `set_field` functions).
 * @return The updated register value with the specified bits set to the
 * new value (for `set_field` functions), or the value of the bits
 * specified by the mask, shifted to the rightmost position (for
 * `get_field` functions).
 *
 * Note: These functions exist in both 32-bit (`set_field_u32`,
 * `get_field_u32`) and 64-bit (`set_field_u64`, `get_field_u64`)
 * versions.
 */
static inline uint32_t get_field_u32(uint32_t reg, uint32_t mask)
{
    return (reg & mask) / (mask & ~(mask - 1));
}

static inline uint32_t set_field_u32(uint32_t reg, uint32_t mask, uint32_t val)
{
    return (reg & ~mask) | (val * (mask & ~(mask - 1)));
}

static inline uint64_t get_field_u64(uint64_t reg, uint64_t mask)
{
    return (reg & mask) / (mask & ~(mask - 1));
}

static inline uint64_t set_field_u64(uint64_t reg, uint64_t mask, uint64_t val)
{
    return (reg & ~mask) | (val * (mask & ~(mask - 1)));
}


#endif /* !STM32_UTIL_H */
