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

// fmindex_test.cpp
//

#define MOD_NAMESPACE
#define MOD_NAMESPACE_NAME fmitest
#define MOD_NAMESPACE_BEGIN namespace fmitest {
#define MOD_NAMESPACE_END   }

//#define NVBIO_CUDA_DEBUG
//#define NVBIO_CUDA_ASSERTS

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <nvbio/basic/timer.h>
#include <nvbio/basic/console.h>
#include <nvbio/basic/vector_wrapper.h>
#include <nvbio/fmindex/dna.h>
#include <nvbio/basic/cached_iterator.h>
#include <nvbio/basic/packedstream.h>
#include <nvbio/basic/deinterleaved_iterator.h>
#include <nvbio/fmindex/bwt.h>
#include <nvbio/fmindex/ssa.h>
#include <nvbio/fmindex/fmindex.h>
#include <nvbio/fmindex/backtrack.h>
#include <nvbio/io/fmi.h>
#include <nvbio/io/reads/reads.h>

using namespace nvbio;

struct ssa_nop {};

namespace { // anonymous namespace

template <uint32 OCC_INTERVAL,typename FMIndexType, typename word_type>
__global__ void locate_kernel(
    const uint32        n_queries,
    const uint32        QUERY_LEN,
    const uint32        genome_length,
    const word_type*    genome_stream,
    const FMIndexType   fmi,
    const uint32*       input,
    uint32*             output)
{
    typedef typename FMIndexType::index_type index_type;
    typedef typename FMIndexType::range_type range_type;

    const uint32 thread_id = threadIdx.x + blockIdx.x*blockDim.x;
    if (thread_id >= n_queries)
        return;

    typedef PackedStream<const_cached_iterator<const word_type*>,uint8,2,true,index_type> GenomeStream;
    const_cached_iterator<const word_type*> cached_base_stream( genome_stream );
    GenomeStream genome( cached_base_stream );

    const range_type range = match(
        fmi,
        genome.begin() + input[ thread_id ],
        QUERY_LEN );

    output[ thread_id ] = uint32( locate( fmi, range.x ) );
}

// test the gpu SSA against the cpu one
template <typename SSA_device, typename SSA_host>
void test_ssa(
    const SSA_device&   ssa_device,
    const SSA_host&     ssa)
{
    thrust::host_vector<typename SSA_device::value_type> d_ssa = ssa_device.m_ssa;
    for (uint32 i = 0; i < d_ssa.size(); ++i)
    {
        if (d_ssa[i] != ssa.m_ssa[i])
        {
            fprintf(stderr, "  \nerror : expected SSA[%u] = %u, got: %u\n", i, (uint32)ssa.m_ssa[i], (uint32)d_ssa[i]);
            exit(1);
        }
    }
}

template <typename index_type>
struct HostData
{
    uint32                              primary;
    thrust::host_vector<index_type>     text;
    thrust::host_vector<index_type>     bwt;
    thrust::host_vector<index_type>     occ;
    thrust::host_vector<index_type>     bwt_occ;
    thrust::host_vector<index_type>     L2;
    thrust::host_vector<uint32>         count_table;
    thrust::host_vector<uint32>         input;
    thrust::host_vector<uint32>         output;
};
template <typename index_type>
struct DeviceData
{
    uint32                               primary;
    thrust::device_vector<index_type>   text;
    thrust::device_vector<index_type>   bwt;
    thrust::device_vector<index_type>   occ;
    thrust::device_vector<index_type>   bwt_occ;
    thrust::device_vector<index_type>   L2;
    thrust::device_vector<uint32>       count_table;
    thrust::device_vector<uint32>       input;
    thrust::device_vector<uint32>       output;

