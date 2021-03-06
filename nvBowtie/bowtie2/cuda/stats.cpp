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

#include <nvBowtie/bowtie2/cuda/stats.h>
#include <nvbio/basic/html.h>
#include <functional>
#include <algorithm>
#include <numeric>

#ifndef WIN32
#include <string>
#endif


namespace nvbio {
namespace bowtie2 {
namespace cuda {

void generate_kernel_table(const char* report, const KernelStats& stats);

Stats::Stats(const Params& params_) :
    n_reads(0),
    n_mapped(0),
    n_unique(0),
    n_multiple(0),
    n_ambiguous(0),
    n_nonambiguous(0),
    mapped( 4096, 0 ),
    f_mapped( 4096, 0 ),
    r_mapped( 4096, 0 ),
    stats_ready( false ),
    params( params_ )
{
    global_time = 0.0f;

    hits_total        = 0u;
    hits_ranges       = 0u;
    hits_max          = 0u;
    hits_max_range    = 0u;
    hits_top_total    = 0u;
    hits_top_max      = 0u;
    hits_stats        = 0u;

    for (uint32 i = 0; i < 28; ++i)
        hits_bins[i] = hits_top_bins[i] = 0;

    for (uint32 i = 0; i < 64; ++i)
        mapq_bins[i] = 0;

    for (uint32 i = 0; i < 64; ++i)
        for (uint32 j = 0; j < 64; ++j)
            mapped2[i][j] = 0;

    map.name                = "map";                map.units                   = "reads";
    select.name             = "select";             select.units                = "seeds";
    sort.name               = "sort";               sort.units                  = "seeds";
    locate.name             = "locate";             locate.units                = "seeds";
    score.name              = "score";              score.units                 = "seeds";
    opposite_score.name     = "score-opposite";     opposite_score.units        = "seeds";
    backtrack.name          = "backtrack";          backtrack.units             = "reads";
    backtrack_opposite.name = "backtrack-opposite"; backtrack_opposite.units    = "reads";
    finalize.name           = "finalize";           finalize.units              = "reads";
    alignments_DtoH.name    = "alignments-DtoH";    alignments_DtoH.units       = "reads";
    read_HtoD.name          = "reads-HtoD";         read_HtoD.units             = "reads";
    read_io.name            = "reads-IO";           read_io.units               = "reads";
    io.name                 = "IO";                 io.units                    = "reads";

    opposite_score.user_names[0] = "queue::get utilization"; opposite_score.user_avg[0] = true;
    opposite_score.user_names[1] = "queue::run utilization"; opposite_score.user_avg[1] = true;
    opposite_score.user_names[2] = "queue::run T_avg";       opposite_score.user_avg[2] = true;
    opposite_score.user_names[3] = "queue::run T_sigma";     opposite_score.user_avg[3] = false;
}

namespace { // anonymous

std::string generate_file_name(const char* report, const char* name)
{
    std::string file_name = report;
    {
        const size_t offset = file_name.find(".html");
        file_name.replace( offset+1, file_name.length() - offset - 1, name );
        file_name.append( ".html" );
    }
    return file_name;
}

// return the local file name from a path
//
const char* local_file(const std::string& file_name)
{
  #if WIN32
    const size_t pos = file_name.find_last_of("/\\");
  #else
    const size_t pos = file_name.rfind('/');
  #endif

    if (pos == std::string::npos)
        return file_name.c_str();
    else
        return file_name.c_str() + pos + 1u;
}

void add_param(FILE* html_output, const char* name, const uint32 val, bool alt)
{
    html::tr_object tr( html_output, "class", alt ? "alt" : "none", NULL );
    html::th_object( html_output, html::FORMATTED, NULL, name );
    html::td_object( html_output, html::FORMATTED, NULL, "%u", val );
}
void add_param(FILE* html_output, const char* name, const float val, bool alt)
{
    html::tr_object tr( html_output, "class", alt ? "alt" : "none", NULL );
    html::th_object( html_output, html::FORMATTED, NULL, name );
    html::td_object( html_output, html::FORMATTED, NULL, "%f", val );
}
void add_param(FILE* html_output, const char* name, const char* val, bool alt)
{
    html::tr_object tr( html_output, "class", alt ? "alt" : "none", NULL );
    html::th_object( html_output, html::FORMATTED, NULL, name );
    html::td_object( html_output, html::FORMATTED, NULL, "%s", val );
}

void stats_string(char* buffer, const uint32 px, const char* units, const float v, const float p, const float range)
{
    sprintf(buffer,"<span><statnum style=\"width:%upx;\">%.1f %s</statnum> <statbar style=\"width:%.1f%%%%\">\'</statbar></span>", px, v, units, 2.0f + range * p);
}

} // anonymous namespace

void generate_report(Stats& stats, const char* report)
{
    if (report == NULL)
        return;

    std::vector<KernelStats*> kernel_stats;
    kernel_stats.push_back( &stats.map );
    kernel_stats.push_back( &stats.select );
    kernel_stats.push_back( &stats.sort );
    kernel_stats.push_back( &stats.locate );
    kernel_stats.push_back( &stats.score );
    kernel_stats.push_back( &stats.opposite_score );
    kernel_stats.push_back( &stats.backtrack );
    kernel_stats.push_back( &stats.backtrack_opposite );
    kernel_stats.push_back( &stats.finalize );
    kernel_stats.push_back( &stats.alignments_DtoH );
    kernel_stats.push_back( &stats.read_HtoD );
    kernel_stats.push_back( &stats.read_io );

    if (stats.params.keep_stats)
    {
        for (uint32 i = 0; i < kernel_stats.size(); ++i)
            generate_kernel_table( report, *kernel_stats[i] );
    }

    FILE* html_output = fopen( report, "w" );
    if (html_output == NULL)
    {
        log_warning( stderr, "unable to write HTML report \"%s\"\n", report );
        return;
    }

    {
        const uint32 n_mapped = stats.n_mapped;
        const uint32 n_reads  = stats.n_reads;

        html::html_object html( html_output );
        {
            const char* meta_list = "<meta http-equiv=\"refresh\" content=\"2\" />";

            html::header_object hd( html_output, "Bowtie2 Report", html::style(), meta_list );
            {
                html::body_object body( html_output );

                //
                // params
                //
                {
                    html::table_object table( html_output, "params", "params", "parameters" );
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "parameter name" );
                        html::th_object( html_output, html::FORMATTED, NULL, "value" );
                    }
                    add_param( html_output, "randomized",  stats.params.randomized ? "yes" : "no", true );
                    add_param( html_output, "N",           stats.params.allow_sub,                 false );
                    add_param( html_output, "seed-len",    stats.params.seed_len,                  true );
                    add_param( html_output, "subseed-len", stats.params.subseed_len,               false );
                    add_param( html_output, "seed-freq",   stats.params.seed_freq,                 true );
                    add_param( html_output, "max-reseed",  stats.params.max_reseed,                false );
                    add_param( html_output, "rep-seeds",   stats.params.rep_seeds,                 true );
                    add_param( html_output, "max-hits",    stats.params.max_hits,                  false );
                    add_param( html_output, "max-dist",    stats.params.max_dist,                  true );
                    add_param( html_output, "max-effort",  stats.params.max_effort,                false );
                    add_param( html_output, "min-ext",     stats.params.min_ext,                   true );
                    add_param( html_output, "max-ext",     stats.params.max_ext,                   false );
                    add_param( html_output, "mapQ-filter", stats.params.mapq_filter,               true );
                    add_param( html_output, "scoring",     stats.params.scoring_file.c_str(),      false );
                    add_param( html_output, "report",      stats.params.report.c_str(),            true );
                }
                //
                // speed stats
                //
                {
                    char span_string[1024];

                    html::table_object table( html_output, "speed-stats", "stats", "speed stats" );
                    float  worst_time[2] = {0};
                    uint32 worst[2]      = {0};

                    for (uint32 i = 0; i < kernel_stats.size(); ++i)
                    {
                        const float time = kernel_stats[i]->time;

                        if (worst_time[0] < time)
                        {
                            worst_time[1] = worst_time[0];
                            worst[1]      = worst[0];
                            worst_time[0] = time;
                            worst[0]      = i;
                        }
                        else if (worst_time[1] < time)
                        {
                            worst_time[1] = time;
                            worst[1]      = i;
                        }
                    }
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "" );
                        html::th_object( html_output, html::FORMATTED, NULL, "time" );
                        html::th_object( html_output, html::FORMATTED, NULL, "avg speed" );
                        html::th_object( html_output, html::FORMATTED, NULL, "max speed" );
                    }
                    {
                        html::tr_object tr( html_output, "class", "alt", NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "total" );
                        html::td_object( html_output, html::FORMATTED, "class", "red", NULL, "%.1f s", stats.global_time );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f K reads/s", 1.0e-3f * float(n_reads)/stats.global_time );
                        html::td_object( html_output, "-", NULL );
                    }
                    for (uint32 i = 0; i < kernel_stats.size(); ++i)
                    {
                        const KernelStats&     kstats = *kernel_stats[i];
                        const char*            name = kstats.name.c_str();
                        const char*            units = kstats.units.c_str();
                        const std::string file_name = generate_file_name( report, name );

                        char link_name[1024];
                        sprintf( link_name, "<a href=\"%s\">%s</a>", local_file( file_name ), name );
                        const char* cls = worst[0] == i ? "yellow" : worst[1] == i ? "orange" : "none";
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, link_name );
                        stats_string( span_string, 40, "s", kstats.time, kstats.time / stats.global_time, 75.0f );
                        html::td_object( html_output, html::FORMATTED, "class", cls, NULL, span_string );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.2f M %s/s", 1.0e-6f * float(kstats.calls)/kstats.time, units );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.2f M %s/s", 1.0e-6f * kstats.max_speed, units );
                    }
                }
                //
                // mapping stats
                //
                {
                    html::table_object table( html_output, "mapping-stats", "stats", "mapping stats" );
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "" );
                        html::th_object( html_output, html::FORMATTED, NULL, "mapped" );
                        html::th_object( html_output, html::FORMATTED, NULL, "ambiguous" );
                        html::th_object( html_output, html::FORMATTED, NULL, "multiple" );
                    }
                    {
                        html::tr_object tr( html_output, "class", "alt", NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "reads" );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f %%", 100.0f * float(n_mapped)/float(n_reads) );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f %%", 100.0f * float(stats.n_ambiguous)/float(n_reads) );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f %%", 100.0f * float(stats.n_multiple)/float(n_reads) );
                    }
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "edit distance" );
                        html::th_object( html_output, html::FORMATTED, NULL, "total" );
                        html::th_object( html_output, html::FORMATTED, NULL, "forward" );
                        html::th_object( html_output, html::FORMATTED, NULL, "reverse" );
                    }
                    uint32 best_bin[2]     = {0};
                    uint32 best_bin_val[2] = {0};
                    for (uint32 i = 0; i < stats.mapped.size(); ++i)
                    {
                        if (best_bin_val[0] < stats.mapped[i])
                        {
                            best_bin_val[1] = best_bin_val[0];
                            best_bin[1]     = best_bin[0];
                            best_bin_val[0] = stats.mapped[i];
                            best_bin[0]     = i;
                        }
                        else if (best_bin_val[1] < stats.mapped[i])
                        {
                            best_bin_val[1] = stats.mapped[i];
                            best_bin[1]     = i;
                        }
                    }
                    for (uint32 i = 0; i < stats.mapped.size(); ++i)
                    {
                        if (float(stats.mapped[i])/float(n_reads) < 1.0e-3f)
                            continue;

                        html::tr_object tr( html_output, "class", i % 2 ? "none" : "alt", NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "%u", i );
                        const char* cls = i == best_bin[0] ? "yellow" : i == best_bin[1] ? "orange" : "none";
                        html::td_object( html_output, html::FORMATTED, "class", cls, NULL, "%.1f %%", 100.0f * float(stats.mapped[i])/float(n_reads) );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f %%", 100.0f * float(stats.f_mapped[i])/float(n_reads) );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f %%", 100.0f * float(stats.r_mapped[i])/float(n_reads) );
                    }
                }
                //
                // mapping quality stats
                //
                {
                    html::table_object table( html_output, "mapping-quality-stats", "stats", "mapping quality stats" );
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "mapQ" );
                        html::th_object( html_output, html::FORMATTED, NULL, "percentage" );
                    }

                    // rebin to a logarithmic scale
                    uint64 bins[7] = {0};
                    for (uint32 i = 0; i < 64; ++i)
                    {
                        const uint32 log_mapq = i ? nvbio::log2(i) + 1 : 0;
                        bins[log_mapq] += stats.mapq_bins[i];
                    }

                    // compute best bins
                    uint32 best_bin[2]     = {0};
                    uint64 best_bin_val[2] = {0};
                    for (uint32 i = 0; i < 7; ++i)
                    {
                        if (best_bin_val[0] < bins[i])
                        {
                            best_bin_val[1] = best_bin_val[0];
                            best_bin[1]     = best_bin[0];
                            best_bin_val[0] = bins[i];
                            best_bin[0]     = i;
                        }
                        else if (best_bin_val[1] < bins[i])
                        {
                            best_bin_val[1] = bins[i];
                            best_bin[1]     = i;
                        }
                    }

                    // output html table
                    for (uint32 i = 0; i < 7; ++i)
                    {
                        const uint32 bin_size = 1u << (i-1);

                        char buffer[1024];
                        if (i <= 1)
                            sprintf( buffer, "%u", i );
                        else if (bin_size < 1024)
                            sprintf( buffer, "%u - %u", bin_size, bin_size*2-1 );

                        html::tr_object tr( html_output, "class", i % 2 ? "none" : "alt", NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, buffer );
                        const char* cls = i == best_bin[0] ? "yellow" : i == best_bin[1] ? "orange" : "none";
                        html::td_object( html_output, html::FORMATTED, "class", cls, NULL, "%.1f %%", 100.0f * float(bins[i])/float(n_reads) );
                    }
                }
                //
                // best2-mapping stats
                //
                {
                    // compute best 2 entries among double alignments
                    uint2  best_bin2[2] = { make_uint2(0,0) };
                    uint32 best_bin2_val[2] = { 0 };

                    for (uint32 i = 1; i <= 16; ++i)
                    {
                        for (uint32 j = 1; j <= 16; ++j)
                        {
                            if (best_bin2_val[0] < stats.mapped2[i][j])
                            {
                                best_bin2_val[1] = best_bin2_val[0];
                                best_bin2[1]     = best_bin2[0];
                                best_bin2_val[0] = stats.mapped2[i][j];
                                best_bin2[0]     = make_uint2(i,j);
                            }
                            else if (best_bin2_val[1] < stats.mapped2[i][j])
                            {
                                best_bin2_val[1] = stats.mapped2[i][j];
                                best_bin2[1]     = make_uint2(i,j);
                            }
                        }
                    }

                    // compute best 2 entries among single alignments
                    uint2  best_bin1[2] = { make_uint2(0,0) };
                    uint32 best_bin1_val[2] = { 0 };

                    for (uint32 i = 0; i <= 16; ++i)
                    {
                        if (best_bin1_val[0] < stats.mapped2[i][0])
                        {
                            best_bin1_val[1] = best_bin1_val[0];
                            best_bin1[1]     = best_bin1[0];
                            best_bin1_val[0] = stats.mapped2[i][0];
                            best_bin1[0]     = make_uint2(i,0);
                        }
                        else if (best_bin1_val[1] < stats.mapped2[i][0])
                        {
                            best_bin1_val[1] = stats.mapped2[i][0];
                            best_bin1[1]     = make_uint2(i,0);
                        }
                    }

                    html::table_object table( html_output, "best2-mapping-stats", "stats", "best2 mapping stats" );
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "" );
                        for (uint32 i = 0; i <= 16; ++i)
                            html::th_object( html_output, html::FORMATTED, NULL, (i == 0 ? "-" : "%u"), i-1 );
                    }
                    for (uint32 i = 0; i <= 16; ++i)
                    {
                        html::tr_object tr( html_output, "class", i % 2 ? "none" : "alt", NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, (i == 0 ? "-" : "%u"), i-1 );

                        for (uint32 j = 0; j <= 16; ++j)
                        {
                            if (100.0f * float(stats.mapped2[i][j])/float(n_reads) >= 0.1f)
                            {
                                const char* cls = ((i == best_bin1[0].x && j == best_bin1[0].y) ||
                                                   (i == best_bin2[0].x && j == best_bin2[0].y)) ? "yellow" :
                                                  ((i == best_bin1[1].x && j == best_bin1[1].y) ||
                                                   (i == best_bin2[1].x && j == best_bin2[1].y)) ? "orange" :
                                                  (i   == j) ? "pink" :
                                                  (i+1 == j) ? "azure" : "none";
                                html::td_object( html_output, html::FORMATTED, "class", cls, NULL, "%.1f %%", 100.0f * float(stats.mapped2[i][j])/float(n_reads) );
                            }
                            else if (100.0f * float(stats.mapped2[i][j])/float(n_reads) >= 0.01f)
                                html::td_object( html_output, html::FORMATTED, "class", "small", NULL, "%.2f %%", 100.0f * float(stats.mapped2[i][j])/float(n_reads) );
                            else
                            {
                                const char* cls = (i > stats.params.max_dist+1 || j > stats.params.max_dist+1) ? "gray" : "none";
                                html::td_object( html_output, html::FORMATTED, "class", cls, NULL, "-" );
                            }
                        }
                    }
                }
                //
                // seeding stats
                //
                if (stats.params.keep_stats)
                {
                    // poll until stats are being updated
                    //while (stats.stats_ready == false) {}

                    // copy stats locally
                    uint64 hits_total     = stats.hits_total;
                    uint64 hits_ranges    = stats.hits_ranges;
                    uint32 hits_max       = stats.hits_max;
                    uint32 hits_max_range = stats.hits_max_range;
                    uint64 hits_top_total = stats.hits_top_total;
                    uint32 hits_top_max   = stats.hits_top_max;
                    uint64 hits_bins[28];
                    uint64 hits_bins_sum = 0;
                    uint64 hits_top_bins[28];
                    uint64 hits_top_bins_sum = 0;
                    for (uint32 i = 0; i < 28; ++i)
                    {
                         hits_bins_sum     += hits_bins[i]     = stats.hits_bins[i];
                         hits_top_bins_sum += hits_top_bins[i] = stats.hits_top_bins[i];
                    }

                    // mark stats as consumed
                    stats.stats_ready = false;

                    html::table_object table( html_output, "seeding-stats", "stats", "seeding stats" );
                    char buffer[1024];
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "" );
                        html::th_object( html_output, html::FORMATTED, NULL, "seed hits" );
                        html::th_object( html_output, html::FORMATTED, NULL, "top-seed hits" );
                        html::th_object( html_output, html::FORMATTED, NULL, "seed ranges" );
                        html::th_object( html_output, html::FORMATTED, NULL, "range size" );
                    }
                    {
                        html::tr_object tr( html_output, "class", "alt", NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "avg" );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f",  float(hits_total)/float(n_reads) );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f",  float(hits_top_total)/float(n_reads) );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f",  float(hits_ranges)/float(n_reads) );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f",  float(hits_total)/float(hits_ranges) );
                    }
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "max" );
                        html::td_object( html_output, html::FORMATTED, NULL, "%u", hits_max );
                        html::td_object( html_output, html::FORMATTED, NULL, "%u", hits_top_max );
                        html::td_object( html_output, html::FORMATTED, NULL, "" );
                        html::td_object( html_output, html::FORMATTED, NULL, "%u", hits_max_range );
                    }
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "# hits" );
                        html::th_object( html_output, html::FORMATTED, NULL, "%% of seeds" );
                        html::th_object( html_output, html::FORMATTED, NULL, "%% of top-seeds" );
                        html::th_object( html_output, html::FORMATTED, NULL, "" );
                        html::th_object( html_output, html::FORMATTED, NULL, "" );
                    }
                    uint32 max_bin             = 0;
                    uint32 best_bin[2]         = {0};
                    uint64 best_bin_val[2]     = {0};
                    uint32 best_top_bin[2]     = {0};
                    uint64 best_top_bin_val[2] = {0};
                    for (uint32 i = 0; i < 28; ++i)
                    {
                        max_bin = float(hits_bins[i]) / float(hits_bins_sum) > 0.001f ? i : max_bin;
                        max_bin = float(hits_top_bins[i]) / float(hits_top_bins_sum) > 0.001f ? i : max_bin;

                        if (best_bin_val[0] < hits_bins[i])
                        {
                            best_bin_val[1] = best_bin_val[0];
                            best_bin[1]     = best_bin[0];
                            best_bin_val[0] = hits_bins[i];
                            best_bin[0]     = i;
                        }
                        else if (best_bin_val[1] < hits_bins[i])
                        {
                            best_bin_val[1] = hits_bins[i];
                            best_bin[1]     = i;
                        }

                        if (best_top_bin_val[0] < hits_top_bins[i])
                        {
                            best_top_bin_val[1] = best_top_bin_val[0];
                            best_top_bin[1]     = best_top_bin[0];
                            best_top_bin_val[0] = hits_top_bins[i];
                            best_top_bin[0]     = i;
                        }
                        else if (best_top_bin_val[1] < hits_top_bins[i])
                        {
                            best_top_bin_val[1] = hits_top_bins[i];
                            best_top_bin[1]     = i;
                        }
                    }
                    for (uint32 i = 0; i < max_bin; ++i)
                    {
                        const uint32 bin_size = 1u << (i-1);

                        html::tr_object tr( html_output, "class", i % 2 ? "none" : "alt", NULL );
                        if (i <= 1)
                            sprintf( buffer, "%u", i );
                        else if (bin_size < 512)
                            sprintf( buffer, "%u - %u", bin_size, bin_size*2-1 );
                        else if (bin_size == 512)
                            sprintf( buffer, "0.5K - 1K" );
                        else
                            sprintf( buffer, "%uK - %uK", bin_size/1024, bin_size*2/1024 );

                        const char* cls     = i == best_bin[0]     ? "yellow" : i == best_bin[1]     ? "orange" : "none";
                        const char* cls_top = i == best_top_bin[0] ? "yellow" : i == best_top_bin[1] ? "orange" : "none";

                        html::th_object( html_output, html::FORMATTED, NULL, buffer );
                        html::td_object( html_output, html::FORMATTED, "class", cls,     NULL, "%4.1f %%", 100.0f * float(hits_bins[i]) / float(hits_bins_sum) );
                        html::td_object( html_output, html::FORMATTED, "class", cls_top, NULL, "%4.1f %%", 100.0f * float(hits_top_bins[i]) / float(hits_top_bins_sum) );
                        html::td_object( html_output, html::FORMATTED, NULL, "" );
                        html::td_object( html_output, html::FORMATTED, NULL, "" );
                    }
                }
            }
        }
    }
    fclose( html_output );
}

