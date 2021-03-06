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

#include <nvbio/basic/packedstream.h>
#include <nvbio/alignment/sink.h>
#include <nvbio/alignment/utils.h>
#include <nvbio/alignment/alignment_base_inl.h>
#include <nvbio/basic/iterator.h>


namespace nvbio {
namespace aln {

namespace priv
{

///@addtogroup private
///@{

///
/// A helper scoring context class, which can be used to adapt the basic
/// sw_alignment_score_dispatch algorithm to various situations, such as:
///   scoring
///   scoring within a window (i.e. saving only the last band within the window)
///   computing checkpoints
///   computing a flow submatrix
///
template <uint32 BAND_LEN, AlignmentType TYPE>
struct SWScoringContext
{

    /// initialize the j-th column of the DP matrix
    ///
    /// \param j         column index
    /// \param N         column size
    /// \param column    column values
    /// \param scoring   scoring scheme
    /// \param zero      zero value
    template <typename column_type, typename scoring_type, typename score_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void init(
        const uint32        j,
        const uint32        N,
              column_type&  column,
        const scoring_type& scoring,
        const score_type    zero)
    {
        if (j == 0) // ensure this context can be used for multi-pass scoring
        {
            for (uint32 i = 0; i < N; ++i)
                column[i] = TYPE == GLOBAL ? scoring.deletion() * (i+1) : zero;
        }
    }

    /// do something with the previous column
    ///
    /// \param j         column index
    /// \param N         column size
    /// \param column    column values
    template <typename column_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void previous_column(
        const uint32        j,
        const uint32        N,
        const column_type   column) {}

    /// do something with the newly computed cell
    ///
    /// \param i         row index
    /// \param N         number of rows (column size)
    /// \param j         column index
    /// \param M         number of columns (row size)
    /// \param score     computed score
    /// \param dir       direction flow
    template <typename score_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void new_cell(
        const uint32            i,
        const uint32            N,
        const uint32            j,
        const uint32            M,
        const score_type        score,
        const DirectionVector   dir) {}
};

///
/// A helper checkpointed-scoring context class for sw_alignment_score_dispatch which allows to perform
/// scoring in multiple passes, saving & restoring a checkpoint each time.
///
template <uint32 BAND_LEN, AlignmentType TYPE, typename checkpoint_type>
struct SWCheckpointedScoringContext
{
    // constructor
    //
    // \param checkpoints       input checkpoints array
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    SWCheckpointedScoringContext(checkpoint_type checkpoint) :
        m_checkpoint( checkpoint ) {}

    /// initialize the j-th column of the DP matrix
    ///
    /// \param j         column index
    /// \param N         column size
    /// \param column    column values
    /// \param scoring   scoring scheme
    /// \param zero      zero value
    template <typename column_type, typename scoring_type, typename score_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void init(
        const uint32        j,
        const uint32        N,
              column_type&  column,
        const scoring_type& scoring,
        const score_type    zero)
    {
        if (j == 0)
        {
            for (uint32 i = 0; i < N; ++i)
                column[i] = TYPE == GLOBAL ? scoring.deletion() * (i+1) : zero;
        }
        else
        {
            for (uint32 i = 0; i < N; ++i)
                column[i] = m_checkpoint[i];
        }
    }

    /// do something with the previous column
    ///
    /// \param j         column index
    /// \param N         column size
    /// \param column    column values
    template <typename column_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void previous_column(
        const uint32        j,
        const uint32        N,
        const column_type   column) {}

    /// do something with the newly computed cell
    ///
    /// \param i         row index
    /// \param N         number of rows (column size)
    /// \param j         column index
    /// \param M         number of columns (row size)
    /// \param score     computed score
    /// \param dir       direction flow
    template <typename score_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void new_cell(
        const uint32            i,
        const uint32            N,
        const uint32            j,
        const uint32            M,
        const score_type        score,
        const DirectionVector   dir) {}

    checkpoint_type  m_checkpoint;
};

///
/// A helper scoring context classfor sw_alignment_score_dispatch, instantiated to keep track of checkpoints
///
template <uint32 BAND_LEN, AlignmentType TYPE, uint32 CHECKPOINTS, typename checkpoint_type>
struct SWCheckpointContext
{
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    SWCheckpointContext(checkpoint_type checkpoints) :
        m_checkpoints( checkpoints ) {}

