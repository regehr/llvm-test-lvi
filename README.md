# llvm-test-lvi

An LLVM pass for testing the soundness of the LazyValueInfo analysis
pass.



# Building

```
mkdir build
cd build
cmake .. -DLLVM_ROOT=/path/to/llvm-install -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DCMAKE_BUILD_TYPE=Release
make
```

Last tested against LLVM r265012

# Using

```
clang -O -Xclang -load -Xclang LLVMTestLVI.so -c ../test/test.c

clang -O -c -emit-llvm ../test/test.c
opt -load LLVMTestLVI.so -overflow-dedup test.bc | llvm-dis
```

On a Mac use .dylib instead of .so
