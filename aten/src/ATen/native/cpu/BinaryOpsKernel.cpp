#include <cmath>
#include <iostream>
#include <ATen/Dispatch.h>
#include <ATen/Parallel.h>
#include <ATen/cpu/vec256/vec256.h>
#include <ATen/cpu/vec256/functional.h>
#include <ATen/native/TensorIterator.h>
#include <ATen/native/BinaryOps.h>
#include <ATen/native/cpu/Loops.h>

namespace at { namespace native {
namespace {

// Suppressing GCC warning about not using '&&' operator in bool multiplication
// instead of '*'.
#if defined(__GNUC__)
#    pragma GCC diagnostic ignored "-Wint-in-bool-context"
#endif

using namespace vec256;

void add_kernel(TensorIterator& iter, Scalar alpha_scalar) {
  if (iter.dtype() == ScalarType::Bool) {
    auto alpha = alpha_scalar.to<bool>();
    binary_kernel(iter, [=](bool a, bool b) -> bool { return a + alpha * b; });
  } else {
    AT_DISPATCH_ALL_TYPES(iter.dtype(), "add_cpu", [&]() {
    auto alpha = alpha_scalar.to<scalar_t>();
    auto alpha_vec = Vec256<scalar_t>(alpha);
    binary_kernel_vec(iter,
      [=](scalar_t a, scalar_t b) -> scalar_t { return a + alpha * b; },
      [=](Vec256<scalar_t> a, Vec256<scalar_t> b) {
        return vec256::fmadd(b, alpha_vec, a);
      });
    });
  }
}

void sub_kernel(TensorIterator& iter, Scalar alpha_scalar) {
  add_kernel(iter, -alpha_scalar);
}

void mul_kernel(TensorIterator& iter) {
  if (iter.dtype() == ScalarType::Bool) {
    binary_kernel(iter, [=](bool a, bool b) -> bool { return a * b; });
  } else {
    AT_DISPATCH_ALL_TYPES(iter.dtype(), "mul_cpu", [&]() {
      binary_kernel_vec(iter,
        [=](scalar_t a, scalar_t b) -> scalar_t { return a * b; },
        [=](Vec256<scalar_t> a, Vec256<scalar_t> b) {
          return a * b;
        });
    });
  }
}

void div_kernel(TensorIterator& iter) {
  if (isIntegralType(iter.dtype())) {
    // There's no SIMD integer division, so don't try to vectorize it.
    // TODO: if the divisor is a scalar, rewrite as multiplication by a constant.
    AT_DISPATCH_INTEGRAL_TYPES(iter.dtype(), "div_cpu", [&]() {
      binary_kernel(iter, [](scalar_t a, scalar_t b) -> scalar_t {
        return a / b;
      });
    });
  } else {
    AT_DISPATCH_FLOATING_TYPES(iter.dtype(), "div_cpu", [&]() {
      binary_kernel_vec(iter,
        [=](scalar_t a, scalar_t b) __ubsan_ignore_float_divide_by_zero__ -> scalar_t {
           return a / b;
        },
        [=](Vec256<scalar_t> a, Vec256<scalar_t> b) {
          return a / b;
        });
    });
  }
}

} // anonymous namespace


REGISTER_DISPATCH(add_stub, &add_kernel);
REGISTER_DISPATCH(sub_stub, &sub_kernel);
REGISTER_DISPATCH(mul_stub, &mul_kernel);
REGISTER_DISPATCH(div_stub, &div_kernel);

}} // namespace at::native