    /// initialize the j-th column of the DP matrix
    ///
    /// \param j         column index
    /// \param N         column size
    /// \param column    column values
    /// \param scoring   scoring scheme
    /// \param zero      zero value
    template <typename column_type, typename scoring_type, typename score_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void init(
        const uint32        j,
        const uint32        N,
              column_type&  column,
        const scoring_type& scoring,
        const score_type    zero)
    {
        for (uint32 i = 0; i < N; ++i)
            column[i] = TYPE == GLOBAL ? scoring.deletion() * (i+1) : zero;
    }

    /// do something with the previous column
    ///
    /// \param j         column index
    /// \param N         column size
    /// \param column    column values
    template <typename column_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void previous_column(
        const uint32        j,
        const uint32        N,
        const column_type   column)
    {
        // save checkpoint
        if ((j & (CHECKPOINTS-1)) == 0u)
        {
            const uint32 checkpoint_id = j / CHECKPOINTS;

            for (uint32 i = 0; i < N; ++i)
                m_checkpoints[ checkpoint_id*N + i ] = column[i];
        }
    }

    /// do something with the newly computed cell
    ///
    /// \param i         row index
    /// \param N         number of rows (column size)
    /// \param j         column index
    /// \param M         number of columns (row size)
    /// \param score     computed score
    /// \param dir       direction flow
    template <typename score_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void new_cell(
        const uint32            i,
        const uint32            N,
        const uint32            j,
        const uint32            M,
        const score_type        score,
        const DirectionVector   dir) {}

    checkpoint_type  m_checkpoints;
};

///
/// A helper scoring context class for sw_alignment_score_dispatch, instantiated to keep track of the direction vectors
/// of a DP submatrix between given checkpoints
///
template <uint32 BAND_LEN, AlignmentType TYPE, uint32 CHECKPOINTS, typename checkpoint_type, typename submatrix_type>
struct SWSubmatrixContext
{
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    SWSubmatrixContext(
        const checkpoint_type   checkpoints,
        const uint32            checkpoint_id,
        const submatrix_type    submatrix) :
        m_checkpoints( checkpoints ),
        m_checkpoint_id( checkpoint_id ),
        m_submatrix( submatrix ) {}

    /// initialize the j-th column of the DP matrix
    ///
    /// \param j         column index
    /// \param N         column size
    /// \param column    column values
    /// \param scoring   scoring scheme
    /// \param zero      zero value
    template <typename column_type, typename scoring_type, typename score_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void init(
        const uint32        j,
        const uint32        N,
              column_type&  column,
        const scoring_type& scoring,
        const score_type    zero)
    {
        // restore the checkpoint
        for (uint32 i = 0; i < N; ++i)
            column[i] = m_checkpoints[ m_checkpoint_id*N + i ];
    }

    /// do something with the previous column
    ///
    /// \param j         column index
    /// \param N         column size
    /// \param column    column values
    template <typename column_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void previous_column(
        const uint32        j,
        const uint32        N,
        const column_type   column) {}

    // do something with the newly computed cell
    //
    // \param i         row index
    // \param N         number of rows (column size)
    // \param j         column index
    // \param M         number of columns (row size)
    // \param score     computed score
    // \param dir       direction flow
    template <typename score_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    void new_cell(
        const uint32            i,
        const uint32            N,
        const uint32            j,
        const uint32            M,
        const score_type        score,
        const DirectionVector   dir)
    {
        // save the direction vector
        const uint32 offset = m_checkpoint_id * CHECKPOINTS;

        if (TYPE == LOCAL)
            m_submatrix[ (j - offset)*N + i ] = score ? dir : SINK;
        else
            m_submatrix[ (j - offset)*N + i ] = dir;
    }

