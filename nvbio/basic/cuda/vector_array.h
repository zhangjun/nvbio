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

#include <nvbio/basic/types.h>
#include <nvbio/basic/thrust_view.h>

namespace nvbio {
namespace cuda {

/// \page vector_arrays_page Vector Arrays
///
/// This module implements the notion of <i>vector array</i>, i.e. an array of dynamically-allocated vectors.
/// The ideas is that one can allocate some shared arena, and then carve N vectors from the arena
/// in parallel.
/// This data-structure provides methods to perform the carving and remember the binding between the i-th
/// array and its slot in the arena.
///
/// \section AtAGlanceSection At a Glance
///
/// There's two flavors of this class, one for the host and one for the device:
///
/// - HostVectorArray
/// - DeviceVectorArray
///
/// The DeviceVectorArray is a container meant to be used from the host; the corresponding view:
///
/// - DeviceVectorArrayView
///
/// can be obtained with a call to the plain_view() function.
///
/// \section Example
///
///\code
/// __global__ void my_alloc_kernel(DeviceVectorArrayView<uint32> vector_array)
/// {
///     const uint32 idx = threadIdx.x + blockIdx.x * blockDim.x;
///     const uint32 size = threadIdx.x+1;
///
///     // alloc this thread's vector
///     vector_array.alloc(
///         idx,           // vector to allocate
///         size );        // vector size
/// }
/// __global__ void my_other_kernel(DeviceVectorArrayView<uint32> vector_array)
/// {
///     const uint32 idx = threadIdx.x + blockIdx.x * blockDim.x;
///
///     // and do something with it
///     do_something( vector_array[i] );
/// }
///
/// DeviceVectorArray<uint32> vector_array( 32, 32*32 );
///
/// my_alloc_kernel<<<1,32>>>( plain_view( vector_array ) );
/// my_other_kernel<<<1,32>>>( plain_view( vector_array ) );
///\endcode
///
/// \section TechnicalOverviewSection Technical Overview
///
/// See the \ref VectorArrayModule module documentation.
///

///@addtogroup Basic
///@{

///\defgroup VectorArrayModule Vector Arrays
///
/// This module implements the notion of <i>vector array</i>, i.e. an array of dynamically-allocated vectors.
/// See \ref vector_arrays_page.
///

///@addtogroup VectorArrayModule
///@{

///
/// A utility class to manage a vector of dynamically-allocated arrays
///
template <typename T>
struct DeviceVectorArrayView
{
    /// constructor
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    DeviceVectorArrayView(
        T*      arena = NULL,
        uint32* index = NULL,
        uint32* pool  = NULL,
        uint32  size  = 0u)
        : m_arena(arena), m_index(index), m_pool(pool), m_size(size) {}

#ifdef __CUDACC__
    /// alloc the vector bound to the given index
    ///
    NVBIO_FORCEINLINE NVBIO_DEVICE
    T* alloc(const uint32 index, const uint32 size)
    {
        const uint32 slot = atomicAdd( m_pool, size );
        if (slot + size >= m_size)
        {
            // mark an out-of-bounds allocation
            m_index[index] = m_size;
            return NULL;
        }
        m_index[index] = slot;
        return m_arena + slot;
    }
#endif

    /// return the vector corresponding to the given index
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    T* operator[](const uint32 index) const
    {
        // for unsuccessful allocations m_index is set to m_size - in that case we return NULL
        return (m_index[index] < m_size) ? m_arena + m_index[index] : NULL;
    }

    /// return the slot corresponding to the given index
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 slot(const uint32 index) const { return m_index[index]; }

public:
    T*      m_arena;        ///< memory arena
    uint32* m_index;        ///< index of the allocated arrays
    uint32* m_pool;         ///< pool counter
    uint32  m_size;         ///< size of the arena
};

///
/// A utility class to manage a vector of dynamically-allocated arrays
///
template <typename T>
struct DeviceVectorArray
{
    typedef DeviceVectorArrayView<T>    device_view_type;
    typedef DeviceVectorArrayView<T>    plain_view_type;

    /// constructor
    ///
    DeviceVectorArray() : m_pool(1,0) {}

