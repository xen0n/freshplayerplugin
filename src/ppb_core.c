/*
 * Copyright © 2013-2014  Rinat Ibragimov
 *
 * This file is part of FreshPlayerPlugin.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include "ppb_core.h"
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include "trace.h"
#include "tables.h"
#include "pp_resource.h"
#include "ppb_message_loop.h"


void
ppb_core_add_ref_resource(PP_Resource resource)
{
    pp_resource_ref(resource);
}

void
ppb_core_release_resource(PP_Resource resource)
{
    pp_resource_unref(resource);
}

PP_Time
ppb_core_get_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

PP_TimeTicks
ppb_core_get_time_ticks(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

struct comt_task_s {
    struct PP_CompletionCallback    callback;
    int32_t                     result_to_pass;
    struct timespec             when;
    int                         terminate;
};

static
void
comt_proxy(void *param)
{
    struct comt_task_s *p = param;

    if (p->callback.func) {
        p->callback.func(p->callback.user_data, p->result_to_pass);
    }
    g_slice_free(struct comt_task_s, p);
}

void
ppb_core_call_on_main_thread(int32_t delay_in_milliseconds, struct PP_CompletionCallback callback,
                             int32_t result)
{
    PP_Resource main_message_loop = ppb_message_loop_get_for_main_thread();
    ppb_message_loop_post_work_with_result(main_message_loop, callback, delay_in_milliseconds,
                                           result);
}

void
ppb_core_call_on_main_thread_now(struct pp_instance_s *pp_i, struct PP_CompletionCallback callback,
                                 int32_t result)
{
    struct comt_task_s *task = g_slice_alloc(sizeof(*task));

    task->callback = callback;
    task->result_to_pass = result;
    task->terminate = 0;

    npn.pluginthreadasynccall(pp_i->npp, comt_proxy, task);
}

PP_Bool
ppb_core_is_main_thread(void)
{
    return ppb_message_loop_get_current() == ppb_message_loop_get_for_main_thread();
}


// trace wrappers
TRACE_WRAPPER
void
trace_ppb_core_add_ref_resource(PP_Resource resource)
{
    trace_info("[PPB] {full} %s resource=%d\n", __func__+6, resource);
    ppb_core_add_ref_resource(resource);
}

TRACE_WRAPPER
void
trace_ppb_core_release_resource(PP_Resource resource)
{
    trace_info("[PPB] {full} %s resource=%d\n", __func__+6, resource);
    ppb_core_release_resource(resource);
}

TRACE_WRAPPER
PP_Time
trace_ppb_core_get_time(void)
{
    trace_info("[PPB] {full} %s\n", __func__+6);
    return ppb_core_get_time();
}

TRACE_WRAPPER
PP_TimeTicks
trace_ppb_core_get_time_ticks(void)
{
    trace_info("[PPB] {full} %s\n", __func__+6);
    return ppb_core_get_time_ticks();
}

TRACE_WRAPPER
void
trace_ppb_core_call_on_main_thread(int32_t delay_in_milliseconds,
                                   struct PP_CompletionCallback callback, int32_t result)
{
    trace_info("[PPB] {full} %s delay_in_milliseconds=%d, callback={.func=%p, .user_data=%p, "
               ".flags=%d}, result=%d\n", __func__+6, delay_in_milliseconds, callback.func,
               callback.user_data, callback.flags, result);
    ppb_core_call_on_main_thread(delay_in_milliseconds, callback, result);
}

TRACE_WRAPPER
PP_Bool
trace_ppb_core_is_main_thread(void)
{
    trace_info("[PPB] {full} %s\n", __func__+6);
    return ppb_core_is_main_thread();
}


const struct PPB_Core_1_0 ppb_core_interface_1_0 = {
    .AddRefResource =   TWRAPF(ppb_core_add_ref_resource),
    .ReleaseResource =  TWRAPF(ppb_core_release_resource),
    .GetTime =          TWRAPF(ppb_core_get_time),
    .GetTimeTicks =     TWRAPF(ppb_core_get_time_ticks),
    .CallOnMainThread = TWRAPF(ppb_core_call_on_main_thread),
    .IsMainThread =     TWRAPF(ppb_core_is_main_thread),
};