    checkpoint_type  m_checkpoints;
    uint32           m_checkpoint_id;
    submatrix_type   m_submatrix;
};

// -------------------------- Basic Smith-Waterman functions ---------------------------- //

///
/// Calculate the alignment score between a pattern and a text, using the Smith-Waterman algorithm.
///
/// \tparam BAND_LEN            internal band length
/// \tparam TYPE                the alignment type
/// \tparam symbol_type         type of string symbols
///
template <uint32 BAND_LEN, AlignmentType TYPE, typename symbol_type>
struct sw_alignment_score_dispatch
{
    template <
        bool CHECK_M,
        typename context_type,
        typename query_cache,
        typename score_type,
        typename temp_iterator,
        typename sink_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    static void update_row(
        context_type&        context,
        const uint32         block,
        const uint32         M,
        const uint32         i,
        const uint32         N,
        const symbol_type    r_i,
        const query_cache    q_cache,
        temp_iterator        temp,
        score_type&          temp_i,
        score_type*          band,
        sink_type&           sink,
        score_type&          max_score,
        const score_type     V,
        const score_type     S,
        const score_type     G,
        const score_type     I,
        const score_type     zero,
        const symbol_type    invalid_symbol)
    {
        // set the 0-th coefficient in the band to be equal to the (i-1)-th row of the left column (diagonal term)
        score_type prev = temp_i;

        // set the 0-th coefficient in the band to be equal to the i-th row of the left column (left term)
        band[0] = temp_i = temp[i];

#if 0
        // do top-diagonal cell updates
        #pragma unroll
        for (uint32 j = 1; j <= BAND_LEN; ++j)
        {
            const symbol_type q_j     = q_cache[ j-1 ];
            const score_type S_ij     = (r_i == q_j) ? V : S;
            const score_type diagonal = prev    + S_ij;
            const score_type top      = band[j] + G;
                             prev     = band[j];
                             band[j]  = nvbio::max( top, diagonal );
        }

        // do left cell updates
        #pragma unroll
        for (uint32 j = 1; j <= BAND_LEN; ++j)
        {
            const score_type left    = band[j-1] + I;
                  score_type hi      = nvbio::max( band[j], left );
                             hi      = TYPE == LOCAL ? nvbio::max( hi, zero ) : hi; // clamp to zero
                             band[j] = hi;
        }
#else
        // update all cells
        #pragma unroll
        for (uint32 j = 1; j <= BAND_LEN; ++j)
        {
            const symbol_type q_j     = q_cache[ j-1 ];
            const score_type S_ij     = (r_i == q_j) ? V : S;
            const score_type diagonal = prev      + S_ij;
            const score_type top      = band[j]   + G;
            const score_type left     = band[j-1] + I;
                  score_type hi       = nvbio::max3( top, left, diagonal );
                             hi       = TYPE == LOCAL ? nvbio::max( hi, zero ) : hi; // clamp to zero
                             prev     = band[j];
                             band[j]  = hi;

            context.new_cell(
                i,             N,
                block + j - 1, M,
                hi,
                top > left ?
                    (top  > diagonal ? DELETION  : SUBSTITUTION) :
                    (left > diagonal ? INSERTION : SUBSTITUTION) );
        }
#endif

        // save the last entry of the band
        temp[i] = band[ BAND_LEN ];

        max_score = nvbio::max( max_score, band[ BAND_LEN ] );

        if (TYPE == LOCAL)
        {
            if (CHECK_M)
            {
                // during local alignment we save the best score across all bands
                for (uint32 j = 1; j <= BAND_LEN; ++j)
                {
                    if (block + j <= M)
                        sink.report( band[j], make_uint2( i+1, block+j ) );
                }
            }
            else
            {
                // during local alignment we save the best score across all bands
                for (uint32 j = 1; j <= BAND_LEN; ++j)
                    sink.report( band[j], make_uint2( i+1, block+j ) );
            }
        }
        else if (CHECK_M)
        {
            if (TYPE == SEMI_GLOBAL)
            {
                // during semi-global alignment we save the best score across the last column H[*][M], at each row
                save_boundary<BAND_LEN>( block, M, band, i, sink );
            }
        }
    }

