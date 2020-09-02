/****************************************************************************
*
*    Copyright (c) 2020 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "vsi_nn_context.h"
#include "vsi_nn_prv.h"
#include "vsi_nn_types.h"
#include "vsi_nn_graph.h"
#include "vsi_nn_log.h"
#include "vsi_nn_error.h"
#include "kernel/vsi_nn_kernel.h"

__BEGIN_DECLS

#define KERNEL_SELECTOR( kernel_name ) \
    static vsi_status _##kernel_name##_kernel_selector( \
            vsi_nn_graph_t*, \
            vsi_nn_tensor_t **, size_t, \
            vsi_nn_tensor_t **, size_t, \
            const vsi_nn_kernel_param_t *, \
            vsi_nn_kernel_selector_t * \
            ); \
    REGISTER_KERNEL_SELECTOR( kernel_name, _##kernel_name##_kernel_selector ) \
    static vsi_status _##kernel_name##_kernel_selector

KERNEL_SELECTOR( depthwise_conv1d )
    (
    vsi_nn_graph_t              * graph,
    vsi_nn_tensor_t            ** inputs,
    size_t                        input_num,
    vsi_nn_tensor_t            ** outputs,
    size_t                        output_num,
    const vsi_nn_kernel_param_t * params,
    vsi_nn_kernel_selector_t    * selector
    )
{
    vsi_nn_kernel_pirority_t pirority[] = {
        { VSI_NN_KERNEL_TYPE_VX,    0 },
        { VSI_NN_KERNEL_TYPE_EVIS,  3 },
        { VSI_NN_KERNEL_TYPE_CL,    2 },
        { VSI_NN_KERNEL_TYPE_CPU,   1 },
        };
    return vsi_nn_kernel_pirority_set( selector, pirority, _cnt_of_array(pirority) );
} /* depthwise_conv1d */

__END_DECLS