    DeviceData(const HostData<index_type>& data) :
        primary( data.primary ),
        text( data.text ),
        bwt( data.bwt ),
        occ( data.occ ),
        bwt_occ( data.bwt_occ ),
        L2( data.L2 ),
        count_table( data.count_table ),
        input( data.input ),
        output( data.output ) {}
};

template <uint32 OCC_INT, uint32 SA_INT, typename BwtIterator, typename OccIterator, typename SSA, typename index_type>
void do_synthetic_test_device(
    const uint32                    REQS,
    const uint32                    LEN,
    const uint32                    PLEN,
    const HostData<index_type>&     host_data,
    const SSA&                      ssa,
          DeviceData<index_type>&   device_data,
    const OccIterator               occ_it,
    const BwtIterator               bwt_it)
{
    typedef cuda::ldg_pointer<uint32> count_table_type;
    const count_table_type count_table( thrust::raw_pointer_cast( &device_data.count_table.front() ) );

    typedef PackedStream<BwtIterator,uint8,2u,true,index_type> bwt_type;
    typedef rank_dictionary< 2u, OCC_INT, bwt_type, OccIterator, count_table_type > rank_dict_type;
    rank_dict_type rank_dict(
        bwt_it,
        occ_it,
        count_table );

    typedef SSA_index_multiple_context<SA_INT,const index_type*> ssa_type;
    typedef fm_index< rank_dict_type, ssa_type > fm_index_type;
    fm_index_type temp_fmi(
        LEN,
        device_data.primary,
        thrust::raw_pointer_cast( &device_data.L2.front() ),
        rank_dict,
        ssa_type() );

    //SSA_value_multiple_device ssa_device( ssa );
    //SSA_index_multiple_device<SA_INT> ssa_device( ssa );

    fprintf(stderr,  "    SSA gpu... started\n" );
    Timer timer;
    timer.start();

    SSA_index_multiple_device<SA_INT,index_type> ssa_device( temp_fmi );

    timer.stop();
    fprintf(stderr,  "    SSA gpu... done: %.3fs\n", timer.seconds() );

    // test the gpu SSA against the cpu one
    test_ssa( ssa_device, ssa );

    fprintf(stderr, "    gpu alignment... started\n");

    fm_index_type fmi(
        LEN,
        device_data.primary,
        thrust::raw_pointer_cast( &device_data.L2.front() ),
        rank_dict,
        ssa_device.get_context() );

    cudaEvent_t start, stop;
    cudaEventCreate( &start );
    cudaEventCreate( &stop );

    cudaEventRecord( start, 0 );

    const uint32 BLOCK_SIZE = 256;
    const uint32 n_blocks = (REQS + BLOCK_SIZE-1) / BLOCK_SIZE;

    locate_kernel<OCC_INT> <<<n_blocks,BLOCK_SIZE>>>(
        REQS,
        PLEN,
        LEN,
        thrust::raw_pointer_cast( &device_data.text.front() ),
        fmi,
        thrust::raw_pointer_cast( &device_data.input.front() ),
        thrust::raw_pointer_cast( &device_data.output.front() ) );

    cudaThreadSynchronize();

    float time;
    cudaEventRecord( stop, 0 );
    cudaEventSynchronize( stop );
    cudaEventElapsedTime( &time, start, stop );

    fprintf(stderr, "    gpu alignment... done: %.1fms, A/s: %.2f M\n", time, REQS/(time*1000.0f) );

    thrust::host_vector<uint32> output_h( device_data.output );

    for (uint32 i = 0; i < REQS; ++i)
    {
        if (host_data.output[i] != output_h[i])
        {
            fprintf(stderr, "\nerror : mismatch at %u: expected %u, got %u\n", i, host_data.output[i], output_h[i] );
            exit(1);
        }
    }
}

template <uint32 OCC_INT, uint32 SA_INT, typename SSA>
void synthetic_test_device(
    const uint32            REQS,
    const uint32            LEN,
    const uint32            PLEN,
    const uint32            WORDS,
    const uint32            OCC_WORDS,
    const HostData<uint32>& host_data,
    const SSA&              ssa)
{
    try
    {
        DeviceData<uint32> device_data( host_data );

        // test an FM-index with separate bwt/occ tables
        {
            typedef cuda::ldg_pointer<uint4> iterator_type;

            iterator_type occ_it( (const uint4*)thrust::raw_pointer_cast( &device_data.occ.front() ) );
            iterator_type bwt_it( (const uint4*)thrust::raw_pointer_cast( &device_data.bwt.front() ) );

            do_synthetic_test_device<OCC_INT, SA_INT>(
                REQS,
                LEN,
                PLEN,
                host_data,
                ssa,
                device_data,
                occ_it,
                bwt_it );
        }

        // test an FM-index with interleaved bwt/occ tables
        if (WORDS == OCC_WORDS)
        {
            typedef cuda::ldg_pointer<uint4> bwt_occ_texture;
            bwt_occ_texture bwt_occ_tex( (const uint4*)thrust::raw_pointer_cast( &device_data.bwt_occ.front() ) );

            typedef deinterleaved_iterator<2,0,bwt_occ_texture> bwt_iterator;
            typedef deinterleaved_iterator<2,1,bwt_occ_texture> occ_iterator;

            occ_iterator occ_it( bwt_occ_tex );
            bwt_iterator bwt_it( bwt_occ_tex );

            do_synthetic_test_device<OCC_INT, SA_INT>(
                REQS,
                LEN,
                PLEN,
                host_data,
                ssa,
                device_data,
                occ_it,
                bwt_it );
        }
    }
    catch (std::exception exception)
    {
        fprintf(stderr, "  \nerror : exception caught : %s\n", exception.what());
        exit(1);
    }
    catch (...)
    {
        fprintf(stderr, "  \nerror : unknown exception\n");
        exit(1);
    }
}

template <uint32 OCC_INT, uint32 SA_INT, typename SSA>
void synthetic_test_device(
    const uint32            REQS,
    const uint32            LEN,
    const uint32            PLEN,
    const uint32            WORDS,
    const uint32            OCC_WORDS,
    const HostData<uint64>& host_data,
    const SSA&              ssa)
{
    try
    {
        DeviceData<uint64> device_data( host_data );

        // test an FM-index with separate bwt/occ tables
        {
            typedef cuda::ldg_pointer<uint64> iterator_type;

            iterator_type occ_it( (const uint64*)thrust::raw_pointer_cast( &device_data.occ.front() ) );
            iterator_type bwt_it( (const uint64*)thrust::raw_pointer_cast( &device_data.bwt.front() ) );

            do_synthetic_test_device<OCC_INT, SA_INT>(
                REQS,
                LEN,
                PLEN,
                host_data,
                ssa,
                device_data,
                occ_it,
                bwt_it );
        }

        // test an FM-index with interleaved bwt/occ tables
        if (WORDS == OCC_WORDS)
        {
            typedef cuda::ldg_pointer<uint64> bwt_occ_texture;
            bwt_occ_texture bwt_occ_tex( (const uint64*)thrust::raw_pointer_cast( &device_data.bwt_occ.front() ) );

            typedef deinterleaved_iterator<2,0,bwt_occ_texture> bwt_iterator;
            typedef deinterleaved_iterator<2,1,bwt_occ_texture> occ_iterator;

            occ_iterator occ_it( bwt_occ_tex );
            bwt_iterator bwt_it( bwt_occ_tex );

            do_synthetic_test_device<OCC_INT, SA_INT>(
                REQS,
                LEN,
                PLEN,
                host_data,
                ssa,
                device_data,
                occ_it,
                bwt_it );
        }
    }
    catch (std::exception exception)
    {
        fprintf(stderr, "  \nerror : exception caught : %s\n", exception.what());
        exit(1);
    }
    catch (...)
    {
        fprintf(stderr, "  \nerror : unknown exception\n");
        exit(1);
    }
}

// perform an alignment test on the cpu
//
template <
    typename TextType,
    typename FMIndexType,
    typename index_type>
void synthetic_test_host(
    const uint32                REQS,
    const uint32                PLEN,
    const TextType              text,
    const FMIndexType           fmi,
          HostData<index_type>& data)
{
    fprintf(stderr, "    cpu alignment... started" );

    typedef typename FMIndexType::range_type range_type;

    Timer timer;
    timer.start();
    for (uint32 i = 0; i < REQS; ++i)
    {
        if ((i & 1023) == 0)
            fprintf(stderr, "\r    cpu alignment... started:  %.1f%%   ", 100.0f*float(i)/float(REQS) );
        const range_type range = match(
            fmi,
            text.begin() + data.input[i],
            PLEN );

        if (range.y < range.x)
        {
            fprintf(stderr, "  \nerror: unable to match pattern %u\n", data.input[i]);
            exit(1);
        }
        data.output[i] = uint32( locate( fmi, range.x ) );
    }
    timer.stop();

    fprintf(stderr, "\n    cpu alignment... done: %.1fms, A/s: %.2f M\n", timer.seconds()*1000.0f, REQS/(timer.seconds()*1.0e6f) );
}

} // anonymous namespace

