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

#include <nvbio/basic/strided_iterator.h>
#include <nvbio/basic/packedstream.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

namespace nvbio {
namespace io {

///
/// \page reads_io_page Read data input
/// This module contains a series of classes to load and represent read streams.
/// The idea is that a read stream is an object implementing a simple interface, \ref ReadDataStream,
/// which allows to stream through a file or other set of reads in batches, which are represented in memory
/// with an object inheriting from ReadData.
/// There are several kinds of ReadData containers to keep the reads in the host RAM, or in CUDA device memory.
/// Additionally, the same container can be viewed with different ReadDataView's, in order to allow reinterpreting
/// the base arrays as arrays of different types, e.g. to perform vector loads or use LDG.
///
/// Specifically, it exposes the following classes and methods:
///
/// - ReadData
/// - ReadDataView
/// - ReadDataStream
/// - open_read_file()
///

///@addtogroup IO
///@{

///
///@defgroup ReadsIO Read data input
/// This module contains a series of classes to load and represent read streams.
/// The idea is that a read stream is an object implementing a simple interface, \ref ReadDataStream,
/// which allows to stream through a file or other set of reads in batches, which are represented in memory
/// with an object inheriting from ReadData.
/// There are several kinds of ReadData containers to keep the reads in the host RAM, or in CUDA device memory.
/// Additionally, the same container can be viewed with different ReadDataView's, in order to allow reinterpreting
/// the base arrays as arrays of different types, e.g. to perform vector loads or use LDG.
///@{
///

// describes the quality encoding for a given read file
enum QualityEncoding
{
    // phred quality
    Phred = 0,
    // phred quality + 33
    Phred33 = 1,
    // phred quality + 64
    Phred64 = 2,
    Solexa = 3,
};

// how mates of a paired-end read are encoded
// F = forward, R = reverse
enum PairedEndPolicy
{
    PE_POLICY_FF = 0,
    PE_POLICY_FR = 1,
    PE_POLICY_RF = 2,
    PE_POLICY_RR = 3,
};

///
/// Encodes a read batch
/// this has no storage, it's meant to be a plain data view
///
template <
    typename IndexIterator,
    typename ReadIterator,
    typename QualIterator,
    typename NameIterator>
struct ReadDataView
{
    typedef IndexIterator   index_iterator;
    typedef ReadIterator    read_iterator;
    typedef QualIterator    qual_iterator;
    typedef NameIterator    name_iterator;

    /// empty constructor
    ///
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE
    ReadDataView()
      : m_n_reads(0),
        m_name_stream_len(0),
        m_read_stream_len(0),
        m_read_stream_words(0),
        m_min_read_len(uint32(-1)),
        m_max_read_len(0),
        m_avg_read_len(0)
    {};

    /// copy constructor
    ///
    template <
        typename InIndexIterator,
        typename InReadIterator,
        typename InQualIterator,
        typename InNameIterator>
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE
    ReadDataView(const ReadDataView<InIndexIterator,InReadIterator,InQualIterator,InNameIterator>& in)
      : m_n_reads           (in.m_n_reads),
        m_name_stream       (NameIterator(in.m_name_stream)),
        m_name_stream_len   (in.m_name_stream_len),
        m_name_index        (IndexIterator(in.m_name_index)),
        m_read_stream       (ReadIterator(in.m_read_stream)),
        m_read_stream_len   (in.m_read_stream_len),
        m_read_stream_words (in.m_read_stream_words),
        m_read_index        (IndexIterator(in.m_read_index)),
        m_qual_stream       (QualIterator(in.m_qual_stream)),
        m_min_read_len      (in.m_min_read_len),
        m_max_read_len      (in.m_max_read_len),
        m_avg_read_len      (in.m_avg_read_len)
    {}

    // symbol size for reads
    static const uint32 READ_BITS = 4;
    // big endian?
    static const bool   HI_BITS   = false;

