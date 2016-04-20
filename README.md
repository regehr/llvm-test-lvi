# llvm-test-lvi

An LLVM pass for testing the soundness of the LazyValueInfo and
ValueTracking analyses. It rewrites the code so that the compile-time
dataflow facts are dynamically checked as the program executes.

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

# Using

```
clang -O -Xclang -load -Xclang LLVMTestLVI.so -c foo.c

opt -load LLVMTestLVI.so -test-lvi ../test/dead.ll | llvm-dis
```

On a Mac use .dylib instead of .so
