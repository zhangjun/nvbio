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

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <nvbio/basic/mmap.h>
#include <nvbio/basic/deinterleaved_iterator.h>
#include <nvbio/basic/cuda/ldg.h>
#include <nvbio/fmindex/fmindex.h>
#include <nvbio/fmindex/ssa.h>

namespace nvbio {
///@addtogroup IO
///@{
namespace io {
///@}

///
/// \page fmindex_io_page FM-Index I/O
/// This module contains a series of classes to load FM-indices from disk into:
///  - RAM
///  - mapped memory
///  - CUDA device memory
///
/// Specifically, it exposes the following classes:
///
/// - FMIndexData
/// - FMIndexDataRAM
/// - FMIndexDataMMAPServer
/// - FMIndexDataMMAP
/// - FMIndexDataCUDA
/// - FMIndexIterators
/// - FMIndexLdgIterators
///

///@addtogroup IO
///@{

///
///@defgroup FMIndexIO FM-Index I/O
/// This module contains a series of classes to load FM-indices from disk into:
///  - RAM
///  - mapped memory
///  - CUDA device memory
///@{
///

struct BNTInfo
{
	uint32  n_seqs;             ///< number of sequences
	uint32  seed;               ///< random seed
	uint32  n_holes;            ///< number of holes
    uint32  names_len;          ///< length of the names vector
    uint32  annos_len;          ///< length of the annotations vector
};
struct BNTAnn
{
    uint32  name_offset;        ///< offset in the names vector
    uint32  anno_offset;        ///< offset in the annotation vector
    int64   offset;             ///< offset in the global sequence
	int32   len;                ///< length of the sequence
	int32   n_ambs;             ///< number of ambiguities
	uint32  gi;                 ///< global index
    uint32  pad;                ///< extra padding field
};
struct BNTAmb
{
	int64   offset;             ///< offset in the global vector
	int32   len;                ///< length
	char    amb;                ///< ambiguous character
};
struct BNTSeqPOD
{
    char*   names;              ///< names strings vector
    char*   annos;              ///< annotation strings vector
    BNTAnn* anns;               ///< annotations vector, n_seqs elements
    BNTAmb* ambs;               ///< ambiguities vector, n_holes elements
};
struct BNTSeqVec
{
    std::vector<char>   names;  ///< names strings vector
    std::vector<char>   annos;  ///< annotation strings vector
    std::vector<BNTAnn> anns;   ///< annotations vector, n_seqs elements
    std::vector<BNTAmb> ambs;   ///< ambiguities vector, n_holes elements
};

///
/// Basic FM-index interface.
///
/// This class holds pointers to data that is typically going to be allocated/loaded/deallocated
/// by inheriting classes.
/// The idea is that accessing this basic information is fast and requires no virtual function
/// calls.
struct FMIndexData
{
    static const uint32 GENOME  = 0x01;
    static const uint32 FORWARD = 0x02;
    static const uint32 REVERSE = 0x04;
    static const uint32 SA      = 0x10;

    static const uint32 READ_BITS = 4;
    static const uint32 OCC_INT = 64;
    static const uint32 SA_INT  = 16;

    typedef PackedStream<const uint32*,uint8,2,true>          stream_type;
    typedef PackedStream<      uint32*,uint8,2,true> nonconst_stream_type;

    typedef SSA_index_multiple<SA_INT> SSA_type;
    typedef SSA_type::context_type     SSA_context;

    typedef rank_dictionary<2u,OCC_INT,stream_type,const uint32*,const uint32*> rank_dict_type;
    typedef fm_index<rank_dict_type, SSA_type::context_type>                    fm_index_type;

             FMIndexData();                                                 ///< empty constructor
    virtual ~FMIndexData() {}                                               ///< virtual destructor
    
    uint32        flags()         const { return m_flags; }                 ///< return loading flags
    uint32        genome_length() const { return seq_length; }              ///< return genome length
    bool          has_genome()    const { return m_genome_stream != NULL; } ///< return whether the genome is present
    bool          has_ssa()       const { return ssa.m_ssa != NULL; }       ///< return whether the sampled suffix array is present
    bool          has_rssa()      const { return rssa.m_ssa != NULL; }      ///< return whether the reverse sampled suffix array is present
    const uint32* genome_stream() const { return m_genome_stream; }         ///< return the genome stream
    const uint32*  bwt_stream()   const { return m_bwt_stream; }            ///< return the BWT stream
    const uint32* rbwt_stream()   const { return m_rbwt_stream; }           ///< return the reverse BWT stream
    const uint32*  occ_stream()   const { return m_occ; }                   ///< return the occurrence table
    const uint32* rocc_stream()   const { return m_rocc; }                  ///< return the reverse occurrence table

