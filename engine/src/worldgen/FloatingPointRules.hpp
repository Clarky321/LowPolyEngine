#pragma once

// Included by every worldgen source (ADR 0003): generation code is never
// compiled with fast-math or with floating-point contraction into fused
// multiply-add, which would change float results between compilers and CPUs.
// GCC and Clang get -ffp-contract=off from the worldgen_sources list in
// engine/CMakeLists.txt; MSVC's default /fp:precise does not contract.
#if defined(__FAST_MATH__) || defined(_M_FP_FAST) || defined(_M_FP_CONTRACT)
#error "worldgen must be compiled without fast-math and floating-point contraction (ADR 0003)"
#endif