template <typename index_type>
void synthetic_test(const uint32 LEN, const uint32 QUERIES)
{
    fprintf(stderr, "  %u-bits synthetic test\n", uint32(sizeof(index_type)*8));

    const uint32 OCC_INT = sizeof(index_type) == sizeof(uint32) ? 64 : 128;
    const uint32 SA_INT  = 32;

    const uint32 SYM_PER_WORD = 4*sizeof(index_type);

    const uint32 PLEN      = 8;
    const uint32 REQS      = nvbio::min( uint32(LEN-PLEN-1u), QUERIES );
    const uint32 WORDS     = (LEN+SYM_PER_WORD-1)/SYM_PER_WORD;
    const uint32 OCC_WORDS = ((LEN+OCC_INT-1) / OCC_INT) * 4;

    Timer timer;

    const uint64 memory_footprint =
        sizeof(index_type)*WORDS +
        sizeof(index_type)*WORDS +
        sizeof(index_type)*OCC_WORDS +
        sizeof(index_type)*uint64(LEN+SA_INT)/SA_INT;

    fprintf(stderr, "  memory  : %.1f MB\n", float(memory_footprint)/float(1024*1024));

    HostData<index_type> data;
    data.text.resize( align<4>(WORDS),      0u );
    data.bwt.resize(  align<4>(WORDS),      0u );
    data.occ.resize(  align<4>(OCC_WORDS),  0u );
    data.L2.resize( 5 );
    data.count_table.resize( 256 );
    data.input.resize( REQS );
    data.output.resize( REQS );

    typedef PackedStream<index_type*,uint8,2,true,index_type> stream_type;
    stream_type text( &data.text[0] );

    for (uint32 i = 0; i < LEN; ++i)
        text[i] = (rand() % 4);

    // print the string
    if (LEN < 64)
    {
        char string[64];
        dna_to_string(
            text.begin(),
            text.begin() + LEN,
            string );

        fprintf(stderr, "  string : %s\n", string);
    }

    // generate the suffix array
    std::vector<int32> sa( LEN+1, 0u );

    gen_sa( LEN, text.begin(), &sa[0] );

    stream_type bwt( &data.bwt[0] );

    data.primary = gen_bwt_from_sa( LEN, text.begin(), &sa[0], bwt.begin() );

    // set sa[0] to -1 so as to get a modulo for free
    sa[0] = -1;

    // print the string
    if (LEN < 64)
    {
        char string[64];
        dna_to_string(
            bwt.begin(),
            bwt.begin() + LEN,
            string );

        fprintf(stderr, "  bwt    : %s\n", string);
    }
    fprintf(stderr,"  primary : %d\n", data.primary );

    // buld the occurrence table
    build_occurrence_table<OCC_INT>(
        bwt.begin(),
        bwt.begin() + LEN,
        &data.occ[0],
        &data.L2[1] );

    // transform the L2 table into a cumulative sum
    data.L2[0] = 0;
    for (uint32 c = 0; c < 4; ++c)
        data.L2[c+1] += data.L2[c];

    // print the L2
    if (LEN < 64)
    {
        for (uint32 i = 0; i < 5; ++i)
            fprintf(stderr, "  L2[%u] : %u\n", i, data.L2[i]);
    }

    // generate the count table
    gen_bwt_count_table( &data.count_table[0] );

    // build the interleaved bwt/occ array
    if (WORDS == OCC_WORDS)
    {
        fprintf(stderr,  "  building interleaved bwt/occ... started\n" );

        data.bwt_occ.resize( WORDS*2 );
        if (sizeof(index_type) == 4)
        {
            for (uint32 w = 0; w < WORDS; w += 4)
            {
                data.bwt_occ[ w*2+0 ] = data.bwt[ w+0 ];
                data.bwt_occ[ w*2+1 ] = data.bwt[ w+1 ];
                data.bwt_occ[ w*2+2 ] = data.bwt[ w+2 ];
                data.bwt_occ[ w*2+3 ] = data.bwt[ w+3 ];
                data.bwt_occ[ w*2+4 ] = data.occ[ w+0 ];
                data.bwt_occ[ w*2+5 ] = data.occ[ w+1 ];
                data.bwt_occ[ w*2+6 ] = data.occ[ w+2 ];
                data.bwt_occ[ w*2+7 ] = data.occ[ w+3 ];
            }
        }
        else
        {
            for (uint32 w = 0; w < WORDS; ++w)
            {
                data.bwt_occ[ w*2+0 ] = data.bwt[ w ];
                data.bwt_occ[ w*2+1 ] = data.occ[ w ];
            }
        }
        fprintf(stderr,  "  building interleaved bwt/occ... done\n" );
    }

    typedef PackedStream<const index_type*,uint8,2u,true,index_type> bwt_type;
    typedef rank_dictionary<2u, OCC_INT, bwt_type, const index_type*, const uint32*> rank_dict_type;

    typedef fm_index<rank_dict_type, ssa_nop> temp_fm_index_type;
    temp_fm_index_type temp_fmi(
        LEN,
        data.primary,
        &data.L2[0],
        rank_dict_type(
            &data.bwt[0],
            &data.occ[0],
            &data.count_table[0] ),
        ssa_nop() );

  #if 0
    // test the Sampled Suffix Array class
    typedef SSA_value_multiple SSA_type;

    SSA_value_multiple ssa( temp_fmi, SA_INT );
    SSA_value_multiple::context_type ssa_context = ssa.get_context();
  #else
    // test the Sampled Suffix Array class
    typedef SSA_index_multiple<SA_INT,index_type> SSA_type;

    timer.start();

    SSA_type ssa( temp_fmi );

    timer.stop();
    fprintf(stderr, "  SSA cpu time: %.3fs\n", timer.seconds() );

    typename SSA_type::context_type ssa_context = ssa.get_context();
  #endif

    fprintf(stderr, "  SSA test... started\n" );
    for (uint32 i = 1; i < LEN; ++i)
    {
        index_type val;
        if (ssa_context.fetch( index_type(i), val ) && (val != (uint32)sa[i]))
        {
            fprintf(stderr, "  SSA mismatch at %u: expected %d, got: %u\n", i, sa[i], val);
            exit(1);
        }
    }
    fprintf(stderr, "  SSA test... done\n" );

    typedef fm_index<rank_dict_type, typename SSA_type::context_type> fm_index_type;
    fm_index_type fmi(
        LEN,
        data.primary,
        &data.L2[0],
        rank_dict_type(
            &data.bwt[0],
            &data.occ[0],
            &data.count_table[0] ),
        ssa_context );

    typedef typename fm_index_type::range_type range_type;

    uint8 pattern[PLEN];
    char  pattern_str[PLEN+1];

    fprintf(stderr, "  alignment test... started:" );
    for (uint32 i = 0; i < 1000; ++i)
    {
        fprintf(stderr, "\r  alignment test... started:  %.1f%%   ", 100.0f*float(i)/1000.0f );
        for (uint32 j = 0; j < PLEN; ++j)
            pattern[j] = text[i+j];

        dna_to_string(
            pattern,
            pattern + PLEN,
            pattern_str );

        range_type range = match(
            fmi,
            pattern,
            PLEN );

        if (range.x > range.y)
        {
            fprintf(stderr, "  \nerror : searching for %s @ %u, resulted in (%u,%u)\n", pattern_str, i, range.x, range.y);
            exit(1);
        }

        // locate the first 100 alignments
        range.y = nvbio::min( range.x + 10u, range.y );

        for (index_type x = range.x; x <= range.y; ++x)
        {
            const uint32 prefix = locate( fmi, x );
            if (prefix >= LEN)
            {
                const range_type inv = inv_psi( fmi, x );
                fprintf(stderr, "  \nerror : searching for %s @ %u, resulted in prefix out of bounds: %u (= sa[%u] + %u)\n", pattern_str, i, prefix, uint32(inv.x), uint32(inv.y));
                exit(1);
            }

            char found_str[PLEN+1];
            dna_to_string(
                text.begin() + prefix,
                text.begin() + prefix + PLEN,
                found_str );

            if (strcmp( found_str, pattern_str ) != 0)
            {
                const range_type inv = inv_psi( fmi, x );
                fprintf(stderr, "  \nerror : locating %s @ %u at SA=%u in SA(%u,%u), resulted in %s @ %u (= sa[%u] + %u)\n", pattern_str, i, x, range.x, range.y, found_str, prefix, uint32(inv.x), uint32(inv.y));
                exit(1);
            }
            /*{
                const uint2 inv = inv_psi( fmi, x );
                fprintf(stderr, "  locating %s @ %u at %u, matched at %u (= sa[%u] + %u)\n", pattern_str, i, x, prefix, inv.x, inv.y);
            }*/
        }
    }
    fprintf(stderr, "\n  alignment test... done\n" );

    const uint32 SPARSITY = 100;

    data.input[0] = 0;
    for (uint32 i = 1; i < REQS; ++i)
        data.input[i] = (data.input[i-1] + (rand() % SPARSITY)) % (LEN - PLEN);

    fprintf(stderr, "  sorted alignment tests... started\n" );

    synthetic_test_host(
        REQS,
        PLEN,
        text,
        fmi,
        data );

    synthetic_test_device<OCC_INT,SA_INT>(
        REQS,
        LEN,
        PLEN,
        WORDS,
        OCC_WORDS,
        data,
        ssa );

    fprintf(stderr, "  sorted alignment tests... done\n" );

    fprintf(stderr, "  shuffled alignment tests... started\n" );

    for (uint32 i = 0; i < REQS; ++i)
    {
        const uint32 j = i + rand() % (REQS - i);
        std::swap( data.input[i], data.input[j] );
    }

    synthetic_test_host(
        REQS,
        PLEN,
        text,
        fmi,
        data );

    synthetic_test_device<OCC_INT,SA_INT>(
        REQS,
        LEN,
        PLEN,
        WORDS,
        OCC_WORDS,
        data,
        ssa );

    fprintf(stderr, "  shuffled alignment tests... done\n" );
}

