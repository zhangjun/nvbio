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

// nvbio-test.cpp
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nvbio/basic/types.h>
#include <nvbio/basic/console.h>
#include <cuda_runtime_api.h>

int cache_test();
int packedstream_test();
int bwt_test();
int fmindex_test(int argc, char* argv[]);
int fastq_test(const char* filename);

void crcInit();

namespace nvbio {

int alloc_test();
int syncblocks_test();
int condition_test();
int rank_test(int argc, char* argv[]);
int work_queue_test(int argc, char* argv[]);
int string_set_test(int argc, char* argv[]);
int sum_tree_test();
namespace cuda { void scan_test(); }
namespace aln { void test(int argc, char* argv[]); }
namespace html { void test(); }

} // namespace nvbio

using namespace nvbio;

enum Tests {
    kStringSet      = 1u,
    kScan           = 2u,
    kSumTree        = 16u,
    kHTML           = 32u,
    kCache          = 64u,
    kPackedStream   = 128u,
    kBWT            = 256u,
    kFMIndex        = 512u,
    kAlloc          = 1024u,
    kSyncblocks     = 2048u,
    kCondition      = 4096u,
    kWorkQueue      = 8192u,
    kAlignment      = 16384u,
    kRank           = 32768u,
    kALL            = 0xFFFFFFFFu
};

int main(int argc, char* argv[])
{
    crcInit();

    int cuda_device = -1;
    int device_count;
    cudaGetDeviceCount(&device_count);
    log_verbose(stderr, "  cuda devices : %d\n", device_count);

    uint32 tests = kALL;

    int arg = 1;
    if (argc > 1)
    {
        if (strcmp( argv[arg], "-device" ) == 0)
        {
            cuda_device = atoi(argv[++arg]);
            ++arg;
        }

        if (arg < argc)
        {
            if (strcmp( argv[arg], "-string-set" ) == 0)
                tests = kStringSet;
            else if (strcmp( argv[arg], "-scan" ) == 0)
                tests = kScan;
            else if (strcmp( argv[arg], "-sum-tree" ) == 0)
                tests = kSumTree;
            else if (strcmp( argv[arg], "-aln" ) == 0)
                tests = kAlignment;
            else if (strcmp( argv[arg], "-html" ) == 0)
                tests = kHTML;
            else if (strcmp( argv[arg], "-cache" ) == 0)
                tests = kCache;
            else if (strcmp( argv[arg], "-packed-stream" ) == 0)
                tests = kPackedStream;
            else if (strcmp( argv[arg], "-bwt" ) == 0)
                tests = kBWT;
            else if (strcmp( argv[arg], "-rank" ) == 0)
                tests = kRank;
            else if (strcmp( argv[arg], "-fm-index" ) == 0)
                tests = kFMIndex;
            else if (strcmp( argv[arg], "-alloc" ) == 0)
                tests = kAlloc;
            else if (strcmp( argv[arg], "-syncblocks" ) == 0)
                tests = kSyncblocks;
            else if (strcmp( argv[arg], "-condition" ) == 0)
                tests = kCondition;
            else if (strcmp( argv[arg], "-work-queue" ) == 0)
                tests = kWorkQueue;

            ++arg;
        }
    }

    // inspect and select cuda devices
    if (device_count)
    {
        if (cuda_device == -1)
        {
            int            best_device = 0;
            cudaDeviceProp best_device_prop;
            cudaGetDeviceProperties( &best_device_prop, best_device );

            for (int device = 0; device < device_count; ++device)
            {
                cudaDeviceProp device_prop;
                cudaGetDeviceProperties( &device_prop, device );
                log_verbose(stderr, "  device %d has compute capability %d.%d\n", device, device_prop.major, device_prop.minor);
                log_verbose(stderr, "    SM count          : %u\n", device_prop.multiProcessorCount);
                log_verbose(stderr, "    SM clock rate     : %u Mhz\n", device_prop.clockRate / 1000);
                log_verbose(stderr, "    memory clock rate : %.1f Ghz\n", float(device_prop.memoryClockRate) * 1.0e-6f);

                if (device_prop.major >= best_device_prop.major &&
                    device_prop.minor >= best_device_prop.minor)
                {
                    best_device_prop = device_prop;
                    best_device      = device;
                }
            }
            cuda_device = best_device;
        }
        log_verbose(stderr, "  chosen device %d\n", cuda_device);
        {
            cudaDeviceProp device_prop;
            cudaGetDeviceProperties( &device_prop, cuda_device );
            log_verbose(stderr, "    device name        : %s\n", device_prop.name);
            log_verbose(stderr, "    compute capability : %d.%d\n", device_prop.major, device_prop.minor);
        }
        cudaSetDevice( cuda_device );
    }

    // allocate some heap
    cudaDeviceSetLimit( cudaLimitMallocHeapSize, 128*1024*1024 );

    argc = argc >= arg ? argc-arg : 0;

    if (tests & kAlloc)         alloc_test();
    if (tests & kSyncblocks)    syncblocks_test();
    if (tests & kCondition)     condition_test();
    if (tests & kWorkQueue)     work_queue_test( argc, argv+arg );
    if (tests & kStringSet)     string_set_test( argc, argv+arg );
    if (tests & kScan)          cuda::scan_test();
    if (tests & kAlignment)     aln::test( argc, argv+arg );
    if (tests & kSumTree)       sum_tree_test();
    if (tests & kHTML)          html::test();
    if (tests & kCache)         cache_test();
    if (tests & kPackedStream)  packedstream_test();
    if (tests & kBWT)           bwt_test();
    if (tests & kRank)          rank_test( argc, argv+arg );
    if (tests & kFMIndex)       fmindex_test( argc, argv+arg );

    cudaDeviceReset();
	return 0;
}