    uint32             m_flags;
    uint32             seq_length;
    uint32             seq_words;
    uint32             occ_words;
    uint32             sa_words;
    uint32              primary;
    uint32             rprimary;
    uint32*            m_genome_stream;
    uint32*            m_bwt_stream;
    uint32*            m_rbwt_stream;
    uint32*            m_occ;
    uint32*            m_rocc;
    uint32*             L2;
    uint32*            rL2;
    uint32*            count_table;
    SSA_context        ssa;
    SSA_context        rssa;

    BNTInfo            m_bnt_info;
    BNTSeqPOD          m_bnt_data;
};

void init_ssa(
    const FMIndexData&       driver_data,
    FMIndexData::SSA_type&   ssa,
    FMIndexData::SSA_type&   rssa);

///
/// An in-RAM FM-index.
///
struct FMIndexDataRAM : public FMIndexData
{
    /// load a genome from file
    ///
    /// \param genome_prefix            prefix file name
    /// \param flags                    loading flags specifying which elements to load
    int load(
        const char* genome_prefix,
        const uint32 flags = GENOME | FORWARD | REVERSE | SA);

    std::vector<uint32> m_genome_stream_vec;
    std::vector<uint32> m_bwt_stream_vec;
    std::vector<uint32> m_rbwt_stream_vec;
    std::vector<uint32> m_occ_vec;
    std::vector<uint32> m_rocc_vec;

    uint32              m_L2[5];
    uint32              m_rL2[5];
    uint32              m_count_table[256];

    std::vector<uint32> m_ssa_vec;
    std::vector<uint32> m_rssa_vec;

    BNTSeqVec           m_bnt_vec;
};

struct FMIndexDataMMAPInfo
{
    uint32  sequence_length;
    uint32  sequence_words;
    uint32  occ_words;
    uint32  sa_words;
    uint32  primary;
    uint32  rprimary;
    uint32  L2[5];
    uint32  rL2[5];
    BNTInfo bnt;
};

///
/// A memory-mapped FM-index server, which can load an FM-index from disk and map it to
/// a shared memory arena.
///
struct FMIndexDataMMAPServer : public FMIndexData
{
    typedef FMIndexDataMMAPInfo Info;

    /// load a genome from file
    ///
    /// \param genome_prefix            prefix file name
    /// \param mapped_name              memory mapped object name
    int load(
        const char* genome_prefix, const char* mapped_name);

private:
    Info             m_info;                         ///< internal info object storage
    ServerMappedFile m_info_file;                    ///< internal memory-mapped info object server
    ServerMappedFile m_pac_file;                     ///< internal memory-mapped genome object server
    ServerMappedFile m_occ_file;                     ///< internal memory-mapped forward occurrence table object server
    ServerMappedFile m_rocc_file;                    ///< internal memory-mapped reverse occurrence table object server
    ServerMappedFile m_bwt_file;                     ///< internal memory-mapped forward BWT object server
    ServerMappedFile m_rbwt_file;                    ///< internal memory-mapped reverse BWT object server
    ServerMappedFile m_sa_file;                      ///< internal memory-mapped forward SSA table object server
    ServerMappedFile m_rsa_file;                     ///< internal memory-mapped reverse SSA table object server
    ServerMappedFile m_bnt_file;                     ///< internal memory-mapped BNT object server
};

///
/// A memory-mapped FM-index client, which can connect to a shared-memory FM-index
/// and present it as local.
///
struct FMIndexDataMMAP : public FMIndexData
{
    typedef FMIndexDataMMAPInfo Info;

    /// load from a memory mapped object
    ///
    /// \param genome_name          memory mapped object name
    int load(
        const char*  genome_name);

