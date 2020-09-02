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

#ifndef __ANEURALNETWORKS_LOCAL_RESPONSE_NORMALIZATION_HPP__
#define __ANEURALNETWORKS_LOCAL_RESPONSE_NORMALIZATION_HPP__

#define OP_SPEC_NAME LocalResponseNormOperation
OP_SPEC_BEGIN()
#define ARG_NAMES         \
    (input,               \
     radius,              \
     bias,                \
     alpha,               \
     beta,                \
     axis)
#define ARGC BOOST_PP_TUPLE_SIZE(ARG_NAMES)

#define BOOST_PP_LOCAL_MACRO(n) OP_SPEC_ARG(BOOST_PP_TUPLE_ELEM(ARGC, n, ARG_NAMES))
#define BOOST_PP_LOCAL_LIMITS (0, ARGC)
#include BOOST_PP_LOCAL_ITERATE()
OP_SPEC_END()

// order of argument is important
MAKE_SPEC(local_response_norm)
    .input_(nnrt::OperandType::TENSOR_FLOAT32)
    .radius_(nnrt::OperandType::INT32)
    .bias_(nnrt::OperandType::FLOAT32)
    .alpha_(nnrt::OperandType::FLOAT32)
    .beta_(nnrt::OperandType::FLOAT32)
    .axis_(nnrt::OperandType::INT32, OPTIONAL));

    OVERRIDE_SPEC(local_response_norm, 0)
    .input_(nnrt::OperandType::TENSOR_FLOAT16));

#undef ARG_NAMES
#undef ARGC
#undef OP_SPEC_NAME

#endif