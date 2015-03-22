/*
 *  chiTCP - A simple, testable TCP stack
 *
 *  Functions to utilize circular buffer structure
 */

/*
 *  Copyright (c) 2013-2014, The University of Chicago
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of The University of Chicago nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "chitcp/buffer.h"

int circular_buffer_init(circular_buffer_t *buf, uint32_t maxsize)
{
    //printf("Entering init...");
    buf->data = (uint8_t*)malloc(sizeof(uint8_t) * maxsize);
    buf->start = 0;
    buf->end = 0;

    buf->seq_initial = 0;
    buf->seq_start = 0;
    buf->seq_end = 0;

    buf->rw_pending = 0;
    buf->closing = 0;

    buf->n_bytes = 0;
    buf->maxsize = maxsize;


    pthread_mutex_init(&buf->lock, NULL);
    //pthread_mutex_setpshared(PTHREAD_PROCESS_SHARED);
    //printf("Leaving init\n");
    //circular_buffer_dump(buf);
    return CHITCP_OK;
}

int circular_buffer_set_seq_initial(circular_buffer_t *buf, uint32_t seq_initial)
{
    buf->seq_initial = seq_initial;
    buf->seq_start = seq_initial;
    buf->seq_end = seq_initial + buf->end;

    return CHITCP_OK;
}


int circular_buffer_write(circular_buffer_t *buf, uint8_t *data, uint32_t len, bool_t blocking)
{
    int bs_towrite, bs_remaining, bs_over;
    bs_towrite = 0;
    bs_remaining = len;

    pthread_mutex_lock(&buf->lock);
    //printf("Entering write, len = %i\n", len);
    //circular_buffer_dump(buf);

    if (buf == NULL || buf->closing == 1) {
        pthread_mutex_unlock(&buf->lock);
        return 0;
    }

    if (blocking == BUFFER_NONBLOCKING) {
        if ((buf->maxsize - buf->n_bytes) < bs_remaining) {
            //printf("W: chitcp ewouldblock\n");
            pthread_mutex_unlock(&buf->lock);
            return CHITCP_EWOULDBLOCK;
        }
        bs_towrite = bs_remaining;
        bs_over = (buf->end + bs_towrite) - buf->maxsize;
        if (bs_over >= 0) {
            memcpy(buf->data + buf->end, data, bs_towrite - bs_over);
            memcpy(buf->data, data + (bs_towrite - bs_over), bs_over);
            buf->end = bs_over;
        }
        else { 
            memcpy(buf->data + buf->end, data, bs_towrite);
            buf->end += bs_towrite;
        }
        buf->seq_end += bs_towrite;
        buf->n_bytes += bs_towrite;
    }

    else {
        buf->rw_pending++;
        while (bs_remaining != 0) {
            if (buf == NULL || buf->closing == 1) {
                buf->rw_pending--;
                pthread_mutex_unlock(&buf->lock);
                return len - bs_remaining;
            }
            else if ((buf->maxsize - buf->n_bytes) == 0) {
                pthread_mutex_unlock(&buf->lock);
                pthread_yield();
                pthread_mutex_lock(&buf->lock);
            }
            else {
                if ((buf->maxsize - buf->n_bytes) < bs_remaining) {
                    bs_towrite = buf->maxsize - buf->n_bytes;
                    bs_remaining -= bs_towrite;
                }
                else {
                    bs_towrite = bs_remaining;
                    bs_remaining = 0;
                }
                bs_over = (buf->end + bs_towrite) - buf->maxsize;
                if (bs_over >= 0) {
                    memcpy(buf->data + buf->end, data, bs_towrite - bs_over);
                    memcpy(buf->data, data + (bs_towrite - bs_over), bs_over);
                    buf->end = bs_over;
                }
                else { 
                    memcpy(buf->data + buf->end, data, bs_towrite);
                    buf->end += bs_towrite;
                }
                buf->seq_end += bs_towrite;
                buf->n_bytes += bs_towrite;
            }
        }
        buf->rw_pending--;
    }
    //printf("Leaving write\n");
    //circular_buffer_dump(buf);
    pthread_mutex_unlock(&buf->lock);
    return len;
}

int circular_buffer_read(circular_buffer_t *buf, uint8_t *dst, uint32_t len, bool_t blocking)
{
    int bs_toread, bs_over;

    pthread_mutex_lock(&buf->lock);
    //printf("Entering read, len = %i\n", len);
    //circular_buffer_dump(buf);
    if (buf == NULL || buf->closing == 1) {
        return 0;
    }

    buf->rw_pending++;
    while (buf->n_bytes == 0) {
        if (blocking == BUFFER_NONBLOCKING) {
            buf->rw_pending--;
            pthread_mutex_unlock(&buf->lock);
            //printf("R: chitcp ewouldblock\n");
            return CHITCP_EWOULDBLOCK;    
        }
        else if (buf == NULL || buf->closing == 1) {
            buf->rw_pending--;
            pthread_mutex_unlock(&buf->lock);
            return 0;
        }
        else {
            pthread_mutex_unlock(&buf->lock);
            pthread_yield();
            pthread_mutex_lock(&buf->lock);
        }
    }
    buf->rw_pending--;
    if (len < buf->n_bytes) 
        bs_toread = len;
    else 
        bs_toread = buf->n_bytes;

    bs_over = (buf->start + bs_toread) - buf->maxsize;
    if (bs_over >= 0) {
        if(dst) memcpy(dst, buf->data + buf->start, bs_toread - bs_over);
        if(dst) memcpy(dst + (bs_toread - bs_over), buf->data, bs_over);
        buf->start = bs_over;
    }
    else {
        if(dst) memcpy(dst, buf->data + buf->start, bs_toread);
        buf->start += bs_toread;
    }
    buf->seq_start += bs_toread;
    buf->n_bytes -= bs_toread;
    //printf("Leaving read\n");
    //circular_buffer_dump(buf);
    pthread_mutex_unlock(&buf->lock);
    return bs_toread;
}

int circular_buffer_peek(circular_buffer_t *buf, uint8_t *dst, uint32_t len, bool_t blocking)
{
    int bs_toread, bs_over;

    pthread_mutex_lock(&buf->lock);
    //printf("Entering peek, len = %i\n", len);
    if (buf == NULL || buf->closing == 1) {
        return 0;
    }
    
    buf->rw_pending++;
    while (buf->n_bytes == 0) {
        if (blocking == BUFFER_NONBLOCKING) {
            buf->rw_pending--;
            pthread_mutex_unlock(&buf->lock);
            //printf("P: chitcp ewouldblock\n");
            return CHITCP_EWOULDBLOCK;    
        }
        else if (buf == NULL || buf->closing == 1) {
            buf->rw_pending--;
            pthread_mutex_unlock(&buf->lock);
            return 0;
        }
        else {
            pthread_mutex_unlock(&buf->lock);
            pthread_yield();
            pthread_mutex_lock(&buf->lock);
        }
    }
    buf->rw_pending--;
    if (len < buf->n_bytes) 
        bs_toread = len;
    else 
        bs_toread = buf->n_bytes;

    bs_over = (buf->start + bs_toread) - buf->maxsize;
    if (bs_over >= 0) {
        if(dst) memcpy(dst, buf->data + buf->start, bs_toread - bs_over);
        if(dst) memcpy(dst + (bs_toread - bs_over), buf->data, bs_over);
    }
    else {
        if(dst) memcpy(dst, buf->data + buf->start, bs_toread);
    }
    //printf("Leaving peek\n");
    pthread_mutex_unlock(&buf->lock);
    return bs_toread;
}

int circular_buffer_first(circular_buffer_t *buf)
{
    return buf->seq_start;
}

int circular_buffer_next(circular_buffer_t *buf)
{
    return buf->seq_end;
}

int circular_buffer_capacity(circular_buffer_t *buf)
{
    return buf->maxsize;
}

int circular_buffer_count(circular_buffer_t *buf)
{
    return buf->n_bytes;
}

int circular_buffer_available(circular_buffer_t *buf)
{
    return buf->maxsize - buf->n_bytes;
}

int circular_buffer_dump(circular_buffer_t *buf)
{
    printf("# # # # # # # # # # # # # # # # #\n");

    printf("maxsize: %i, n_bytes: %i\n", buf->maxsize, buf->n_bytes);
    printf("start: %i, end: %i\n", buf->start, buf->end);

    for(int i=0; i<buf->maxsize; i++)
    {
        printf("data[%i] = %i", i, buf->data[i]);
        if(i==buf->start)
            printf("  <<< START");
        if(i==buf->end)
            printf("  <<< END");
        printf("\n");
    }
    printf(" # # # # # # # # # # # # # # # # #\n");

    return CHITCP_OK;
}

int circular_buffer_close(circular_buffer_t *buf)
{
    //printf("Entering close\n");

    pthread_mutex_lock(&buf->lock);
    buf->closing = 1;
    pthread_mutex_unlock(&buf->lock);

    while (buf->rw_pending > 0) {
        pthread_yield();
    }
    circular_buffer_free(buf);

    //printf("Leaving close\n");
    return CHITCP_OK;
}

int circular_buffer_free(circular_buffer_t *buf)
{
 
    pthread_mutex_lock(&buf->lock);
    free(buf->data);
    pthread_mutex_unlock(&buf->lock);
    pthread_mutex_destroy(&buf->lock);
    //free(buf); // DOES NOT like this -- double free or corruption error?

    return CHITCP_OK;
}

