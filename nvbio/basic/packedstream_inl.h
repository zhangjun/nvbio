/*
 * nvbio
 * Copyright (C) 2012-2014, NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <nvbio/basic/cached_iterator.h>

namespace nvbio {

template <bool BIG_ENDIAN_T, uint32 SYMBOL_SIZE, typename Symbol, typename InputStream, typename IndexType, typename ValueType>
struct packer {
};

template <bool BIG_ENDIAN_T, uint32 SYMBOL_SIZE, typename Symbol, typename InputStream, typename IndexType>
struct packer<BIG_ENDIAN_T,SYMBOL_SIZE,Symbol,InputStream,IndexType,uint32>
{
    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get_symbol(InputStream stream, const IndexType sym_idx)
    {
        const uint32 SYMBOL_COUNT = 1u << SYMBOL_SIZE;
        const uint32 SYMBOL_MASK  = SYMBOL_COUNT - 1u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const uint64     bit_idx  = uint64(sym_idx) * SYMBOL_SIZE;
        const index_type word_idx = index_type( bit_idx >> 5u );

        if (is_pow2<SYMBOL_SIZE>())
        {
            const uint32 word = stream[ word_idx ];
            const uint32 symbol_offset = BIG_ENDIAN_T ? (32u - SYMBOL_SIZE  - uint32(bit_idx & 31u)) : uint32(bit_idx & 31u);
            const uint32 symbol = (word >> symbol_offset) & SYMBOL_MASK;

            return Symbol( symbol );
        }
        else
        {
            const uint32 word1 = stream[ word_idx ];
            const uint32 symbol_offset = uint32(bit_idx & 31u);
            const uint32 symbol1 = (word1 >> symbol_offset) & SYMBOL_MASK;

            // check if we need to read a second word
            const uint32 read_bits = nvbio::min( 32u - symbol_offset, SYMBOL_SIZE );
            const uint32 rem_bits  = SYMBOL_SIZE - read_bits;
            if (rem_bits)
            {
                const uint32 rem_mask = (1u << rem_bits) - 1u;

                const uint32 word2 = stream[ word_idx+1 ];
                const uint32 symbol2 = word2 & rem_mask;

                return Symbol( symbol1 | (symbol2 << read_bits) );
            }
            else
                return Symbol( symbol1 );
        }
    }

    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set_symbol(InputStream stream, const IndexType sym_idx, Symbol sym)
    {
        const uint32 SYMBOL_COUNT = 1u << SYMBOL_SIZE;
        const uint32 SYMBOL_MASK  = SYMBOL_COUNT - 1u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const uint64     bit_idx  = uint64(sym_idx) * SYMBOL_SIZE;
        const index_type word_idx = index_type( bit_idx >> 5u );

        if (is_pow2<SYMBOL_SIZE>())
        {
                  uint32 word = stream[ word_idx ];
            const uint32 symbol_offset = BIG_ENDIAN_T ? (32u - SYMBOL_SIZE - uint32(bit_idx & 31u)) : uint32(bit_idx & 31u);
            const uint32 symbol = uint32(sym & SYMBOL_MASK) << symbol_offset;

            // clear all bits
            word &= ~(SYMBOL_MASK << symbol_offset);

            // set bits
            stream[ word_idx ] = word | symbol;
        }
        else
        {
                  uint32 word1 = stream[ word_idx ];
            const uint32 symbol_offset = uint32(bit_idx & 31u);
            const uint32 symbol1 = uint32(sym & SYMBOL_MASK) << symbol_offset;

            // clear all bits
            word1 &= ~(SYMBOL_MASK << symbol_offset);

            // set bits
            stream[ word_idx ] = word1 | symbol1;

            // check if we need to write a second word
            const uint32 read_bits = nvbio::min( 32u - symbol_offset, SYMBOL_SIZE );
            const uint32 rem_bits  = SYMBOL_SIZE - read_bits;
            if (rem_bits)
            {
                      uint32 word2   = stream[ word_idx+1 ];
                const uint32 symbol2 = uint32(sym & SYMBOL_MASK) >> read_bits;

                const uint32 rem_mask = (1u << rem_bits) - 1u;

                // clear all bits
                word2 &= ~rem_mask;

                // set bits
                stream[ word_idx+1 ] = word2 | symbol2;
            }
        }
    }
};

template <bool BIG_ENDIAN_T, uint32 SYMBOL_SIZE, typename Symbol, typename InputStream, typename IndexType>
struct packer<BIG_ENDIAN_T,SYMBOL_SIZE,Symbol,InputStream,IndexType,uint64>
{
    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get_symbol(InputStream stream, const IndexType sym_idx)
    {
        const uint32 SYMBOL_COUNT = 1u << SYMBOL_SIZE;
        const uint32 SYMBOL_MASK  = SYMBOL_COUNT - 1u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const uint64     bit_idx  = uint64(sym_idx) * SYMBOL_SIZE;
        const index_type word_idx = index_type( bit_idx >> 6u );

        if (is_pow2<SYMBOL_SIZE>())
        {
            const uint64 word = stream[ word_idx ];
            const uint32 symbol_offset = BIG_ENDIAN_T ? (64u - SYMBOL_SIZE  - uint32(bit_idx & 63u)) : uint32(bit_idx & 63u);
            const uint32 symbol = uint32((word >> symbol_offset) & SYMBOL_MASK);

            return Symbol( symbol );
        }
        else
        {
            const uint32 word1 = stream[ word_idx ];
            const uint32 symbol_offset = uint32(bit_idx & 63u);
            const uint32 symbol1 = uint32((word1 >> symbol_offset) & SYMBOL_MASK);

            // check if we need to read a second word
            const uint32 read_bits = nvbio::min( 64u - symbol_offset, SYMBOL_SIZE );
            const uint32 rem_bits  = SYMBOL_SIZE - read_bits;
            if (rem_bits)
            {
                const uint64 rem_mask = (uint64(1u) << rem_bits) - 1u;

                const uint64 word2 = stream[ word_idx+1 ];
                const uint32 symbol2 = uint32(word2 & rem_mask);

                return Symbol( symbol1 | (symbol2 << read_bits) );
            }
            else
                return Symbol( symbol1 );
        }
    }

    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set_symbol(InputStream stream, const IndexType sym_idx, Symbol sym)
    {
        const uint32 SYMBOL_COUNT = 1u << SYMBOL_SIZE;
        const uint32 SYMBOL_MASK  = SYMBOL_COUNT - 1u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const uint64     bit_idx  = uint64(sym_idx) * SYMBOL_SIZE;
        const index_type word_idx = index_type( bit_idx >> 6u );

        if (is_pow2<SYMBOL_SIZE>())
        {
                  uint64 word = stream[ word_idx ];
            const uint32 symbol_offset = BIG_ENDIAN_T ? (64u - SYMBOL_SIZE - uint32(bit_idx & 63u)) : uint32(bit_idx & 63u);
            const uint64 symbol = uint64(sym & SYMBOL_MASK) << symbol_offset;

            // clear all bits
            word &= ~(uint64(SYMBOL_MASK) << symbol_offset);

            // set bits
            stream[ word_idx ] = word | symbol;
        }
        else
        {
                  uint64 word1 = stream[ word_idx ];
            const uint32 symbol_offset = uint32(bit_idx & 63);
            const uint64 symbol1 = uint64(sym & SYMBOL_MASK) << symbol_offset;

            // clear all bits
            word1 &= ~(uint64(SYMBOL_MASK) << symbol_offset);

            // set bits
            stream[ word_idx ] = word1 | symbol1;

            // check if we need to write a second word
            const uint32 read_bits = nvbio::min( 64u - symbol_offset, SYMBOL_SIZE );
            const uint32 rem_bits  = SYMBOL_SIZE - read_bits;
            if (rem_bits)
            {
                      uint32 word2   = stream[ word_idx+1 ];
                const uint64 symbol2 = uint64(sym & SYMBOL_MASK) >> read_bits;

                const uint64 rem_mask = (uint64(1u) << rem_bits) - 1u;

                // clear all bits
                word2 &= ~rem_mask;

                // set bits
                stream[ word_idx+1 ] = word2 | symbol2;
            }
        }
    }
};

template <bool BIG_ENDIAN_T, uint32 SYMBOL_SIZE, typename Symbol, typename InputStream, typename IndexType>
struct packer<BIG_ENDIAN_T,SYMBOL_SIZE,Symbol,InputStream,IndexType,uint8>
{
    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get_symbol(InputStream stream, const IndexType sym_idx)
    {
        const uint8 SYMBOL_COUNT = uint8(1u) << SYMBOL_SIZE;
        const uint8 SYMBOL_MASK  = SYMBOL_COUNT - uint8(1u);

        typedef typename unsigned_type<IndexType>::type index_type;

        const uint64     bit_idx  = uint64(sym_idx) * SYMBOL_SIZE;
        const index_type word_idx = index_type( bit_idx >> 3u );

        if (is_pow2<SYMBOL_SIZE>())
        {
            const uint8 word = stream[ word_idx ];
            const uint8 symbol_offset = BIG_ENDIAN_T ? (8u - SYMBOL_SIZE - uint8(bit_idx & 7u)) : uint8(bit_idx & 7u);
            const uint8 symbol = (word >> symbol_offset) & SYMBOL_MASK;

            return Symbol( symbol );
        }
        else
        {
            const uint8 word1 = stream[ word_idx ];
            const uint8 symbol_offset = uint8(bit_idx & 7u);
            const uint8 symbol1 = (word1 >> symbol_offset) & SYMBOL_MASK;

            // check if we need to read a second word
            const uint32 read_bits = nvbio::min( 8u - symbol_offset, SYMBOL_SIZE );
            const uint32 rem_bits  = SYMBOL_SIZE - read_bits;
            if (rem_bits)
            {
                const uint8 rem_mask = uint8((1u << rem_bits) - 1u);

                const uint8 word2 = stream[ word_idx+1 ];
                const uint8 symbol2 = word2 & rem_mask;

                return Symbol( symbol1 | (symbol2 << read_bits) );
            }
            else
                return Symbol( symbol1 );
        }
    }

    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set_symbol(InputStream stream, const IndexType sym_idx, Symbol sym)
    {
        const uint8 SYMBOL_COUNT = uint8(1u) << SYMBOL_SIZE;
        const uint8 SYMBOL_MASK  = SYMBOL_COUNT - uint8(1u);

        typedef typename unsigned_type<IndexType>::type index_type;

        const uint64     bit_idx  = uint64(sym_idx) * SYMBOL_SIZE;
        const index_type word_idx = index_type( bit_idx >> 3u );

        if (is_pow2<SYMBOL_SIZE>())
        {
                  uint8 word = stream[ word_idx ];
            const uint8 symbol_offset = BIG_ENDIAN_T ? (8u - SYMBOL_SIZE - uint8(bit_idx & 7u)) : uint8(bit_idx & 7u);
            const uint8 symbol = uint32(sym & SYMBOL_MASK) << symbol_offset;

            // clear all bits
            word &= ~(SYMBOL_MASK << symbol_offset);

            // set bits
            stream[ word_idx ] = word | symbol;
        }
        else
        {
                  uint8 word1 = stream[ word_idx ];
            const uint8 symbol_offset = uint8(bit_idx & 7u);
            const uint8 symbol1 = uint8(sym & SYMBOL_MASK) << symbol_offset;

            // clear all bits
            word1 &= ~(SYMBOL_MASK << symbol_offset);

            // set bits
            stream[ word_idx ] = word1 | symbol1;

            // check if we need to write a second word
            const uint32 read_bits = nvbio::min( 8u - symbol_offset, SYMBOL_SIZE );
            const uint32 rem_bits  = SYMBOL_SIZE - read_bits;
            if (rem_bits)
            {
                      uint8 word2   = stream[ word_idx+1 ];
                const uint8 symbol2 = uint32(sym & SYMBOL_MASK) >> read_bits;

                const uint8 rem_mask = uint8((1u << rem_bits) - 1u);

                // clear all bits
                word2 &= ~rem_mask;

                // set bits
                stream[ word_idx+1 ] = word2 | symbol2;
            }
        }
    }
};


template <bool BIG_ENDIAN_T, typename Symbol, typename InputStream, typename IndexType>
struct packer<BIG_ENDIAN_T,2u,Symbol,InputStream,IndexType,uint32>
{
    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get_symbol(InputStream stream, const IndexType sym_idx)
    {
        const uint32 SYMBOL_MASK = 3u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 4u;

        const uint32 word = stream[ word_idx ];
        const uint32 symbol_offset = BIG_ENDIAN_T ? (30u - (uint32(sym_idx & 15u) << 1)) : uint32((sym_idx & 15u) << 1);
        const uint32 symbol = (word >> symbol_offset) & SYMBOL_MASK;

        return Symbol( symbol );
    }

    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set_symbol(InputStream stream, const IndexType sym_idx, Symbol sym)
    {
        const uint32 SYMBOL_MASK = 3u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 4u;

              uint32 word = stream[ word_idx ];
        const uint32 symbol_offset = BIG_ENDIAN_T ? (30u - (uint32(sym_idx & 15u) << 1)) : uint32((sym_idx & 15u) << 1);
        const uint32 symbol = uint32(sym & SYMBOL_MASK) << symbol_offset;

        // clear all bits
        word &= ~(SYMBOL_MASK << symbol_offset);

        // set bits
        stream[ word_idx ] = word | symbol;
    }
};
template <bool BIG_ENDIAN_T, typename Symbol, typename InputStream, typename IndexType>
struct packer<BIG_ENDIAN_T,4u,Symbol,InputStream,IndexType,uint32>
{
    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get_symbol(InputStream stream, const IndexType sym_idx)
    {
        const uint32 SYMBOL_MASK = 15u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 3u;

        const uint32 word = stream[ word_idx ];
        const uint32 symbol_offset = BIG_ENDIAN_T ? (28u - (uint32(sym_idx & 7u) << 2)) : uint32((sym_idx & 7u) << 2);
        const uint32 symbol = (word >> symbol_offset) & SYMBOL_MASK;

        return Symbol( symbol );
    }

    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set_symbol(InputStream stream, const IndexType sym_idx, Symbol sym)
    {
        const uint32 SYMBOL_MASK = 15u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 3u;

              uint32 word = stream[ word_idx ];
        const uint32 symbol_offset = BIG_ENDIAN_T ? (28u - (uint32(sym_idx & 7u) << 2)) : uint32((sym_idx & 7u) << 2);
        const uint32 symbol = uint32(sym & SYMBOL_MASK) << symbol_offset;

        // clear all bits
        word &= ~(SYMBOL_MASK << symbol_offset);

        // set bits
        stream[ word_idx ] = word | symbol;
    }
};

template <bool BIG_ENDIAN_T, typename Symbol, typename InputStream, typename IndexType>
struct packer<BIG_ENDIAN_T,2u,Symbol,InputStream,IndexType,uint4>
{
    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get_symbol(InputStream stream, const IndexType sym_idx)
    {
        const uint32 SYMBOL_MASK = 3u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 6u;

        const uint4  word = stream[ word_idx ];
        const uint32 symbol_comp   = (sym_idx & 63u) >> 4u;
        const uint32 symbol_offset = BIG_ENDIAN_T ? (30u - (uint32(sym_idx & 15u) << 1)) : uint32((sym_idx & 15u) << 1);
        const uint32 symbol = (comp( word, symbol_comp ) >> symbol_offset) & SYMBOL_MASK;

        return Symbol( symbol );
    }

    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set_symbol(InputStream stream, const IndexType sym_idx, Symbol sym)
    {
        const uint32 SYMBOL_MASK = 3u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 6u;

              uint4  word = stream[ word_idx ];
        const uint32 symbol_comp   = (sym_idx & 63u) >> 4u;
        const uint32 symbol_offset = BIG_ENDIAN_T ? (30u - (uint32(sym_idx & 15u) << 1)) : uint32((sym_idx & 15u) << 1);
        const uint32 symbol = uint32(sym & SYMBOL_MASK) << symbol_offset;

        // clear all bits
        select( word, symbol_comp ) &= ~(SYMBOL_MASK << symbol_offset);
        select( word, symbol_comp ) |= symbol;

        // set bits
        stream[ word_idx ] = word;
    }
};
template <bool BIG_ENDIAN_T, typename Symbol, typename InputStream, typename IndexType>
struct packer<BIG_ENDIAN_T,4u,Symbol,InputStream,IndexType,uint4>
{
    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get_symbol(InputStream stream, const IndexType sym_idx)
    {
        const uint32 SYMBOL_MASK = 15u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 5u;

        const uint4 word = stream[ word_idx ];
        const uint32 symbol_comp   = (sym_idx & 31u) >> 3u;
        const uint32 symbol_offset = BIG_ENDIAN_T ? (28u - (uint32(sym_idx & 7u) << 2)) : uint32((sym_idx & 7u) << 2);
        const uint32 symbol = (comp( word, symbol_comp ) >> symbol_offset) & SYMBOL_MASK;

        return Symbol( symbol );
    }

    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set_symbol(InputStream stream, const IndexType sym_idx, Symbol sym)
    {
        const uint32 SYMBOL_MASK = 15u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 5u;

              uint4  word = stream[ word_idx ];
        const uint32 symbol_comp   = (sym_idx & 31u) >> 3u;
        const uint32 symbol_offset = BIG_ENDIAN_T ? (28u - (uint32(sym_idx & 7u) << 2)) : uint32((sym_idx & 7u) << 2);
        const uint32 symbol = uint32(sym & SYMBOL_MASK) << symbol_offset;

        // clear all bits
        select( word, symbol_comp ) &= ~(SYMBOL_MASK << symbol_offset);
        select( word, symbol_comp ) |= symbol;

        // set bits
        stream[ word_idx ] = word;
    }
};

template <bool BIG_ENDIAN_T, typename Symbol, typename InputStream, typename IndexType>
struct packer<BIG_ENDIAN_T,2u,Symbol,InputStream,IndexType,uint64>
{
    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol get_symbol(InputStream stream, const IndexType sym_idx)
    {
        const uint32 SYMBOL_MASK = 3u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 5u;

        const uint64 word = stream[ word_idx ];
        const uint32 symbol_offset = BIG_ENDIAN_T ? (62u - (uint64(sym_idx & 31u) << 1)) : uint64((sym_idx & 31u) << 1);
        const uint64 symbol = (word >> symbol_offset) & SYMBOL_MASK;

        return Symbol( symbol );
    }

    static NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void set_symbol(InputStream stream, const IndexType sym_idx, Symbol sym)
    {
        const uint32 SYMBOL_MASK = 3u;

        typedef typename unsigned_type<IndexType>::type index_type;

        const index_type word_idx = sym_idx >> 5u;

              uint64 word = stream[ word_idx ];
        const uint32 symbol_offset = BIG_ENDIAN_T ? (62u - (uint64(sym_idx & 31u) << 1)) : uint64((sym_idx & 31u) << 1);
        const uint64 symbol = uint64(sym & SYMBOL_MASK) << symbol_offset;

        // clear all bits
        word &= ~(uint64(SYMBOL_MASK) << symbol_offset);

        // set bits
        stream[ word_idx ] = word | symbol;
    }
};

template <typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE Symbol PackedStream<InputStream,Symbol, SYMBOL_SIZE_T, BIG_ENDIAN_T, IndexType>::get(const index_type sym_idx) const
{
    return packer<BIG_ENDIAN_T, SYMBOL_SIZE,Symbol,InputStream,IndexType,storage_type>::get_symbol( m_stream, sym_idx );
}
template <typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void PackedStream<InputStream,Symbol, SYMBOL_SIZE_T, BIG_ENDIAN_T, IndexType>::set(const index_type sym_idx, const Symbol sym)
{
    return packer<BIG_ENDIAN_T, SYMBOL_SIZE,Symbol,InputStream,IndexType,storage_type>::set_symbol( m_stream, sym_idx, sym );
}
// return begin iterator
//
template <typename InputStream, typename Symbol, uint32 SYMBOL_SIZE_T, bool BIG_ENDIAN_T, typename IndexType>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
typename PackedStream<InputStream,Symbol, SYMBOL_SIZE_T, BIG_ENDIAN_T, IndexType>::iterator
PackedStream<InputStream,Symbol, SYMBOL_SIZE_T, BIG_ENDIAN_T, IndexType>::begin() const
{
    return iterator( m_stream, 0 );
}

/*
// dereference operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
typename PackedStreamIterator<Stream>::Symbol
PackedStreamIterator<Stream>::operator* () const
{
    return m_stream.get( m_index );
}
*/
// dereference operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE typename PackedStreamIterator<Stream>::reference PackedStreamIterator<Stream>::operator* () const
{
    return reference( m_stream, m_index );
}