    MappedFile          m_genome_file;                  ///< internal memory-mapped genome object
    MappedFile          m_bwt_file;                     ///< internal memory-mapped forward BWT object
    MappedFile          m_rbwt_file;                    ///< internal memory-mapped reverse BWT object
    MappedFile          m_occ_file;                     ///< internal memory-mapped forward occurrence table object
    MappedFile          m_rocc_file;                    ///< internal memory-mapped reverse occurrence table object
    MappedFile          m_sa_file;                      ///< internal memory-mapped forward SSA table object
    MappedFile          m_rsa_file;                     ///< internal memory-mapped reverse SSA table object
    MappedFile          m_info_file;                    ///< internal memory-mapped info object
    MappedFile          m_bnt_file;                     ///< internal memory-mapped BNT object

    uint32              m_L2[5];                        ///< local storage for the forward L2 table
    uint32              m_rL2[5];                       ///< local storage for the reverse L2 table
    uint32              m_count_table[256];             ///< local storage for the BWT counting table
};

#define FUSED_BWT_OCC


///
/// A device-side FM-index - which can take a host memory FM-index and map it to
/// device memory.
///
struct FMIndexDataCUDA : public FMIndexData
{
    typedef SSA_index_multiple_device<SA_INT>               SSA_device_type;

    static const uint32 GENOME  = 0x01;
    static const uint32 FORWARD = 0x02;
    static const uint32 REVERSE = 0x04;
    static const uint32 SA      = 0x10;

    /// load a host-memory FM-index in device memory
    ///
    /// \param host_data                                host-memory FM-index to load
    /// \param flags                                    specify which parts of the FM-index to load
     FMIndexDataCUDA(const FMIndexData& host_data, const uint32 flags = GENOME | FORWARD | REVERSE);
    ~FMIndexDataCUDA();                                 ///< destructor

    uint64 allocated() const { return m_allocated; }    ///< return the amount of allocated device memory

    const uint32*  bwt_occ() const { return thrust::raw_pointer_cast( &m_bwt_occ.front() ); }  ///< return the fused forward BWT & occurrence tables
    const uint32* rbwt_occ() const { return thrust::raw_pointer_cast( &m_rbwt_occ.front() ); } ///< return the fused reverse BWT & occurrence tables

private:
    uint64                        m_allocated;          ///< # of allocated device memory bytes
    thrust::device_vector<uint32> m_bwt_occ;            ///< fused forward BWT & occurrence table storage
    thrust::device_vector<uint32> m_rbwt_occ;           ///< fused reverse BWT & occurrence table storage
};

/// initialize the sampled suffix arrays on the GPU given a device-side FM-index.
///
void init_ssa(
    const FMIndexDataCUDA&              driver_data,
    FMIndexDataCUDA::SSA_device_type&   ssa,
    FMIndexDataCUDA::SSA_device_type&   rssa);

#if defined(__CUDACC__)
struct FMIndexLdgIterators
{
    typedef cuda::ldg_pointer<uint4>                            bwt_occ_type;
    typedef deinterleaved_iterator<2,0,bwt_occ_type>            bwt_type;
    typedef deinterleaved_iterator<2,1,bwt_occ_type>            occ_type;
    typedef cuda::ldg_pointer<uint32>                           count_table_type;
    typedef cuda::ldg_pointer<uint32>                           ssa_ldg_type;

    typedef rank_dictionary<
        2u,
        FMIndexDataCUDA::OCC_INT,
        PackedStream<bwt_type,uint8,2u,true>,
        occ_type,
        count_table_type>                                       rank_dict_type;

    typedef SSA_index_multiple_context<
        FMIndexDataCUDA::SA_INT,
        ssa_ldg_type>                                           ssa_type;

    FMIndexLdgIterators(const FMIndexDataCUDA& driver_data) : m_driver_data( driver_data ) {}

    occ_type  occ_iterator() { return occ_type(bwt_occ_type((const uint4*) m_driver_data.bwt_occ())); }
    occ_type rocc_iterator() { return occ_type(bwt_occ_type((const uint4*)m_driver_data.rbwt_occ())); }

    bwt_type  bwt_iterator() { return bwt_type(bwt_occ_type((const uint4*) m_driver_data.bwt_occ())); }
    bwt_type rbwt_iterator() { return bwt_type(bwt_occ_type((const uint4*)m_driver_data.rbwt_occ())); }