    typedef PackedStream<read_iterator,uint8,READ_BITS,HI_BITS> read_stream_type;

    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE name_iterator  name_stream()     const { return m_name_stream; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE index_iterator name_index()      const { return m_name_index;  }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE read_iterator  read_stream()     const { return m_read_stream; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE index_iterator read_index()      const { return m_read_index;  }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE qual_iterator  qual_stream()     const { return m_qual_stream; }

    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32  size()                    const { return m_n_reads; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32  bps()                     const { return m_read_stream_len; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32  words()                   const { return m_read_stream_words; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32  name_stream_len()         const { return m_name_stream_len; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32  max_read_len()            const { return m_max_read_len; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32  min_read_len()            const { return m_min_read_len; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint32  avg_read_len()            const { return m_avg_read_len; }
    NVBIO_HOST_DEVICE NVBIO_FORCEINLINE uint2   get_range(const uint32 i) const { return make_uint2(m_read_index[i],m_read_index[i+1]); }

public:
    // number of reads in this struct
    uint32             m_n_reads;

    // a pointer to a buffer containing the names of all the reads in this batch
    name_iterator      m_name_stream;
    // the length (in bytes) of the name_stream buffer
    uint32             m_name_stream_len;
    // an array of uint32 with the byte indices of the starting locations of each name in name_stream
    index_iterator     m_name_index;

    // a pointer to a buffer containing the read data
    // note that this could point at either host or device memory
    read_iterator      m_read_stream;
    // the length of read_stream in base pairs
    uint32             m_read_stream_len;
    // the number of words in read_stream
    uint32             m_read_stream_words;
    // an array of uint32 with the indices of the starting locations of each read in read_stream (in base pairs)
    index_iterator     m_read_index;

    // a pointer to a buffer containing quality data
    // (the indices in m_read_index are also valid for this buffer)
    qual_iterator      m_qual_stream;

    // statistics on the reads: minimum size, maximum size, average size
    uint32             m_min_read_len;
    uint32             m_max_read_len;
    uint32             m_avg_read_len;
};

///
/// Base abstract class to encode a host-side read batch.
/// This has no storage, it's meant to be a base for either host or device memory objects
///
struct ReadData : public ReadDataView<uint32*,uint32*,char*,char*>
{
    typedef ReadDataView<uint32*,uint32*,char*,char*> ReadDataBase;

    /// empty constructor
    ///
    ReadData() : ReadDataBase()
    {
        m_name_stream   = NULL;
        m_name_index    = NULL;
        m_read_stream   = NULL;
        m_read_index    = NULL;
        m_qual_stream   = NULL;
    }

    /// virtual destructor
    ///
    virtual ~ReadData() {}

    typedef PackedStream<uint32*,uint8,READ_BITS,HI_BITS>             read_stream_type;
    typedef PackedStream<const uint32*,uint8,READ_BITS,HI_BITS> const_read_stream_type;
};

///
/// a read batch in host memory
///
struct ReadDataRAM : public ReadData
{
    ReadDataRAM();

    /// conversion flags for push_back
    enum {
        REVERSE    = 0x0001,
        COMPLEMENT = 0x0002,
    };

    /// add a read to the end of this batch
    ///
    void push_back(const uint32 in_read_len,
                   const char *name, const uint8 *base_pairs,
                   const uint8 *quality, QualityEncoding q_encoding,
                   uint32 truncate_read_len, uint32 conversion_flags);

    /// signals that the batch is complete
    ///
    void end_batch(void);

    std::vector<uint32> m_read_vec;
    std::vector<uint32> m_read_index_vec;
    std::vector<char>   m_qual_vec;
    std::vector<char>   m_name_vec;
    std::vector<uint32> m_name_index_vec;
};

///
/// a read in device memory
///
struct ReadDataCUDA : public ReadData
{
    enum {
        READS = 0x01,
        QUALS = 0x02,
    };

    /// constructor
    ///
     ReadDataCUDA(const ReadData& host_data, const uint32 flags = READS);

    /// destructor
    ///
    ~ReadDataCUDA();

    uint64 allocated() const { return m_allocated; }

private:
    uint64 m_allocated;
};

///
/// A stream of ReadData, allowing to process the associated
/// reads in batches.
///
struct ReadDataStream
{
    ReadDataStream(uint32 truncate_read_len = uint32(-1))
      : m_truncate_read_len(truncate_read_len)
    {
    };

    /// virtual destructor
    ///
    virtual ~ReadDataStream() {}

    /// next batch
    ///
    virtual ReadData* next(const uint32 batch_size) = 0;

    /// is the stream ok?
    ///
    virtual bool is_ok() = 0;

    // maximum length of a read; longer reads are truncated to this size
    uint32             m_truncate_read_len;
};


/// factory method to open a read file
///
ReadDataStream *open_read_file(const char *          read_file_name,
                               const QualityEncoding qualities,
                               const uint32          max_reads = uint32(-1),
                               const uint32          max_read_len = uint32(-1));

///@} // ReadsIO
///@} // IO

} // namespace io
} // namespace nvbio
