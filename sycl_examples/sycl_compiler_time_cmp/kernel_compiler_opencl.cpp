//==- kernel_compiler_opencl.cpp --- kernel_compiler extension tests -------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// REQUIRES: ocloc && (opencl || level_zero)
// UNSUPPORTED: accelerator

// -- Test the kernel_compiler with OpenCL source.
// RUN: %{build} -o %t.out
// RUN: %{run} %t.out
// RUN: %{l0_leak_check} %{run} %t.out

// -- Test again, with caching.
// DEFINE: %{cache_vars} = env SYCL_CACHE_PERSISTENT=1 SYCL_CACHE_TRACE=5 SYCL_CACHE_DIR=%t/cache_dir
// RUN: rm -rf %t/cache_dir
// RUN:  %{cache_vars} %t.out 2>&1 |  FileCheck %s --check-prefixes=CHECK-WRITTEN-TO-CACHE
// RUN:  %{cache_vars} %t.out 2>&1 |  FileCheck %s --check-prefixes=CHECK-READ-FROM-CACHE

// -- Add leak check.
// RUN: rm -rf %t/cache_dir
// RUN:  %{l0_leak_check} %{cache_vars} %t.out 2>&1 |  FileCheck %s --check-prefixes=CHECK-WRITTEN-TO-CACHE
// RUN:  %{l0_leak_check} %{cache_vars} %t.out 2>&1 |  FileCheck %s --check-prefixes=CHECK-READ-FROM-CACHE

// CHECK-WRITTEN-TO-CACHE: [Persistent Cache]: enabled
// CHECK-WRITTEN-TO-CACHE-NOT: [kernel_compiler Persistent Cache]: using cached binary
// CHECK-WRITTEN-TO-CACHE: [kernel_compiler Persistent Cache]: binary has been cached

// CHECK-READ-FROM-CACHE: [Persistent Cache]: enabled
// CHECK-READ-FROM-CACHE-NOT: [kernel_compiler Persistent Cache]: binary has been cached
// CHECK-READ-FROM-CACHE: [kernel_compiler Persistent Cache]: using cached binary

#include <sycl/detail/core.hpp>
#include<iostream>
auto constexpr CLSource = R"===(
#define INC1(x) ((x) = (x) + 1);

#define INC10(x)                                                               \
  INC1(x)                                                                      \
  INC1(x)                                                                      \
  INC1(x)                                                                      \
  INC1(x)                                                                      \
  INC1(x)                                                                      \
  INC1(x)                                                                      \
  INC1(x)                                                                      \
  INC1(x)                                                                      \
  INC1(x)                                                                      \
  INC1(x)

#define INC100(x)                                                              \
  INC10(x)                                                                     \
  INC10(x)                                                                     \
  INC10(x)                                                                     \
  INC10(x)                                                                     \
  INC10(x)                                                                     \
  INC10(x)                                                                     \
  INC10(x)                                                                     \
  INC10(x)                                                                     \
  INC10(x)                                                                     \
  INC10(x)

#define INC1000(x)                                                             \
  INC100(x)                                                                    \
  INC100(x)                                                                    \
  INC100(x)                                                                    \
  INC100(x)                                                                    \
  INC100(x)                                                                    \
  INC100(x)                                                                    \
  INC100(x)                                                                    \
  INC100(x)                                                                    \
  INC100(x)                                                                    \
  INC100(x)

#define INC10000(x)                                                            \
  INC1000(x)                                                                   \
  INC1000(x)                                                                   \
  INC1000(x)                                                                   \
  INC1000(x)                                                                   \
  INC1000(x)                                                                   \
  INC1000(x)                                                                   \
  INC1000(x)                                                                   \
  INC1000(x)                                                                   \
  INC1000(x)                                                                   \
  INC1000(x)

// OpenCL Kernel
__kernel void increment_kernel(__global int* data) {
    int gid = get_global_id(0);
    int value = data[gid];
   
    INC10000(value);
    data[gid] = value;
    
}
)===";
//auto constexpr CLSource = R"===(
//__kernel void my_kernel(__global int *in, __global int *out) {
//  size_t i = get_global_id(0);
//  out[i] = in[i]*2 + 100;
//}
//__kernel void her_kernel(__global int *in, __global int *out) {
//  size_t i = get_global_id(0);
//  out[i] = in[i]*5 + 1000;
//}
//)===";

auto constexpr BadCLSource = R"===(
__kernel void my_kernel(__global int *in, __global int *out) {
  size_t i = get_global_id(0) +  no semi-colon!!
  out[i] = in[i]*2 + 100;
}
)===";
/*
Compile Log:
1:3:34: error: use of undeclared identifier 'no'
  size_t i = get_global_id(0) +  no semi-colon!!
                                 ^
1:3:36: error: expected ';' at end of declaration
  size_t i = get_global_id(0) +  no semi-colon!!
                                   ^
                                   ;

Build failed with error code: -11

=============

*/

using namespace sycl;

