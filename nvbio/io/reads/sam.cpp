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

#include <stdlib.h>
#include <string.h>
#include <zlib/zlib.h>

#include <nvbio/basic/console.h>
#include <nvbio/io/reads/sam.h>

namespace nvbio {
namespace io {

ReadDataFile_SAM::ReadDataFile_SAM(const char *read_file_name,
                                   const uint32 max_reads,
                                   const uint32 truncate_read_len)
  : ReadDataFile(max_reads, truncate_read_len)
{
    fp = gzopen(read_file_name, "rt");
    if (fp == Z_NULL)
    {
        // this will cause init() to fail below
        log_error(stderr, "unable to open SAM file %s\n", read_file_name);
        m_file_state = FILE_OPEN_FAILED;
    } else {
        m_file_state = FILE_OK;
    }

    linebuf = NULL;
    linebuf_size = 0;
    line_length = 0;

    numLines = 0;

    version = NULL;
    sortOrder = SortOrder_unknown;
}

bool ReadDataFile_SAM::readLine(void)
{
    char *ret;
    int start_file_pos;

    // current position in linebuf that we're writing into
    // used when resizing the buffer
    int cur_buf_pos = 0;

    if (!linebuf)
    {
        // allocate initial buffer
        linebuf_size = LINE_BUFFER_INIT_SIZE;
        linebuf = (char *) malloc(linebuf_size);

        if (linebuf == NULL)
        {
            log_error(stderr, "out of memory reading SAM file\n");
            m_file_state = FILE_STREAM_ERROR;
            return false;
        }
    }

    // stash the (uncompressed) stream position we started reading from
    start_file_pos = gztell(fp);

    for(;;)
    {
        // mark the next-to-last byte with a \0
        // this serves to detect when a buffer is too small for a full line
        linebuf[linebuf_size - 2] = '\0';

        // try to read a full line
        ret = gzgets(fp, &linebuf[cur_buf_pos], linebuf_size - cur_buf_pos);
        if (ret == Z_NULL)
        {
            // EOF
            m_file_state = FILE_EOF;
            return false;
        }

        if (linebuf[linebuf_size - 2] == '\0')
        {
            break;
        } else {
            // buffer is too small, double it
            char *tmp;

            cur_buf_pos = linebuf_size - 1;

            linebuf_size *= 2;
            tmp = (char *) realloc(linebuf, linebuf_size);
            if (tmp == NULL)
            {
                log_error(stderr, "out of memory reading SAM file\n");
                m_file_state = FILE_STREAM_ERROR;
                return false;
            }

            linebuf = tmp;
        }
    }

    // store the byte size of the line we read
    line_length = gztell(fp) - start_file_pos;

    // chop off the newline character if any
    if (linebuf[line_length - 1] == '\n')
    {
        assert(linebuf[line_length] == '\0');
        linebuf[line_length - 1] = '\0';
    }

    numLines++;
    return true;
}

void ReadDataFile_SAM::rewindLine(void)
{
    assert(line_length);
    gzseek(fp, -line_length, SEEK_CUR);
}

// initializes a SAM file
// will consume the SAM header and prepare for fetching reads from the file
// returns false on failure
bool ReadDataFile_SAM::init(void)
{
    bool ret;

    if (m_file_state != FILE_OK)
    {
        // file failed to open
        return false;
    }

    // read the header section
    do {
        ret = readLine();
        if (!ret)
        {
            return false;
        }

        if (linebuf[0] != '@')
        {
            break;
        }

        char *delim;
        delim = strchr(linebuf, '\t');

        if (delim)
        {
            if (strncmp(linebuf, "@HD\t", strlen("@HD\t")) == 0)
            {
                ret = parseHeaderLine(delim + 1);
                if (!ret)
                {
                    return false;
                }
            } else if (strncmp(linebuf, "@SQ\t", strlen("@SQ\t")) == 0) {
                // ignored
                continue;
            } else if (strncmp(linebuf, "@RG\t", strlen("@RG\t")) == 0) {
                // ignored
                continue;
            } else if (strncmp(linebuf, "@PG\t", strlen("@PG\t")) == 0) {
                // ignored
                continue;
            } else if (strncmp(linebuf, "@CO\t", strlen("@CO\t")) == 0) {
                // ignored
                continue;
            } else {
                log_warning(stderr, "SAM file warning: unknown header at line %d\n", numLines);
            }
        } else {
            log_warning(stderr, "SAM file warning: malformed line %d\n", numLines);
        }
    } while(linebuf[0] == '@');

    // rewind the last line
    rewindLine();

    return true;
}

// parse a @HD line from the SAM file
// start points at the first tag of the line
bool ReadDataFile_SAM::parseHeaderLine(char *start)
{
    char *version = NULL;
    char *delim;

    if (numLines != 1)
    {
        log_warning(stderr, "SAM file warning (line %d): @HD not the first line in the header section\n", numLines);
    }

    for(;;)
    {
        // look for the next delimiter
        delim = strchr(start, '\t');

        // zero out the next delimiter if found
        if (delim)
        {
            *delim = 0;
        }

        if (strncmp(start, "VN:", strlen("VN:")) == 0)
        {
            version = strdup(&start[3]);
        } else if (strncmp(start, "SO:", strlen("SO:")) == 0) {
            if(strcmp(&start[3], "unknown") == 0)
            {
                sortOrder = SortOrder_unknown;
            } else if (strcmp(&start[3], "unsorted") == 0) {
                sortOrder = SortOrder_unsorted;
            } else if (strcmp(&start[3], "queryname") == 0) {
                sortOrder = SortOrder_queryname;
            } else if (strcmp(&start[3], "coordinate") == 0) {
                sortOrder = SortOrder_coordinate;
            } else {
                log_warning(stderr, "SAM file warning (line %d): invalid sort order %s\n", numLines, &start[3]);
            }
        } else {
            log_warning(stderr, "SAM file warning (line %d): invalid tag %s in @HD\n", numLines, start);
        }

        if (!delim)
        {
            // this was the last token
            break;
        }

        // advance to next token
        start = delim + 1;
    }

    if (version == NULL)
    {
        log_warning(stderr, "SAM file warning (line %d): header does not contain a version tag\n", numLines);
    }

    return true;
}

// fetch the next chunk of reads (up to max_reads) from the file and push it into output
int ReadDataFile_SAM::nextChunk(ReadDataRAM *output, uint32 max_reads)
{
    char *name;
    char *flag;
    char *rname;
    char *pos;
    char *mapq;
    char *cigar;
    char *rnext;
    char *pnext;
    char *tlen;
    char *seq;
    char *qual;

    uint32 read_flags;

    // find the next primary alignment from the file
    do {
        // get next line from file
        if (readLine() == false)
        {
            return 0;
        }

// ugly macro to tokenize the string based on strchr
#define NEXT(prev, next)                        \
    {                                           \
        next = strchr(prev, '\t');              \
        if (!next) {                                                    \
            log_error(stderr, "Error parsing SAM file (line %d): incomplete alignment section\n", numLines); \
            m_file_state = FILE_PARSE_ERROR;                            \
            return -1;                                                  \
        }                                                               \
        *next = '\0';                                                   \
        next++;                                                         \
    }

        // first token is just the start of the string
        name = linebuf;

        // for all remaining tokens, locate the next token based on the previous
        NEXT(name, flag);
        NEXT(flag, rname);
        NEXT(rname, pos);
        NEXT(pos, mapq);
        NEXT(mapq, cigar);
        NEXT(cigar, rnext);
        NEXT(rnext, pnext);
        NEXT(pnext, tlen);
        NEXT(tlen, seq);
        NEXT(seq, qual);

#undef NEXT

        // figure out what the flag value is
        read_flags = strtol(flag, NULL, 0);
    } while(read_flags & SAMFlag_SecondaryAlignment);

    uint32 conversion_flags = 0;

    if (read_flags & SAMFlag_ReverseComplemented)
    {
        conversion_flags = ReadDataRAM::REVERSE | ReadDataRAM::COMPLEMENT;
    }

    // add the read
    output->push_back(uint32(strlen(seq)),
                      name,
                      (uint8*)seq,
                      (uint8*)qual,
                      Phred33,
                      m_truncate_read_len,
                      conversion_flags);

    // we always input 1 read at a time here
    return 1;
}

} // namespace io
} // namespace nvbio