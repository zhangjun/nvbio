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

#include <nvBowtie/bowtie2/cuda/defs.h>
#include <nvBowtie/bowtie2/cuda/fmindex_def.h>
#include <nvBowtie/bowtie2/cuda/seed_hit.h>
#include <nvBowtie/bowtie2/cuda/seed_hit_deque_array.h>
#include <nvBowtie/bowtie2/cuda/scoring_queues.h>
#include <nvBowtie/bowtie2/cuda/bowtie2_cuda_driver.h>
#include <nvBowtie/bowtie2/cuda/params.h>
#include <nvBowtie/bowtie2/cuda/stats.h>
#include <nvBowtie/bowtie2/cuda/mapping.h>
#include <nvbio/io/alignments.h>
#include <nvbio/io/output/output_file.h>
#include <nvBowtie/bowtie2/cuda/scoring.h>
#include <nvBowtie/bowtie2/cuda/mapq.h>
#include <nvBowtie/bowtie2/cuda/input_thread.h>
#include <nvbio/basic/cuda/arch.h>
#include <nvbio/basic/cuda/sort.h>
#include <nvbio/basic/cuda/host_device_buffer.h>
#include <nvbio/basic/cuda/vector_array.h>
#include <nvbio/basic/cuda/work_queue.h>
#include <nvbio/basic/timer.h>
#include <nvbio/basic/console.h>
#include <nvbio/basic/options.h>
#include <nvbio/basic/threads.h>
#include <nvbio/basic/html.h>
#include <nvbio/fmindex/dna.h>
#include <nvbio/fmindex/bwt.h>
#include <nvbio/fmindex/ssa.h>
#include <nvbio/fmindex/fmindex.h>
#include <nvbio/fmindex/fmindex_device.h>
#include <thrust/host_vector.h>
#include <thrust/device_vector.h>
#include <thrust/scan.h>
#include <thrust/sort.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <functional>



namespace nvbio {
namespace bowtie2 {
namespace cuda {

struct Aligner
{
    static const uint32 MAX_READ_LEN = MAXIMUM_READ_LENGTH;

    typedef FMIndexDef::type                                                                                                 fmi_type;
    typedef FMIndexDef::type                                                                                                 rfmi_type;

    typedef typename binary_switch<uint32,uint4,USE_UINT4_PACKING>::type                                                     read_storage_type;
    typedef typename binary_switch<const read_storage_type*,nvbio::cuda::ldg_pointer<read_storage_type>,USE_TEX_READS>::type read_base_type;
    typedef typename binary_switch<const char*,             nvbio::cuda::ldg_pointer<char>,             USE_TEX_READS>::type read_qual_type;
    typedef io::ReadDataView<const uint32*,read_base_type,read_qual_type,const char*>                                        read_batch_type;

    typedef typename binary_switch<uint32,uint4,USE_UINT4_PACKING>::type                                                     genome_storage_type;
    typedef nvbio::cuda::ldg_pointer<genome_storage_type>                                                                    genome_iterator_type;


    uint32                              BATCH_SIZE;

    thrust::device_vector<uint8>        dp_buffer_dvec;
    uint8*                              dp_buffer_dptr;

    SeedHitDequeArray                   hit_deques;

    nvbio::cuda::PingPongQueues<uint32> seed_queues;
    ScoringQueues                       scoring_queues;

    thrust::device_vector<uint32>       idx_queue_dvec;
    uint32*                             idx_queue_dptr;
    thrust::device_vector<uint16>       sorting_queue_dvec;

    thrust::device_vector<uint32>       trys_dvec;
    uint32*                             trys_dptr;
    thrust::device_vector<uint32>       rseeds_dvec;
    uint32*                             rseeds_dptr;

    thrust::device_vector<io::BestAlignments> best_data_dvec;
    thrust::device_vector<io::BestAlignments> best_data_dvec_o;
    io::BestAlignments*                       best_data_dptr;
    io::BestAlignments*                       best_data_dptr_o;

    // --- paired-end vectors --------------------------------- //
    thrust::device_vector<uint32>   opposite_queue_dvec;
    uint32*                         opposite_queue_dptr;

    // --- all-mapping vectors -------------------------------- //
    thrust::device_vector<io::Alignment>  buffer_alignments_dvec;
    io::Alignment*                        buffer_alignments_dptr;
    thrust::device_vector<uint32>         buffer_read_info_dvec;
    uint32*                               buffer_read_info_dptr;
    thrust::device_vector<io::Alignment>  output_alignments_dvec;
    io::Alignment*                        output_alignments_dptr;
    thrust::device_vector<uint32>         output_read_info_dvec;
    uint32*                               output_read_info_dptr;

    thrust::device_vector<uint32>         hits_count_scan_dvec;
    uint32*                               hits_count_scan_dptr;
    thrust::device_vector<uint64>         hits_range_scan_dvec;
    uint64*                               hits_range_scan_dptr;
    // -------------------------------------------------------- //

    nvbio::cuda::DeviceVectorArray<uint8>       mds;
    nvbio::cuda::DeviceVectorArray<io::Cigar>   cigar;
    thrust::device_vector<uint2>                cigar_coords_dvec;
    uint2*                                      cigar_coords_dptr;

    thrust::device_vector<uint64>   hits_stats_dvec;
    thrust::host_vector<uint64>     hits_stats_hvec;
    uint64*                         hits_stats_dptr;

    uint32                          batch_number;

    nvbio::cuda::SortEnactor        sort_enactor;
    // --------------------------------------------------------------------------------------------- //