    /// resize the arena
    ///
    /// \param size      size of the array (i.e. number of vectors)
    /// \param arena     size of the memory arena
    /// \param do_alloc  a flag to indicate whether to really perform allocations;
    ///                  if false, the function will just return the amount of memory
    ///                  needed
    ///
    /// \return          amount of memory allocated/needed
    ///
    uint64 resize(const uint32 size, const uint32 arena, const bool do_alloc = true)
    {
        uint64 bytes = 0;
        if (do_alloc) m_arena.resize( arena ); bytes += sizeof(T)*arena;
        if (do_alloc) m_index.resize( size );  bytes += sizeof(uint32)*size;
        return bytes;
    }

    /// clear the pool
    ///
    void clear() { m_pool[0] = 0; }

    /// return number of vectors
    ///
    uint32 size() const { return m_index.size(); }

    /// return allocated size
    ///
    uint32 allocated_size() const { return m_pool[0]; }

    /// return allocated size
    ///
    uint32 arena_size() const { return m_arena.size(); }

    /// return the device view of this object
    ///
    device_view_type device_view()
    {
        return DeviceVectorArrayView<T>(
            nvbio::device_view( m_arena ),
            nvbio::device_view( m_index ),
            nvbio::device_view( m_pool ),
            uint32( m_arena.size() ) );
    }

    thrust::device_vector<T>        m_arena;        ///< memory arena
    thrust::device_vector<uint32>   m_index;        ///< index of the allocated arrays
    thrust::device_vector<uint32>   m_pool;         ///< pool counter
};

///
/// A utility class to manage a vector of dynamically-allocated arrays
///
template <typename T>
struct HostVectorArray
{
    /// constructor
    ///
    HostVectorArray() : m_pool(1,0) {}

    /// resize the arena
    ///
    /// \param size      size of the array (i.e. number of vectors)
    /// \param arena     size of the memory arena
    /// \param do_alloc  a flag to indicate whether to really perform allocations;
    ///                  if false, the function will just return the amount of memory
    ///                  needed
    ///
    /// \return          amount of memory allocated/needed
    ///
    uint64 resize(const uint32 size, const uint32 arena, const bool do_alloc = true)
    {
        uint64 bytes = 0;
        if (do_alloc) m_arena.resize( arena ); bytes += sizeof(T)*arena;
        if (do_alloc) m_index.resize( size );  bytes += sizeof(uint32)*size;
        return bytes;
    }

    /// clear the pool
    ///
    void clear() { m_pool[0] = 0; }

    /// return number of vectors
    ///
    uint32 size() const { return m_index.size(); }

    /// return allocated size
    ///
    uint32 allocated_size() const { return m_pool[0]; }

    /// copy operator
    ///
    HostVectorArray& operator=(const DeviceVectorArray<T>& vec)
    {
        thrust_copy_vector( m_arena, vec.m_arena );
        thrust_copy_vector( m_index, vec.m_index );
        thrust_copy_vector( m_pool,  vec.m_pool );
        return *this;
    }

    /// return the vector corresponding to the given index
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    const T* operator[](const uint32 index) const
    {
        // for unsuccessful allocations m_index is set to m_size - in that case we return NULL
        return (m_index[index] < m_arena.size()) ? &m_arena[0] + m_index[index] : NULL;
    }

    /// return the slot corresponding to the given index
    ///
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    uint32 slot(const uint32 index) const { return m_index[index]; }

    thrust::host_vector<T>        m_arena;        ///< memory arena
    thrust::host_vector<uint32>   m_index;        ///< index of the allocated arrays
    thrust::host_vector<uint32>   m_pool;         ///< pool counter
};

///@} // VectorArrayModule
///@} Basic

} // namespace cuda

///\relates cuda::DeviceVectorArray
/// return a view of the queues
///
template <typename T>
cuda::DeviceVectorArrayView<T> device_view(cuda::DeviceVectorArray<T>& vec) { return vec.device_view(); }

///\relates cuda::DeviceVectorArray
/// return a view of the queues
///
template <typename T>
cuda::DeviceVectorArrayView<T> plain_view(cuda::DeviceVectorArray<T>& vec) { return vec.device_view(); }

} // namespace nvbio
