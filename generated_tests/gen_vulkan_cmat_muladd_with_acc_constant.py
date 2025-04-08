# Copyright (c) 2025 Intel Corporation
# SPDX-License-Identifier: MIT

import mako
import os
import random
import sys

import numpy as np

if np.lib.NumpyVersion(np.__version__) >= '2.0.0b1':
    np.set_printoptions(legacy = "1.25")

from modules import utils
from templates import MAKO_TEMP_DIR

# TODO: Parametize types.  Have to tweak [require] depending on the types.

shader_test_template = """[require]
vulkan 1.2
vulkanMemoryModel
cooperative_matrix m=${m} n=${n} k=${k} a=float16_t b=float16_t c=float
storageBuffer16BitAccess
shaderFloat16

[compute shader]
#version 450

#extension GL_KHR_memory_scope_semantics: require
#extension GL_KHR_cooperative_matrix: require
#extension GL_EXT_shader_explicit_arithmetic_types: require
#extension GL_EXT_buffer_reference: require
#extension GL_EXT_shader_16bit_storage: require
#define M ${m}
#define N ${n}
#define K ${k}

layout(local_size_x = 32) in;

layout(binding = 0) buffer A {
    float16_t data_a[M * K];
};

layout(binding = 1) buffer B {
    float16_t data_b[K * N];
};

layout(binding = 2) buffer R {
    float data_r[M * N];
};

void main() {
    coopmat<float16_t, gl_ScopeSubgroup, M, K, gl_MatrixUseA> mat_a;
    coopmat<float16_t, gl_ScopeSubgroup, K, N, gl_MatrixUseB> mat_b;
    coopmat<float, gl_ScopeSubgroup, M, N, gl_MatrixUseAccumulator> mat_c;
    coopmat<float, gl_ScopeSubgroup, M, N, gl_MatrixUseAccumulator> mat_r;

    coopMatLoad(mat_a, data_a, 0, K, gl_CooperativeMatrixLayoutRowMajor);
    coopMatLoad(mat_b, data_b, 0, N, gl_CooperativeMatrixLayoutRowMajor);
    mat_c = coopmat<float, gl_ScopeSubgroup, M, N, gl_MatrixUseAccumulator>(${acc_constant});

    mat_r = coopMatMulAdd(mat_a, mat_b, mat_c);

    coopMatStore(mat_r, data_r, 0, N, gl_CooperativeMatrixLayoutRowMajor);
}

[test]
ssbo 0 subdata float16_t 0 ${matrix_a}

ssbo 1 subdata float16_t 0 ${matrix_b}

ssbo 2 ${matrix_r_size_in_bytes}

compute 1 1 1

probe ssbo float 2 0 == ${matrix_r}
"""

def matrix_to_string(m):
    s = [""]
    for row in m:
        s.append(" ".join([str(x) for x in row]))
    return " \\\n ".join(s)

def main():
    templ = mako.template.Template(
        text=shader_test_template,
        module_directory=MAKO_TEMP_DIR)

    tests = []

    # Ensure the matrices always be the same.
    prng = random.Random(1234)
    assert prng.randint(0, 1024) == 902, "Python PRNG algorithm changed unexpectedly"

    for c in [0.0, 0.5]:
        for m, n, k in [(8, 8, 16), (8, 16, 16)]:
            acc_type_size = 4

            mat_a = np.asarray(prng.choices(range(4), k=(m*k))).reshape(m, k)
            mat_b = np.asarray(prng.choices(range(4), k=(k*n))).reshape(k, n)
            mat_r = np.matmul(mat_a, mat_b) + c

            tests.append({
                "m": m,
                "n": n,
                "k": k,
                "acc_constant": c,

                "matrix_a": matrix_to_string(mat_a),
                "matrix_b": matrix_to_string(mat_b),
                "matrix_r": matrix_to_string(mat_r),
                "matrix_r_size_in_bytes": m * n * acc_type_size,
            })

    base_dir = os.path.join("vulkan", "shaders", "vulkan-cmat-muladd-with-acc-constant")
    utils.safe_makedirs(base_dir)

    for t in tests:
        name = "float16-float32-constant-{m}x{n}x{k}-acc-{acc_constant}.vk_shader_test".format(**t)
        filename = os.path.join(base_dir, name)

        with open(filename, "w") as f:
            try:
                f.write(templ.render_unicode(**t))
            except:
                print(mako.exceptions.text_error_template().render(), file=sys.stderr)
                raise
            print(filename)

if __name__ == '__main__':
    main()