    // file object that we're writing into
    io::OutputFile *output_file;

    static uint32 band_length(const uint32 max_dist)
    {
        //return max_dist*2+1;
        // compute band length
        uint32 band_len = 4;
        while (band_len-1 < max_dist*2+1)
            band_len *= 2;
        band_len -= 1;
        return band_len;
    }

    bool init(const uint32 BATCH_SIZE, const Params& params, const EndType type);

    void keep_stats(const uint32 count, Stats& stats);

    template <typename scoring_tag>
    void best_approx(
        const Params&               params,
        const fmi_type              fmi,
        const rfmi_type             rfmi,
        const UberScoringScheme&    scoring_scheme,
        const io::FMIndexDataCUDA&  driver_data,
        io::ReadDataCUDA&           read_data,
        Stats&                      stats);

    template <
        typename scoring_tag,
        typename scoring_scheme_type>
    void best_approx_score(
        const Params&               params,
        const fmi_type              fmi,
        const rfmi_type             rfmi,
        const scoring_scheme_type&  scoring_scheme,
        const io::FMIndexDataCUDA&  driver_data,
        io::ReadDataCUDA&           read_data,
        const uint32                seeding_pass,
        const uint32                seed_queue_size,
        const uint32*               seed_queue,
        Stats&                      stats);

    template <typename scoring_tag>
    void best_approx(
        const Params&               params,
        const FMIndexDef::type      fmi,
        const FMIndexDef::type      rfmi,
        const UberScoringScheme&    scoring_scheme,
        const io::FMIndexDataCUDA&  driver_data,
        io::ReadDataCUDA&           read_data1,
        io::ReadDataCUDA&           read_data2,
        Stats&                      stats);

    template <
        typename scoring_tag,
        typename scoring_scheme_type>
    void best_approx_score(
        const Params&               params,
        const fmi_type              fmi,
        const rfmi_type             rfmi,
        const scoring_scheme_type&  scoring_scheme,
        const io::FMIndexDataCUDA&  driver_data,
        const uint32                anchor,
        io::ReadDataCUDA&           read_data1,
        io::ReadDataCUDA&           read_data2,
        const uint32                seeding_pass,
        const uint32                seed_queue_size,
        const uint32*               seed_queue,
        Stats&                      stats);

    template <typename scoring_tag>
    void all(
        const Params&               params,
        const fmi_type              fmi,
        const rfmi_type             rfmi,
        const UberScoringScheme&    scoring_scheme,
        const io::FMIndexDataCUDA&  driver_data,
        io::ReadDataCUDA&           read_data,
        Stats&                      stats);

    template <typename scoring_scheme_type>
    void score_all(
        const Params&               params,
        const fmi_type              fmi,
        const rfmi_type             rfmi,
        const UberScoringScheme&    input_scoring_scheme,
        const scoring_scheme_type&  scoring_scheme,
        const io::FMIndexDataCUDA&  driver_data,
        io::ReadDataCUDA&           read_data,
        const uint32                seed_queue_size,
        const uint32*               seed_queue,
        Stats&                      stats,
        uint64&                     total_alignments);

    // return a pointer to an "index" into the given keys sorted by their hi bits
    //
    uint32* sort_hi_bits(
        const uint32    count,
        const uint32*   keys);

    // sort a set of keys in place
    //
    void sort_inplace(
        const uint32    count,
        uint32*         keys);

private:
    std::pair<uint64,uint64> init_alloc(const uint32 BATCH_SIZE, const Params& params, const EndType type, bool do_alloc);
};

// Compute the total number of matches found
void hits_stats(
    const uint32    batch_size,
    const SeedHit*  hit_data,
    const uint32*   hit_counts,
          uint64*   hit_stats);

void ring_buffer_to_plain_array(
    const uint32* buffer,
    const uint32  buffer_size,
    const uint32  begin,
    const uint32  end,
          uint32* output);

#if defined(__CUDACC__)

// initialize a set of BestAlignments
//
template <typename ReadBatch, typename ScoreFunction>
__global__
void init_alignments_kernel(
    const ReadBatch         read_batch,
    const ScoreFunction     worst_score_fun,
    io::BestAlignments*     best_data,
    const uint32            mate)
{
    const uint32 thread_id = threadIdx.x + BLOCKDIM*blockIdx.x;
    if (thread_id >= read_batch.size()) return;

    // compute the read length
    const uint2 read_range = read_batch.get_range( thread_id );
    const uint32 read_len  = read_range.y - read_range.x;

    const int32 worst_score = worst_score_fun( read_len );

    io::BestAlignments best;
    best.m_a1 = io::Alignment( uint32(-1), io::Alignment::max_ed(), worst_score, mate );
    best.m_a2 = io::Alignment( uint32(-1), io::Alignment::max_ed(), worst_score, mate );
    best_data[ thread_id ] = best;
}

// initialize a set of BestAlignments
//
template <typename ReadBatch, typename ScoreFunction>
void init_alignments(
    const ReadBatch         read_batch,
    const ScoreFunction     worst_score_fun,
    io::BestAlignments*     best_data,
    const uint32            mate = 0)
{
    const int blocks = (read_batch.size() + BLOCKDIM-1) / BLOCKDIM;

    init_alignments_kernel<<<blocks, BLOCKDIM>>>(
        read_batch,
        worst_score_fun,
        best_data,
        mate );
}

#endif // defined(__CUDACC__)

} // namespace cuda
} // namespace bowtie2
} // namespace nvbio