template <typename T>
void find_gt2(const uint32 n, const T* table, uint32 best_bin[2])
{
    best_bin[0] = best_bin[1] = 0;
    T best_bin_val[2] = {0};
    for (uint32 i = 0; i < n; ++i)
    {
        if (best_bin_val[0] < table[i])
        {
            best_bin_val[1] = best_bin_val[0];
            best_bin[1]     = best_bin[0];
            best_bin_val[0] = table[i];
            best_bin[0]     = i;
        }
        else if (best_bin_val[1] < table[i])
        {
            best_bin_val[1] = table[i];
            best_bin[1]     = i;
        }
    }
}

void generate_kernel_table(const char* report, const KernelStats& stats)
{
    const char* name            = stats.name.c_str();
    const char* units           = stats.units.c_str();
    const std::string file_name = generate_file_name( report, name );

    const std::deque< std::pair<uint32,float> >& table = stats.info;

    FILE* html_output = fopen( file_name.c_str(), "w" );
    if (html_output == NULL)
    {
        log_warning( stderr, "unable to write HTML report \"%s\"\n", file_name.c_str() );
        return;
    }

    {
        html::html_object html( html_output );
        {
            const char* meta_list = "<meta http-equiv=\"refresh\" content=\"2\" />";

            html::header_object hd( html_output, "Bowtie2 Report", html::style(), meta_list );
            {
                html::body_object body( html_output );

                //
                // kernel summary stats
                //
                {
                    uint32 bin_calls[32]    = {0};
                    float  bin_sum_time[32] = {0.0f};
                    float  bin_avg_time[32] = {0.0f};
                    float  bin_speed[32]    = {0.0f};

                    float total_time = stats.time;
                    float avg_speed  = total_time ? float(double(stats.calls) / double(stats.time)) : 0.0f;
                    float max_speed  = 0.0f;

                    for (uint32 bin = 0; bin < 32; ++bin)
                    {
                        bin_calls[bin]    = stats.bin_calls[bin];
                        bin_sum_time[bin] = stats.bin_time[bin];
                        bin_avg_time[bin] = stats.bin_calls[bin] ? stats.bin_time[bin] / stats.bin_calls[bin] : 0.0f;
                        bin_speed[bin]    = stats.bin_time[bin]  ? float(double(stats.bin_items[bin]) / double(stats.bin_time[bin])) : 0.0f;
                        //bin_speed[bin]    = stats.bin_calls[bin] ? stats.bin_speed[bin] / stats.bin_calls[bin] : 0.0f;

                        max_speed = std::max( max_speed, bin_speed[bin] );
                    }

                    char buffer1[1024];
                    char buffer2[1024];
                    sprintf( buffer1, "%s-summary-stats", name );
                    sprintf( buffer2, "%s summary stats", name );
                    html::table_object tab( html_output, buffer1, "stats", buffer2 );
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "items" );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.2f M", float(stats.calls) * 1.0e-6f );
                    }
                    // write "user" stats
                    for (uint32 i = 0; i < 32; ++i)
                    {
                        if (stats.user_names[i] == NULL)
                            break;

                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, stats.user_names[i] );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.3f %s",
                            stats.user_avg[i] ? stats.user[i]/float(stats.num) :
                                                stats.user[i],
                            stats.user_units[i] );
                    }
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "batch size (%s)", units );
                        html::th_object( html_output, html::FORMATTED, NULL, "calls" );
                        html::th_object( html_output, html::FORMATTED, NULL, "avg time" );
                        html::th_object( html_output, html::FORMATTED, NULL, "sum time" );
                        html::th_object( html_output, html::FORMATTED, NULL, "cumul. time" );
                        html::th_object( html_output, html::FORMATTED, NULL, "speed" );
                    }

                    uint32 best_avg_bin[2];
                    uint32 best_sum_bin[2];
                    find_gt2( 32, bin_avg_time, best_avg_bin );
                    find_gt2( 32, bin_sum_time, best_sum_bin );

                    float cum_time = 0.0f;

                    float max_avg_time = 0.0f;
                    float max_sum_time = 0.0f;
                    for (uint32 i = 0; i < 32; ++i)
                    {
                        max_avg_time = nvbio::max( bin_avg_time[i], max_avg_time );
                        max_sum_time = nvbio::max( bin_sum_time[i], max_sum_time );
                    }

                    char span_string[1024];
                    for (uint32 i = 0; i < 32; ++i)
                    {
                        if (bin_calls[i] == 0)
                            continue;

                        const float speed = bin_speed[i];

                        cum_time += bin_sum_time[i];

                        const uint32 bin_size = 1u << i;
                        html::tr_object tr( html_output, "class", i % 2 ? "none" : "alt", NULL );
                        if (bin_size == 1)
                            html::th_object( html_output, html::FORMATTED, NULL, "1" );
                        else if (bin_size < 512)
                            html::th_object( html_output, html::FORMATTED, NULL, "%u - %u", bin_size, bin_size*2-1 );
                        else if (bin_size == 512) 
                            html::th_object( html_output, html::FORMATTED, NULL, "512 - 1K" );
                        else if (bin_size < 512*1024)
                            html::th_object( html_output, html::FORMATTED, NULL, "%uK- %uK", bin_size/1024, (bin_size*2)/1024 );
                        else if (bin_size == 512*1024)
                            html::th_object( html_output, html::FORMATTED, NULL, "512K - 1M" );

                        const char* avg_cls = i == best_avg_bin[0] ? "yellow" : i == best_avg_bin[1] ? "orange" : "none";
                        const char* sum_cls = i == best_sum_bin[0] ? "yellow" : i == best_sum_bin[1] ? "orange" : "none";
                        const char* spd_cls = speed == max_speed ? "yellow" : speed < avg_speed * 0.1f ? "red" : speed < max_speed * 0.1f ? "pink" : "none";

                        html::td_object( html_output, html::FORMATTED, NULL, "%u", bin_calls[i] );
                        stats_string( span_string, 60, "ms", 1000.0f * bin_avg_time[i], bin_avg_time[i] / max_avg_time, 50.0f );
                        html::td_object( html_output, html::FORMATTED, "class", avg_cls, NULL, span_string );
                        stats_string( span_string, 60, "ms", 1000.0f * bin_sum_time[i], bin_sum_time[i] / max_sum_time, 50.0f );
                        html::td_object( html_output, html::FORMATTED, "class", sum_cls, NULL, span_string );
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f %%", 100.0f * cum_time / total_time );
                        html::td_object( html_output, html::FORMATTED, "class", spd_cls, NULL, "%.1f %c %s/s", speed * (speed >= 1.0e6f ?  1.0e-6f : 1.0e-3f), speed >= 1.0e6f ? 'M' : 'K', units );
                    }
                }
                //
                // kernel table stats
                //
                {
                    char buffer1[1024];
                    char buffer2[1024];
                    sprintf( buffer1, "%s-stats", name );
                    sprintf( buffer2, "%s stats", name );
                    html::table_object tab( html_output, buffer1, "stats", buffer2 );
                    {
                        html::tr_object tr( html_output, NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "launch" );
                        html::th_object( html_output, html::FORMATTED, NULL, "batch size (%s)", units );
                        html::th_object( html_output, html::FORMATTED, NULL, "time" );
                        html::th_object( html_output, html::FORMATTED, NULL, "speed" );
                    }
                    uint32 best_bin[2]     = {0};
                    float  best_bin_val[2] = {0};
                    for (uint32 i = 0; i < table.size(); ++i)
                    {
                        if (best_bin_val[0] < table[i].second)
                        {
                            best_bin_val[1] = best_bin_val[0];
                            best_bin[1]     = best_bin[0];
                            best_bin_val[0] = table[i].second;
                            best_bin[0]     = i;
                        }
                        else if (best_bin_val[1] < table[i].second)
                        {
                            best_bin_val[1] = table[i].second;
                            best_bin[1]     = i;
                        }
                    }

                    float max_time  = 0.0f;
                    float max_speed = 0.0f;
                    for (uint32 i = 0; i < table.size(); ++i)
                    {
                        const float speed = float(table[i].first) / table[i].second;
                        max_time = nvbio::max( float(table[i].second), max_time );
                        max_speed = nvbio::max( speed, max_speed );
                    }

                    char span_string[1024];
                    char units_string[1024];
                    for (uint32 i = 0; i < table.size(); ++i)
                    {
                        const float speed = float(table[i].first) / table[i].second;
                        html::tr_object tr( html_output, "class", i % 2 ? "none" : "alt", NULL );
                        html::th_object( html_output, html::FORMATTED, NULL, "%u", i );
                        const char* cls = i == best_bin[0] ? "yellow" : i == best_bin[1] ? "orange" : "none";
                        html::td_object( html_output, html::FORMATTED, NULL, "%.1f %c", float(table[i].first) * (table[i].first > 1000000 ?  1.0e-6f : 1.0e-3f), table[i].first > 1000000 ? 'M' : 'K' );
                        stats_string( span_string, 50, "ms", 1000.0f * float(table[i].second), float(table[i].second) / max_time, 50.0f );
                        html::td_object( html_output, html::FORMATTED, "class", cls, NULL, span_string );
                        sprintf(units_string, "%c %s/s", speed > 1000000 ? 'M' : 'K', units );
                        stats_string( span_string, 100, units_string, speed * (speed >= 1.0e6f ? 1.0e-6f : 1.0e-3f), speed / max_speed, 50.0f );
                        html::td_object( html_output, html::FORMATTED, NULL, span_string );
                    }
                }
            }
        }
    }
    fclose( html_output );
}

} // namespace cuda
} // namespace bowtie2
} // namespace nvbio