//
// A backtracking delegate used to count the total number of occurrences
//
struct CountDelegate
{
    // constructor
    //
    // \param count     pointer to the global counter
    NVBIO_FORCEINLINE NVBIO_DEVICE
    CountDelegate(uint32* count) : m_count( count ) {}

    // main functor operator
    //
    NVBIO_FORCEINLINE NVBIO_DEVICE
    void operator() (const uint2 range) const { atomicAdd( m_count, range.y + 1u - range.x ); }

private:
    uint32* m_count;    // global counter
};

//
// k-mer counting kernel
//
template <typename ReadsView, typename FMIndexType>
__global__
void count_kernel(
    const ReadsView   reads,            // reads view
    const FMIndexType fmi,              // FM-index
    const uint32      len,              // pattern length
    const uint32      seed,             // exact-matching seed length
    const uint32      mismatches,       // number of allowed mismatches after the seed
          uint32*     count)            // global output counter
{
    const uint32 thread_id = threadIdx.x + blockIdx.x*blockDim.x;
    if (thread_id >= reads.size())
        return;

    CountDelegate counter( count );

    typedef io::ReadData::const_read_stream_type read_stream_type;
    const read_stream_type read_stream( reads.read_stream() );

    const uint32 read_begin = reads.read_index()[ thread_id ];
    const uint32 read_end   = reads.read_index()[ thread_id+1 ];

    uint4 stack[32*4];

    hamming_backtrack(
        fmi,
        read_stream.begin() + read_begin,
        len,
        seed,
        mismatches,
        stack,
        counter );
}