    ssa_type  ssa_iterator() { return ssa_type(ssa_ldg_type( m_driver_data.ssa.m_ssa)); }
    ssa_type rssa_iterator() { return ssa_type(ssa_ldg_type(m_driver_data.rssa.m_ssa)); }

    count_table_type count_table() { return count_table_type( m_driver_data.count_table ); }

    rank_dict_type  rank_dict() { return rank_dict_type(  bwt_iterator(),  occ_iterator(), count_table() ); }
    rank_dict_type rrank_dict() { return rank_dict_type( rbwt_iterator(), rocc_iterator(), count_table() ); }

    const FMIndexDataCUDA& m_driver_data;
};
#else
struct FMIndexLdgIterators
{
    typedef cuda::ldg_pointer<uint4>                            bwt_type;
    typedef cuda::ldg_pointer<uint4>                            occ_type;
    typedef cuda::ldg_pointer<uint32>                           count_table_type;
    typedef cuda::ldg_pointer<uint32>                           ssa_ldg_type;

    typedef rank_dictionary<
        2u,
        FMIndexDataCUDA::OCC_INT,
        PackedStream<bwt_type,uint8,2u,true>,
        occ_type,
        count_table_type>                                       rank_dict_type;

    typedef SSA_index_multiple_context<
        FMIndexDataCUDA::SA_INT,
        ssa_ldg_type>                                           ssa_type;

    FMIndexLdgIterators(const FMIndexDataCUDA& driver_data) : m_driver_data( driver_data ) {}

    occ_type  occ_iterator() { return occ_type((const uint4*) m_driver_data.occ_stream()); }
    occ_type rocc_iterator() { return occ_type((const uint4*)m_driver_data.rocc_stream()); }

    bwt_type  bwt_iterator() { return bwt_type((const uint4*) m_driver_data.bwt_stream()); }
    bwt_type rbwt_iterator() { return bwt_type((const uint4*)m_driver_data.rbwt_stream()); }

    ssa_type  ssa_iterator() { return ssa_type(ssa_ldg_type( m_driver_data.ssa.m_ssa)); }
    ssa_type rssa_iterator() { return ssa_type(ssa_ldg_type(m_driver_data.rssa.m_ssa)); }

    count_table_type count_table() { return count_table_type( m_driver_data.count_table ); }

    rank_dict_type  rank_dict() { return rank_dict_type(  bwt_iterator(),  occ_iterator(), count_table() ); }
    rank_dict_type rrank_dict() { return rank_dict_type( rbwt_iterator(), rocc_iterator(), count_table() ); }

    const FMIndexDataCUDA& m_driver_data;
};
#endif // __CUDACC__

struct FMIndexIterators
{
    typedef const uint4*                occ_type;
    typedef const uint4*                bwt_type;
    typedef const uint32*               count_table_type;
    typedef FMIndexData::SSA_context    ssa_type;

    typedef rank_dictionary<
        2u,
        FMIndexDataCUDA::OCC_INT,
        PackedStream<bwt_type,uint8,2u,true>,
        occ_type,
        count_table_type>               rank_dict_type;

    FMIndexIterators(const FMIndexData& driver_data) : m_driver_data( driver_data ) {}

    occ_type  occ_iterator() { return occ_type(m_driver_data.occ_stream()); }
    occ_type rocc_iterator() { return occ_type(m_driver_data.rocc_stream()); }

    bwt_type  bwt_iterator() { return bwt_type(m_driver_data.bwt_stream()); }
    bwt_type rbwt_iterator() { return bwt_type(m_driver_data.rbwt_stream()); }

    ssa_type  ssa_iterator() { return m_driver_data.ssa; }
    ssa_type rssa_iterator() { return m_driver_data.rssa; }

    count_table_type count_table() { return m_driver_data.count_table; }

    rank_dict_type  rank_dict() { return rank_dict_type(  bwt_iterator(),  occ_iterator(), count_table() ); }
    rank_dict_type rrank_dict() { return rank_dict_type( rbwt_iterator(), rocc_iterator(), count_table() ); }

    const FMIndexData& m_driver_data;
};

///@} // FMIndexIO
///@} // IO

} // namespace io
} // namespace nvbio