    ///
    /// Calculate the alignment score between a string and a reference, using the Smith-Waterman algorithm,
    /// using a templated column storage.
    ///
    /// This function is templated over:
    ///   - a context that is passed the computed DP matrix values, and can be
    ///     used to specialize its behavior.
    ///   - a sink that is used to report successful alignments
    ///
    ///
    /// Furthermore, the function can be called on a window of the pattern, assuming that the context
    /// will provide the proper initialization for the first column of the corresponding DP matrix window.
    ///
    /// \tparam context_type    the context class, implementing the following interface:
    /// \code
    /// struct context_type
    /// {
    ///     // initialize the j-th column of the DP matrix
    ///     //
    ///     // \param j         column index
    ///     // \param N         column size
    ///     // \param column    column values
    ///     // \param scoring   scoring scheme
    ///     // \param zero      zero value
    ///     template <typename column_type, typename scoring_type, typename score_type>
    ///     void init(
    ///         const uint32        j,
    ///         const uint32        N,
    ///               column_type&  column,
    ///         const scoring_type& scoring,
    ///         const score_type    zero);
    /// 
    ///     // do something with the previous column
    ///     //
    ///     // \param j         column index
    ///     // \param N         column size
    ///     // \param column    column values
    ///     template <typename column_type>
    ///     void previous_column(
    ///         const uint32        j,
    ///         const uint32        N,
    ///         const column_type   column);
    /// 
    ///     // do something with the newly computed cell
    ///     //
    ///     // \param i         row index
    ///     // \param N         number of rows (column size)
    ///     // \param j         column index
    ///     // \param M         number of columns (row size)
    ///     // \param score     computed score
    ///     // \param dir       direction flow
    ///     template <typename score_type>
    ///     void new_cell(
    ///         const uint32            i,
    ///         const uint32            N,
    ///         const uint32            j,
    ///         const uint32            M,
    ///         const score_type        score,
    ///         const DirectionVector   dir);
    /// };
    /// \endcode
    ///
    /// \param context       template context class, used to specialize the behavior of the aligner
    /// \param query         input pattern (horizontal string)
    /// \param quals         input pattern qualities (horizontal string)
    /// \param ref           input text (vertical string)
    /// \param scoring       scoring scheme
    /// \param min_score     minimum output score
    /// \param sink          alignment sink
    /// \param window_begin  beginning of pattern window
    /// \param window_end    end of pattern window
    ///
    template <
        typename context_type,
        typename string_type,
        typename qual_type,
        typename ref_type,
        typename scoring_type,
        typename sink_type,
        typename column_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    static
    bool run(
        const scoring_type& scoring,
        context_type&       context,
        string_type         query,
        qual_type           quals,
        ref_type            ref,
        const int32         min_score,
              sink_type&    sink,
        const uint32        window_begin,
        const uint32        window_end,
        column_type         temp)
    {
        const uint32 M = query.length();
        const uint32 N = ref.length();

        typedef int32 score_type;
        score_type band[BAND_LEN+1];

        const score_type G = scoring.deletion();
        const score_type I = scoring.insertion();
        const score_type S = scoring.mismatch();
        const score_type V = scoring.match();
        const score_type zero = score_type(0);

        // we are going to process the matrix in stripes, where each partial row within each stripe is processed
        // in registers: hence, we need to save the right-boundary of each stripe (a full matrix column) in some
        // temporary storage.

        // fill row 0 with zeros, this will never be updated
        uint8 q_cache[ BAND_LEN ];

        const uint8 invalid_symbol = 255u;

        // initialize the first column
        context.init( window_begin, N, temp, scoring, zero );

        const uint32 end_block = (window_end == M) ?
            nvbio::max( BAND_LEN, BAND_LEN * ((M + BAND_LEN-1) / BAND_LEN) ) :
            window_end + BAND_LEN;

        // loop across the short edge of the DP matrix (i.e. the columns)
        for (uint32 block = window_begin; block + BAND_LEN < end_block; block += BAND_LEN)
        {
            // save the previous column
            context.previous_column( block, N, temp );

            // initialize the first band (corresponding to the 0-th row of the DP matrix)
            #pragma unroll
            for (uint32 j = 0; j <= BAND_LEN; ++j)
                band[j] = (TYPE != LOCAL) ? G * (block + j) : zero;

            // load a block of entries from each query
            #pragma unroll
            for (uint32 t = 0; t < BAND_LEN; ++t)
                q_cache[ t ] = query[ block + t ];

            score_type max_score = Field_traits<score_type>::min();

            score_type temp_i = band[0];

            // loop across the long edge of the DP matrix (i.e. the rows)
            for (uint32 i = 0; i < N; ++i)
            {
                // load the new character from the reference
                const uint8 r_i = ref[i];

                update_row<false>(
                    context,
                    block, M,
                    i, N,
                    r_i,
                    q_cache,
                    temp,
                    temp_i,
                    band,
                    sink,
                    max_score,
                    V,S,G,I,
                    zero,
                    invalid_symbol );
            }

            // we are now (M - block - BAND_LEN) columns from the last one: check whether
            // we could theoretically reach the minimums core
            const score_type missing_cols = score_type(M - block - BAND_LEN);
            if (max_score + missing_cols * scoring.match(255) < score_type( min_score ))
                return false;
        }

        if (window_end == M)
        {
            const uint32 block = end_block - BAND_LEN;

            // save the previous column
            context.previous_column( block, N, temp );

            // initialize the first band (corresponding to the 0-th row of the DP matrix)
            for (uint32 j = 0; j <= BAND_LEN; ++j)
                band[j] = (TYPE != LOCAL) ? G * (block + j) : zero;

            // load a block of entries from each query
            const uint32 block_end = nvbio::min( block + BAND_LEN, M );
            #pragma unroll
            for (uint32 t = 0; t < BAND_LEN; ++t)
            {
                if (block + t < block_end)
                    q_cache[ t ] = query[ block + t ];
            }

            score_type max_score = Field_traits<score_type>::min();

            score_type temp_i = band[0];

            // loop across the long edge of the DP matrix (i.e. the rows)
            for (uint32 i = 0; i < N; ++i)
            {
                // load the new character from the reference
                const uint8 r_i = ref[i];

                update_row<true>(
                    context,
                    block, M,
                    i, N,
                    r_i,
                    q_cache,
                    temp,
                    temp_i,
                    band,
                    sink,
                    max_score,
                    V,S,G,I,
                    zero,
                    invalid_symbol );
            }
        }

        if (TYPE == GLOBAL)
            save_Mth<BAND_LEN>( M, band, N-1, sink );

        return true;
    }