//
// run a set of backtracking tests with real data
//
void backtrack_test(const char* index_file, const char* reads_name, const uint32 n_reads)
{
    io::FMIndexDataRAM fmi;
    if (fmi.load( index_file, io::FMIndexData::FORWARD ))
    {
        io::FMIndexDataCUDA fmi_cuda( fmi, io::FMIndexDataCUDA::FORWARD );

        typedef PackedStream<io::FMIndexLdgIterators::bwt_type,uint8,2u,true> bwt_type;
        typedef rank_dictionary<2u,
            io::FMIndexDataCUDA::OCC_INT,
            bwt_type,
            io::FMIndexLdgIterators::occ_type,
            io::FMIndexLdgIterators::count_table_type>                      rank_dict_type;

        typedef fm_index<rank_dict_type,io::FMIndexLdgIterators::ssa_type>  fmindex_type;

        io::FMIndexLdgIterators fmi_iterators( fmi_cuda );

        fmindex_type fmindex_cuda(
            fmi_cuda.genome_length(),
            fmi_cuda.primary,
            fmi_cuda.L2,
            rank_dict_type(
                fmi_iterators.bwt_iterator(),
                fmi_iterators.occ_iterator(),
                fmi_iterators.count_table() ),
            fmi_iterators.ssa_iterator() );

        io::ReadDataStream* reads_file = io::open_read_file(
            reads_name,
            io::Phred,
            n_reads,
            50 );

        if (reads_file == NULL)
        {
            log_error(stderr, "unable to load \"%s\"\n", reads_name);
            exit(1);
        }

        io::ReadData* reads_data = reads_file->next( n_reads );
        if (reads_data == NULL)
        {
            log_error(stderr, "unable to fetch reads from file \"%s\"\n", reads_name);
            exit(1);
        }

        io::ReadDataCUDA reads_data_cuda( *reads_data );

        // create a device-side read batch
        io::ReadDataView<const uint32*,const uint32*,const char*,const char*> reads_view( reads_data_cuda );

        thrust::device_vector<uint32> counter(1);
        counter[0] = 0;

        const uint32 blockdim = 128;
        const uint32 n_blocks = (reads_data_cuda.size() + blockdim - 1) / blockdim;

        // 20-mers, distance=0
        {
            cudaEvent_t start, stop;
            cudaEventCreate( &start );
            cudaEventCreate( &stop );

            cudaEventRecord( start, 0 );

            count_kernel<<<n_blocks,blockdim>>>(
                reads_view,
                fmindex_cuda,
                20u,
                0u,
                0u,
                thrust::raw_pointer_cast( &counter.front() ) );

            cudaThreadSynchronize();

            float time;
            cudaEventRecord( stop, 0 );
            cudaEventSynchronize( stop );
            cudaEventElapsedTime( &time, start, stop );

            fprintf(stderr, "  backtracking (20,0,0)... done: %.1fms, A/s: %.2f M\n", time, reads_data_cuda.size()/(time*1000.0f) );
        }
        // 32-mers, distance=1
        {
            cudaEvent_t start, stop;
            cudaEventCreate( &start );
            cudaEventCreate( &stop );

            cudaEventRecord( start, 0 );

            count_kernel<<<n_blocks,blockdim>>>(
                reads_view,
                fmindex_cuda,
                32u,
                0u,
                1u,
                thrust::raw_pointer_cast( &counter.front() ) );

            cudaThreadSynchronize();

            float time;
            cudaEventRecord( stop, 0 );
            cudaEventSynchronize( stop );
            cudaEventElapsedTime( &time, start, stop );

            fprintf(stderr, "  backtracking (32,1,0)... done: %.1fms, A/s: %.2f M\n", time, reads_data_cuda.size()/(time*1000.0f) );
        }
        // 50-mers, distance=2, seed=25
        {
            cudaEvent_t start, stop;
            cudaEventCreate( &start );
            cudaEventCreate( &stop );

            cudaEventRecord( start, 0 );

            count_kernel<<<n_blocks,blockdim>>>(
                reads_view,
                fmindex_cuda,
                50u,
                25u,
                2u,
                thrust::raw_pointer_cast( &counter.front() ) );

            cudaThreadSynchronize();

            float time;
            cudaEventRecord( stop, 0 );
            cudaEventSynchronize( stop );
            cudaEventElapsedTime( &time, start, stop );

            fprintf(stderr, "  backtracking (50,2,25)... done: %.1fms, A/s: %.2f M\n", time, reads_data_cuda.size()/(time*1000.0f) );
        }

        delete reads_data;
        delete reads_file;
    }
    else
        log_warning(stderr, "unable to load \"%s\"\n", index_file);
}