// indexing operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE typename PackedStreamIterator<Stream>::reference PackedStreamIterator<Stream>::operator[] (const sindex_type i) const
{
    return reference( m_stream, m_index + i );
}
/*
// indexing operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE typename PackedStreamIterator<Stream>::reference PackedStreamIterator<Stream>::operator[] (const index_type i) const
{
    return reference( m_stream, m_index + i );
}
*/
// set value
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE void PackedStreamIterator<Stream>::set(const Symbol s)
{
    m_stream.set( m_index, s );
}

// pre-increment operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamIterator<Stream>& PackedStreamIterator<Stream>::operator++ ()
{
    ++m_index;
    return *this;
}

// post-increment operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamIterator<Stream> PackedStreamIterator<Stream>::operator++ (int dummy)
{
    This r( m_stream, m_index );
    ++m_index;
    return r;
}

// pre-decrement operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamIterator<Stream>& PackedStreamIterator<Stream>::operator-- ()
{
    --m_index;
    return *this;
}

// post-decrement operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamIterator<Stream> PackedStreamIterator<Stream>::operator-- (int dummy)
{
    This r( m_stream, m_index );
    --m_index;
    return r;
}

// add offset
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamIterator<Stream>& PackedStreamIterator<Stream>::operator+= (const sindex_type distance)
{
    m_index += distance;
    return *this;
}