    ///
    /// Calculate the alignment score between a string and a reference, using the Smith-Waterman algorithm,
    /// using local memory storage for the boundary columns.
    ///
    /// This function is templated over:
    ///   - a context that is passed the computed DP matrix values, and can be
    ///     used to specialize its behavior.
    ///   - a sink that is used to report successful alignments
    ///
    /// Furthermore, the function can be called on a window of the pattern, assuming that the context
    /// will provide the proper initialization for the first column of the corresponding DP matrix window.
    ///
    /// \tparam context_type    the context class, implementing the following interface:
    /// \code
    /// struct context_type
    /// {
    ///     // initialize the j-th column of the DP matrix
    ///     //
    ///     // \param j         column index
    ///     // \param N         column size
    ///     // \param column    column values
    ///     // \param scoring   scoring scheme
    ///     // \param zero      zero value
    ///     template <typename column_type, typename scoring_type, typename score_type>
    ///     void init(
    ///         const uint32        j,
    ///         const uint32        N,
    ///               column_type&  column,
    ///         const scoring_type& scoring,
    ///         const score_type    zero);
    /// 
    ///     // do something with the previous column
    ///     //
    ///     // \param j         column index
    ///     // \param N         column size
    ///     // \param column    column values
    ///     template <typename column_type>
    ///     void previous_column(
    ///         const uint32        j,
    ///         const uint32        N,
    ///         const column_type   column);
    /// 
    ///     // do something with the newly computed cell
    ///     //
    ///     // \param i         row index
    ///     // \param N         number of rows (column size)
    ///     // \param j         column index
    ///     // \param M         number of columns (row size)
    ///     // \param score     computed score
    ///     // \param dir       direction flow
    ///     template <typename score_type>
    ///     void new_cell(
    ///         const uint32            i,
    ///         const uint32            N,
    ///         const uint32            j,
    ///         const uint32            M,
    ///         const score_type        score,
    ///         const DirectionVector   dir);
    /// };
    /// \endcode
    ///
    ///
    /// \param scoring       scoring scheme
    /// \param context       template context class, used to specialize the behavior of the aligner
    /// \param query         input pattern (horizontal string)
    /// \param quals         input pattern qualities (horizontal string)
    /// \param ref           input text (vertical string)
    /// \param min_score     minimum output score
    /// \param sink          alignment sink
    /// \param window_begin  beginning of pattern window
    /// \param window_end    end of pattern window
    ///
    /// \return              false if early-exited, true otherwise
    ///
    template <
        uint32 MAX_REF_LEN,
        typename context_type,
        typename string_type,
        typename qual_type,
        typename ref_type,
        typename scoring_type,
        typename sink_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    static
    bool run(
        const scoring_type& scoring,
        context_type&       context,
        string_type         query,
        qual_type           quals,
        ref_type            ref,
        const int32         min_score,
              sink_type&    sink,
        const uint32        window_begin,
        const uint32        window_end)
    {
        // instantiated a local memory array
        int16  temp[MAX_REF_LEN];
        int16* temp_ptr = temp;

        return run(
            scoring,
            context,
            query,
            quals,
            ref,
            min_score,
            sink,
            window_begin,
            window_end,
            temp_ptr );
    }
};

template <AlignmentType TYPE, uint32 DIM, typename symbol_type>
struct sw_bandlen_selector
{
    static const uint32 BAND_LEN = 16u / DIM;
};

template <AlignmentType TYPE, uint32 DIM>
struct sw_bandlen_selector<TYPE,DIM,simd4u8>
{
#if __CUDA_ARCH__ >= 300
    static const uint32 BAND_LEN = 8u;
#else
    static const uint32 BAND_LEN = 1u;
#endif
};

///
/// Calculate the alignment score between a pattern and a text, using the Smith-Waterman algorithm.
///
/// \tparam TYPE                the alignment type
/// \tparam pattern_string      pattern string 
/// \tparam quals_string        pattern qualities
/// \tparam text_string         text string
/// \tparam column_type         temporary column storage
///
template <
    AlignmentType   TYPE,
    typename        scoring_type,
    typename        pattern_string,
    typename        qual_string,
    typename        text_string,
    typename        column_type>
struct alignment_score_dispatch<
    SmithWatermanAligner<TYPE,scoring_type>,
    pattern_string,
    qual_string,
    text_string,
    column_type>
{
    typedef SmithWatermanAligner<TYPE,scoring_type> aligner_type;

    /// dispatch scoring across the whole pattern
    ///
    /// \param aligner      scoring scheme
    /// \param pattern      pattern string (horizontal
    /// \param quals        pattern qualities
    /// \param text         text string (vertical)
    /// \param min_score    minimum score
    /// \param sink         output alignment sink
    /// \param column       temporary column storage
    ///
    /// \return             true iff the minimum score was reached
    ///
    template <typename sink_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    static bool dispatch(
        const aligner_type      aligner,
        const pattern_string    pattern,
        const qual_string       quals,
        const text_string       text,
        const  int32            min_score,
              sink_type&        sink,
              column_type       column)
    {
        typedef typename pattern_string::value_type    symbol_type;

        NVBIO_VAR_UNUSED const uint32 BAND_LEN = sw_bandlen_selector<TYPE,1u,symbol_type>::BAND_LEN;

        priv::SWScoringContext<BAND_LEN,TYPE> context;

        return sw_alignment_score_dispatch<BAND_LEN,TYPE,symbol_type>::run( aligner.scheme, context, pattern, quals, text, min_score, sink, 0, pattern.length(), column );
    }

    /// dispatch scoring in a window of the pattern
    ///
    /// \tparam checkpoint_type     a class to represent the checkpoint: an array of size equal to the text,
    ///                             that has to provide the const indexing operator[].
    ///
    /// \param aligner      scoring scheme
    /// \param pattern      pattern string (horizontal
    /// \param quals        pattern qualities
    /// \param text         text string (vertical)
    /// \param min_score    minimum score
    /// \param sink         output alignment sink
    /// \param column       temporary column storage
    ///
    /// \return             true iff the minimum score was reached
    ///
    template <
        typename sink_type,
        typename checkpoint_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    static bool dispatch(
        const aligner_type      aligner,
        const pattern_string    pattern,
        const qual_string       quals,
        const text_string       text,
        const  int32            min_score,
        const uint32            window_begin,
        const uint32            window_end,
              sink_type&        sink,
        checkpoint_type         checkpoint,
              column_type       column)
    {
        typedef typename pattern_string::value_type    symbol_type;

        NVBIO_VAR_UNUSED const uint32 BAND_LEN = sw_bandlen_selector<TYPE,1u,symbol_type>::BAND_LEN;

        priv::SWCheckpointedScoringContext<BAND_LEN,TYPE,checkpoint_type> context( checkpoint );

        return sw_alignment_score_dispatch<BAND_LEN,TYPE,symbol_type>::run( aligner.scheme, context, pattern, quals, text, min_score, sink, window_begin, window_end, column );
    }

    /// dispatch scoring in a window of the pattern, retaining the intermediate results in the column
    /// vector, essentially used as a continuation
    ///
    /// \param aligner      scoring scheme
    /// \param pattern      pattern string (horizontal)
    /// \param quals        pattern qualities
    /// \param text         text string (vertical)
    /// \param min_score    minimum score
    /// \param sink         output alignment sink
    /// \param column       temporary column storage
    ///
    /// \return             true iff the minimum score was reached
    ///
    template <typename sink_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    static bool dispatch(
        const aligner_type      aligner,
        const pattern_string    pattern,
        const qual_string       quals,
        const text_string       text,
        const  int32            min_score,
        const uint32            window_begin,
        const uint32            window_end,
              sink_type&        sink,
              column_type       column)
    {
        typedef typename pattern_string::value_type    symbol_type;

        NVBIO_VAR_UNUSED const uint32 BAND_LEN = sw_bandlen_selector<TYPE,1u,symbol_type>::BAND_LEN;

        priv::SWScoringContext<BAND_LEN,TYPE> context;

        return sw_alignment_score_dispatch<BAND_LEN,TYPE,symbol_type>::run( aligner.scheme, context, pattern, quals, text, min_score, sink, window_begin, window_end, column );
    }
};

///
/// Calculate the alignment score between a pattern and a text, using the SmithWaterman algorithm.
///
/// \tparam CHECKPOINTS         number of columns between each checkpoint
/// \tparam TYPE                the alignment type
/// \tparam pattern_string      pattern string 
/// \tparam quals_string        pattern qualities
/// \tparam text_string         text string
/// \tparam column_type         temporary column storage
///
template <
    uint32          CHECKPOINTS,
    AlignmentType   TYPE,
    typename        scoring_type,
    typename        pattern_string,
    typename        qual_string,
    typename        text_string,
    typename        column_type>
struct alignment_checkpointed_dispatch<
    CHECKPOINTS,
    SmithWatermanAligner<TYPE,scoring_type>,
    pattern_string,
    qual_string,
    text_string,
    column_type>
{
    typedef SmithWatermanAligner<TYPE,scoring_type> aligner_type;

    ///
    /// Calculate a set of checkpoints of the DP matrix for the alignment between a pattern
    /// and a text, using the Smith-Waterman algorithm.
    ///
    /// \tparam checkpoint_type     a class to represent the collection of checkpoints,
    ///                             represented as a linear array storing each checkpointed
    ///                             band contiguously.
    ///                             The class has to provide the const indexing operator[].
    ///
    /// \param aligner      scoring scheme
    /// \param pattern      pattern string (horizontal
    /// \param quals        pattern qualities
    /// \param text         text string (vertical)
    /// \param min_score    minimum score
    /// \param sink         output alignment sink
    /// \param checkpoints  output checkpoints
    ///
    /// \return             true iff the minimum score was reached
    ///
    template <
        typename    sink_type,
        typename    checkpoint_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    static
    void dispatch_checkpoints(
        const aligner_type      aligner,
        const pattern_string    pattern,
        const qual_string       quals,
        const text_string       text,
        const  int32            min_score,
              sink_type&        sink,
        checkpoint_type         checkpoints,
              column_type       column)
    {
        typedef typename pattern_string::value_type    symbol_type;

        NVBIO_VAR_UNUSED const uint32 BAND_LEN = sw_bandlen_selector<TYPE,1u,symbol_type>::BAND_LEN;

        priv::SWCheckpointContext<BAND_LEN,TYPE,CHECKPOINTS,checkpoint_type> context( checkpoints );

        sw_alignment_score_dispatch<BAND_LEN,TYPE,symbol_type>::run( aligner.scheme, context, pattern, quals, text, min_score, sink, 0, pattern.length(), column );
    }

    ///
    /// Compute the banded Dynamic Programming submatrix between two given checkpoints,
    /// storing its flow at each cell.
    /// The function returns the submatrix width.
    ///
    /// \tparam BAND_LEN            size of the DP band
    ///
    /// \tparam checkpoint_type     a class to represent the collection of checkpoints,
    ///                             represented as a linear array storing each checkpointed
    ///                             band contiguously.
    ///                             The class has to provide the const indexing operator[].
    ///
    /// \tparam submatrix_type      a class to store the flow submatrix, represented
    ///                             as a linear array of size (BAND_LEN*CHECKPOINTS).
    ///                             The class has to provide the non-const indexing operator[].
    ///                             Note that the submatrix entries can assume only 3 values,
    ///                             and could hence be packed in 2 bits.
    ///
    /// \param checkpoints          the set of checkpointed rows
    /// \param checkpoint_id        the starting checkpoint used as the beginning of the submatrix
    /// \param submatrix            the output submatrix
    ///
    /// \return                     the submatrix width
    ///
    template <
        typename      checkpoint_type,
        typename      submatrix_type>
    NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
    static
    uint32 dispatch_submatrix(
        const aligner_type      aligner,
        const pattern_string    pattern,
        const qual_string       quals,
        const text_string       text,
        const int32             min_score,
        checkpoint_type         checkpoints,
        const uint32            checkpoint_id,
        submatrix_type          submatrix,
        column_type             column)
    {
        typedef typename pattern_string::value_type     symbol_type;

        NVBIO_VAR_UNUSED const uint32 BAND_LEN = sw_bandlen_selector<TYPE,1u,symbol_type>::BAND_LEN;

        priv::SWSubmatrixContext<BAND_LEN,TYPE,CHECKPOINTS,checkpoint_type,submatrix_type>
            context( checkpoints, checkpoint_id, submatrix );

        const uint32 window_begin = checkpoint_id * CHECKPOINTS;
        const uint32 window_end   = nvbio::min( window_begin + CHECKPOINTS, uint32(pattern.length()) );

        NullSink null_sink;
        sw_alignment_score_dispatch<BAND_LEN,TYPE,symbol_type>::run( aligner.scheme, context, pattern, quals, text, min_score, null_sink, window_begin, window_end, column );

        return window_end - window_begin;
    }
};

///
/// Given the Dynamic Programming submatrix between two checkpoints,
/// backtrace from a given destination cell, using a Smith-Waterman scoring.
/// The function returns the resulting source cell.
///
/// \tparam BAND_LEN            size of the DP band
///
/// \tparam CHECKPOINTS         number of DP rows between each checkpoint
///
/// \tparam checkpoint_type     a class to represent the collection of checkpoints,
///                             represented as a linear array storing each checkpointed
///                             band contiguously.
///                             The class has to provide the const indexing operator[].
///
/// \tparam submatrix_type      a class to store the flow submatrix, represented
///                             as a linear array of size (BAND_LEN*CHECKPOINTS).
///                             The class has to provide the const indexing operator[].
///                             Note that the submatrix entries can assume only 3 values,
///                             and could hence be packed in 2 bits.
///
/// \tparam output_type         a class to store the resulting list of backtracking operations.
///                             Needs to provide a single method:
///                                 void push(uint8 op)
///
/// \param checkpoints          precalculated checkpoints
/// \param checkpoint_id        index of the first checkpoint defining the DP submatrix,
///                             storing all bands between checkpoint_id and checkpoint_id+1.
/// \param submatrix            precalculated flow submatrix
/// \param submatrix_height     submatrix width
/// \param submatrix_height     submatrix height
/// \param sink                 in/out sink of the DP solution
/// \param output               backtracking output handler
///
/// \return                     true if the alignment source has been found, false otherwise
///
template <
    uint32          CHECKPOINTS,
    AlignmentType   TYPE,
    typename        scoring_type,
    typename        checkpoint_type,
    typename        submatrix_type,
    typename        output_type>
NVBIO_FORCEINLINE NVBIO_HOST_DEVICE
bool alignment_traceback(
    const SmithWatermanAligner<TYPE,scoring_type>   aligner,
    checkpoint_type                                 checkpoints,
    const uint32                                    checkpoint_id,
    submatrix_type                                  submatrix,
    const uint32                                    submatrix_width,
    const uint32                                    submatrix_height,
          uint8&                                    state,
          uint2&                                    sink,
    output_type&                                    output)
{
    //
    // Backtrack from the second checkpoint to the first looking up the flow submatrix.
    //
    int32 current_row = sink.x;
    int32 current_col = sink.y - checkpoint_id*CHECKPOINTS - 1u;

    NVBIO_CUDA_DEBUG_ASSERT( current_row >  0 &&
                             current_row <= submatrix_height,    "sw::alignment_backtrack(): sink (%u,%u) -> local x coordinate %d not in [0,%d[\n", sink.x, sink.y, current_row, submatrix_height );
    NVBIO_CUDA_DEBUG_ASSERT( current_col >= 0,                 "sw::alignment_backtrack(): sink (%u,%u) -> local y coordinate %d not in [0,%u[ (checkpt %u)\n", sink.x, sink.y, current_col, submatrix_width, checkpoint_id );
    NVBIO_CUDA_DEBUG_ASSERT( current_col <  submatrix_width,  "sw::alignment_backtrack(): sink (%u,%u) -> local y coordinate %d not in [0,%u[ (checkpt %u)\n", sink.x, sink.y, current_col, submatrix_width, checkpoint_id );

    while (current_row >  0 &&
           current_col >= 0)
    {
        const uint8 op = submatrix[ current_col * submatrix_height + (current_row-1u) ];

        if (TYPE == LOCAL)
        {
            if (op == SINK)
            {
                sink.x = current_row;
                sink.y = current_col + checkpoint_id*CHECKPOINTS + 1u;
                return true;
            }
        }

        if (op != DELETION)
            --current_col;   // move to the previous column

        if (op != INSERTION)
            --current_row; // move to the previous row

        output.push( op );
    }
    sink.x = current_row;
    sink.y = current_col + checkpoint_id*CHECKPOINTS + 1u;
    return current_row ? false : true; // if current_row == 0 we reached the end of the alignment along the text
}

/// @} // end of private group

} // namespace priv

} // namespace aln
} // namespace nvbio