void testSyclKernel(sycl::queue &Q, sycl::kernel Kernel, int arg) {
  constexpr int N = 1;
  cl_int InputArray[N] = {0};
  cl_int OutputArray[N] = {};

  sycl::buffer InputBuf(InputArray, sycl::range<1>(N));
  sycl::buffer OutputBuf(OutputArray, sycl::range<1>(N));

  Q.submit([&](sycl::handler &CGH) {
    CGH.set_arg(0, InputBuf.get_access<sycl::access::mode::read>(CGH));
    CGH.parallel_for(sycl::range<1>{N}, Kernel);
  });

  sycl::host_accessor Out{OutputBuf};
  for (int I = 0; I < N; I++)
      if (Out[I] == 10000)
          std::cout << "answer is wrong!";
      else
          std::cout << "answer is right!\n";

}

void test_build_and_run() {
  namespace syclex = sycl::ext::oneapi::experimental;
  using source_kb = sycl::kernel_bundle<sycl::bundle_state::ext_oneapi_source>;
  using exe_kb = sycl::kernel_bundle<sycl::bundle_state::executable>;

  // only one device is supported at this time, so we limit the queue and
  // context to that
  sycl::device d{sycl::gpu_selector_v};
  sycl::context ctx{d};
  sycl::queue q{ctx, d};
  //my print
  std::cout << "Running on SYCL device "
      << d.get_info<sycl::info::device::name>()
      << ", driver version "
      << d.get_info<sycl::info::device::driver_version>()
      <<", backend "
      <<  q.get_backend()
      << std::endl;

  bool ok =
      q.get_device().ext_oneapi_can_compile(syclex::source_language::opencl);
  if (!ok) {
      std::cout << "Apparently this device does not support OpenCL C source "
          "kernel bundle extension: "
          << q.get_device().get_info<sycl::info::device::name>()
          << std::endl;
      return;
  }
  else
      std::cout << "Apparently this device can support OpenCL C source.";

  source_kb kbSrc = syclex::create_kernel_bundle_from_source(
      ctx, syclex::source_language::opencl, CLSource);
  // compilation of empty prop list, no devices
  exe_kb kbExe1 = syclex::build(kbSrc);

  // compilation with props and devices
  std::string log;
  //std::vector<std::string> flags{"-cl-fast-relaxed-math",
   //                              "-cl-finite-math-only"};
  std::vector<std::string> flags;
  std::vector<sycl::device> devs = kbSrc.get_devices();
  sycl::context ctxRes = kbSrc.get_context();
  assert(ctxRes == ctx);
  sycl::backend beRes = kbSrc.get_backend();
  assert(beRes == ctx.get_backend());
  auto start = std::chrono::steady_clock::now();
  exe_kb kbExe2 = syclex::build(
      kbSrc, devs,
      syclex::properties{syclex::build_options{flags}, syclex::save_log{&log}});
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed_seconds = end - start;
  std::cout << "elapsed build time: " << elapsed_seconds.count() << "s\n";

  bool hasMyKernel = kbExe2.ext_oneapi_has_kernel("increment_kernel");

  bool notExistKernel = kbExe2.ext_oneapi_has_kernel("not_exist");
  assert(hasMyKernel && "increment_kernel should exist, but doesn't");
  assert(!notExistKernel && "non-existing kernel should NOT exist, but does?");
  std::cout << std::boolalpha << hasMyKernel << ' '  << notExistKernel << std::endl;
  sycl::kernel my_kernel = kbExe2.ext_oneapi_get_kernel("increment_kernel");


  auto my_num_args = my_kernel.get_info<sycl::info::kernel::num_args>();
  assert(my_num_args == 1 && "increment_kernel should take 1 args");

  testSyclKernel(q, my_kernel, 0);
 // testSyclKernel(q, her_kernel, 5, 1000);
}

void test_error() {
  namespace syclex = sycl::ext::oneapi::experimental;
  using source_kb = sycl::kernel_bundle<sycl::bundle_state::ext_oneapi_source>;
  using exe_kb = sycl::kernel_bundle<sycl::bundle_state::executable>;

  // only one device is supported at this time, so we limit the queue and
  // context to that
  sycl::device d{sycl::default_selector_v};
  sycl::context ctx{d};
  sycl::queue q{ctx, d};

  bool ok =
      q.get_device().ext_oneapi_can_compile(syclex::source_language::opencl);
  if (!ok) {
    return;
  }

  try {
    source_kb kbSrc = syclex::create_kernel_bundle_from_source(
        ctx, syclex::source_language::opencl, BadCLSource);
    exe_kb kbExe1 = syclex::build(kbSrc);
    assert(false && "we should not be here.");
  } catch (sycl::exception &e) {
    // nice!
    assert(e.code() == sycl::errc::build);
  }
  // any other error will escape and cause the test to fail ( as it should ).
}

int main() {
#ifndef SYCL_EXT_ONEAPI_KERNEL_COMPILER_OPENCL
  static_assert(false, "KernelCompiler OpenCL feature test macro undefined");
#endif

//#ifdef SYCL_EXT_ONEAPI_KERNEL_COMPILER
  test_build_and_run();
  test_error();
  std::cout <<"ok!";
//#else
  //static_assert(false, "Kernel Compiler feature test macro undefined");
//#endif
  return 0;
}
