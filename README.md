# llvm-test-lvi

An LLVM pass for testing the soundness of the LazyValueInfo analysis
pass. It rewrites the code so that every integer-typed value for which
a non-trivial ConstantRange can be inferred is dynamically checked
that it is in the bounds specified by the interval.

NOTE: Unless/until this patch lands, you'll need to build this pass
against a patched LLVM:

  http://reviews.llvm.org/D19179

# Building

```
mkdir build
cd build
cmake .. -DLLVM_ROOT=/path/to/llvm-install -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Release
make
```

Last tested against LLVM r266472

# Using

```
clang -O -Xclang -load -Xclang LLVMTestLVI.so -c foo.c

opt -load LLVMTestLVI.so -test-lvi ../test/dead.ll | llvm-dis
```

On a Mac use .dylib instead of .so