int fmindex_test(int argc, char* argv[])
{
    uint32 synth_len     = 10000000;
    uint32 synth_queries = 64*1024;

    char*  index_name        = "data/human.NCBI36/Homo_sapiens.NCBI36.53.dna.toplevel.fa";
    char*  reads_name        = "data/SRR493095_1.fastq.gz";
    uint32 backtrack_queries = 64*1024;

    for (int i = 0; i < argc; ++i)
    {
        if (strcmp( argv[i], "-synth-length" ) == 0)
            synth_len = atoi( argv[++i] )*1000;
        else if (strcmp( argv[i], "-synth-queries" ) == 0)
            synth_queries = atoi( argv[++i] )*1000;
        else if (strcmp( argv[i], "-backtrack-queries" ) == 0)
            backtrack_queries = atoi( argv[++i] ) * 1024;
        else if (strcmp( argv[i], "-index" ) == 0)
            index_name = argv[++i];
        else if (strcmp( argv[i], "-reads" ) == 0)
            reads_name = argv[++i];
    }

    fprintf(stderr, "FM-index test... started\n");

    if (synth_len && synth_queries)
    {
        synthetic_test<uint32>( synth_len, synth_queries );
        synthetic_test<uint64>( synth_len, synth_queries );
    }

    if (backtrack_queries)
        backtrack_test( index_name, reads_name, backtrack_queries );

    fprintf(stderr, "FM-index test... done\n");
    return 0;
}