// subtract offset
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamIterator<Stream>& PackedStreamIterator<Stream>::operator-= (const sindex_type distance)
{
    m_index -= distance;
    return *this;
}

// add offset
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamIterator<Stream> PackedStreamIterator<Stream>::operator+ (const sindex_type distance) const
{
    return This( m_stream, m_index + distance );
}

// subtract offset
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamIterator<Stream> PackedStreamIterator<Stream>::operator- (const sindex_type distance) const
{
    return This( m_stream, m_index - distance );
}

// difference
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
typename PackedStreamIterator<Stream>::sindex_type
PackedStreamIterator<Stream>::operator- (const PackedStreamIterator it) const
{
    return sindex_type( m_index - it.m_index );
}

// less than
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool operator< (
    const PackedStreamIterator<Stream>& it1,
    const PackedStreamIterator<Stream>& it2)
{
    return it1.m_index < it2.m_index;
}

// greater than
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool operator>(
    const PackedStreamIterator<Stream>& it1,
    const PackedStreamIterator<Stream>& it2)
{
    return it1.m_index > it2.m_index;
}

// equality test
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool operator== (
    const PackedStreamIterator<Stream>& it1,
    const PackedStreamIterator<Stream>& it2)
{
    return /*it1.m_stream == it2.m_stream &&*/ it1.m_index == it2.m_index;
}
// inequality test
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE bool operator!= (
    const PackedStreamIterator<Stream>& it1,
    const PackedStreamIterator<Stream>& it2)
{
    return /*it1.m_stream != it2.m_stream ||*/ it1.m_index != it2.m_index;
}

