/*
 * Copyright (c) 2024-2025 Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file
 * @brief CAPI direct API definitions
 *
 * This header provides optional direct function aliases that bypass the
 * function pointer indirection in the CAPI ops structure for better performance.
 *
 * The aliases are only created when:
 * - CAPI_DIRECT_API is defined
 * - The compiler supports __attribute__((alias)) and __typeof__
 *
 * On unsupported compilers, the macros expand to nothing and users must
 * use the standard CAPI interface through function pointers.
 */

#ifndef CAPI_DIRECT_H_
#define CAPI_DIRECT_H_

#if defined(__cplusplus)
extern "C" {
#endif /* _cplusplus */

#ifdef CAPI_DIRECT_API

/* Check for compiler support of alias attribute and typeof operator */
#if defined(__GNUC__) || defined(__clang__)
/* GCC and Clang support both __attribute__((alias)) and __typeof__ */
#define CAPI_DIRECT_ALIAS_SUPPORTED 1
#elif defined(__has_attribute) && __has_attribute(alias) && defined(__has_extension) &&            \
	__has_extension(typeof)
/* Compiler supports checking for attributes and extensions */
#define CAPI_DIRECT_ALIAS_SUPPORTED 1
#else
/* Compiler doesn't support alias attribute, disable direct aliases */
#define CAPI_DIRECT_ALIAS_SUPPORTED 0
#endif

#if CAPI_DIRECT_ALIAS_SUPPORTED

#define CAPI_DIRECT_API_CAT2(a, b) a##_##b
#define CAPI_DIRECT_API_STR1(x)    #x
#define CAPI_DIRECT_API_STR2(a, b) CAPI_DIRECT_API_STR1(CAPI_DIRECT_API_CAT2(a, b))

#define ADD_OPTIONAL_CAPI_DIRECT_ALIAS(capi_initial, drv_initial, func_name)                       \
	extern __typeof__(capi_initial##_##func_name) capi_initial##_##func_name                   \
		__attribute__((alias(#drv_initial "_" #func_name)));

#else

/* Compiler doesn't support alias attribute - disable direct aliases */
#define ADD_OPTIONAL_CAPI_DIRECT_ALIAS(...) /**/

#endif /* CAPI_DIRECT_ALIAS_SUPPORTED */

#else

#define ADD_OPTIONAL_CAPI_DIRECT_ALIAS(...) /**/

#endif /* CAPI_DIRECT_API */

#if defined(__cplusplus)
}
#endif

#endif /* CAPI_DIRECT_H_ */