// assignment operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamRef<Stream>& PackedStreamRef<Stream>::operator= (const PackedStreamRef& ref)
{
    return (*this = Symbol( ref ));
}

// assignment operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamRef<Stream>& PackedStreamRef<Stream>::operator= (const Symbol s)
{
    m_stream.set( m_index, s );
    return *this;
}

// conversion operator
//
template <typename Stream>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE PackedStreamRef<Stream>::operator Symbol() const
{
    return m_stream.get( m_index );
}

#if defined(__CUDACC__)

#if 0
//
// A utility device function to transpose a set of packed input streams:
//   the symbols of the i-th input stream is supposed to be stored contiguously in the range [offset(i), offset + N(i)]
//   the *words* of i-th output stream will be stored in strided fashion at out_stream[tid, tid + (N(i)+symbols_per_word-1/symbols_per_word) * stride]
//
// The function is warp-synchronous, hence all threads in each warp must be active.
//
// \param stride       output stride
// \param N            length of this thread's string in the input stream
// \param in_offset    offset of this thread's string in the input stream
// \param in_stream    input stream
// \param out_stream   output stream
//
template <uint32 BLOCKDIM, uint32 BITS, bool BIG_ENDIAN_T, typename InStreamIterator, typename OutStreamIterator>
NVBIO_DEVICE
void transpose_packed_streams(const uint32 stride, const uint32 N, const uint32 in_offset, const InStreamIterator in_stream, OutStreamIterator out_stream)
{
    const uint32 WARP_SIZE = cuda::Arch::WARP_SIZE;
    const uint32 NUM_WARPS = BLOCKDIM / WARP_SIZE;

    __shared__ volatile uint32 sm_begin_words[ BLOCKDIM ];
    __shared__ volatile uint32 sm_end_words[ BLOCKDIM ];
    __shared__ volatile uint32 sm_words[ BLOCKDIM * (cuda::Arch::WARP_SIZE+1) ];

    const uint32 SYMBOLS_PER_WORD = (sizeof(uint32)*8) / BITS;
          uint32 begin_word       = in_offset / SYMBOLS_PER_WORD;
          uint32 end_word         = (in_offset + N + SYMBOLS_PER_WORD-1) / SYMBOLS_PER_WORD;
          uint32 word_offset      = in_offset & (SYMBOLS_PER_WORD-1);
          uint32 saved_words      = 0;

    // save the last word
    sm_end_words[ threadIdx.x ] = end_word;

    while (__any( begin_word < end_word ))
    {
        // save the new first word
        sm_begin_words[ threadIdx.x ] = begin_word;

        // use the entire warp to load WARP_SIZE words for each sequence
        for (uint32 t = 0; t < WARP_SIZE; ++t)
        {
            const uint32 src_begin_word  =  sm_begin_words[ warp_id() * WARP_SIZE + t ];
            const uint32 src_end_word    =  sm_end_words[   warp_id() * WARP_SIZE + t ];

            // use the entire warp to load up to WARP_SIZE consecutive words
            if (src_begin_word + warp_tid() < src_end_word)
                sm_words[ warp_tid() * (BLOCKDIM+NUM_WARPS) + (warp_id() * (WARP_SIZE+1) + t) ] = in_stream[ src_begin_word + warp_tid() ];
        }

        if (begin_word < end_word)
        {
            // compute the number of whole words we are going to save out
            const uint32 whole_words = nvbio::min( word_offset ? WARP_SIZE-1 : WARP_SIZE, end_word - begin_word );

            // shift the sequence of bits in shared memory so as to remove the offset before writing them out
            typedef PackedStream<strided_iterator<uint32*>, uint8, BITS, BIG_ENDIAN_T> stream_type;
            stream_type stream( strided_iterator<uint32*>( (uint32*)sm_words + (warp_id() * (WARP_SIZE+1) + warp_tid()), BLOCKDIM+NUM_WARPS ) );

            for (uint32 i = 0; i < whole_words * SYMBOLS_PER_WORD; ++i)
                stream[i] = stream[i + word_offset];

            // save the shifted words to global memory
            for (uint32 i = 0; i < whole_words; ++i)
                out_stream[ stride*(saved_words + i) ] = sm_words[ (warp_id() * (WARP_SIZE+1) + warp_tid()) + i * (BLOCKDIM+NUM_WARPS) ];

            // update the first word we have to start reading from
            begin_word  += whole_words;
            saved_words += whole_words;

            // update the offset in the word we have to start reading from
            word_offset = (word_offset + whole_words * SYMBOLS_PER_WORD) & (SYMBOLS_PER_WORD-1);
        }
    }
}
#else
//
// A utility device function to transpose a set of packed input streams:
//   the symbols of the i-th input stream is supposed to be stored contiguously in the range [offset(i), offset + N(i)]
//   the *words* of i-th output stream will be stored in strided fashion at out_stream[tid, tid + (N(i)+symbols_per_word-1/symbols_per_word) * stride]
//
// The function is warp-synchronous, hence all threads in each warp must be active.
//
// \param stride       output stride
// \param N            length of this thread's string in the input stream
// \param in_offset    offset of this thread's string in the input stream
// \param in_stream    input stream
// \param out_stream   output stream
//
template <uint32 BLOCKDIM, uint32 BITS, bool BIG_ENDIAN_T, typename InStreamIterator, typename OutStreamIterator>
NVBIO_DEVICE
void transpose_packed_streams(const uint32 stride, const uint32 N, const uint32 in_offset, const InStreamIterator in_stream, OutStreamIterator out_stream)
{
    const uint32 SYMBOLS_PER_WORD = (sizeof(uint32)*8) / BITS;
          uint32 begin_word       = in_offset / SYMBOLS_PER_WORD;
          uint32 end_word         = (in_offset + N + SYMBOLS_PER_WORD-1) / SYMBOLS_PER_WORD;
          uint32 word_offset      = in_offset & (SYMBOLS_PER_WORD-1);

    // load the words of the input stream in local memory with a tight loop
    uint32 lmem[64];
    for (uint32 word = begin_word; word < end_word; ++word)
        lmem[word - begin_word] = in_stream[ word ];

    typedef PackedStream<const_cached_iterator<const uint32*>,uint8,BITS,BIG_ENDIAN_T> const_stream_type;
    typedef PackedStream<uint32*,uint8,BITS,BIG_ENDIAN_T>                                    stream_type;

    const_stream_type clmem_stream( &lmem[0] );
    stream_type        lmem_stream( &lmem[0] );

    // shift the symbols in lmem
    for (uint32 i = 0; i < N; ++i)
        lmem_stream[i] = clmem_stream[i + word_offset];

    // save the shifted words to global memory
    for (uint32 i = 0; i < end_word - begin_word; ++i)
        out_stream[ stride*i ] = lmem[ i ];
}
#endif

#endif

} // namespace nvbio
