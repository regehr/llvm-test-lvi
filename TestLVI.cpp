#define DEBUG_TYPE "overflow-dedup"
#include "llvm/Support/Debug.h"

#include "llvm/ADT/Statistic.h"
STATISTIC(TestLVICount, "Number of redundant overflow checks eliminated");

#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/LazyValueInfo.h"

#include <map>
#include <utility>

using namespace llvm;

namespace {

class TestLVI : public llvm::FunctionPass {

private:

public:
  static char ID;

  TestLVI() : llvm::FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &Info) const override {
    Info.addRequired<LazyValueInfo>();
  }

  void printConstantRanges(Function &F, LazyValueInfo *LVI) {
    errs() << F.getName() << " :\n";
    for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      Instruction *Inst = &*I;
      // errs() << *Inst << "\n";
      if (!Inst->getType()->isIntegerTy())
	continue;
      ConstantRange CR = LVI->getConstantRange(Inst, Inst->getParent());
      errs() << "    " << CR << "\n";
    }
    errs() << "\n";
  }

  BasicBlock *createTrapBB(Function &F) {
    auto &C = F.getContext();
    BasicBlock *TrapBB = BasicBlock::Create(C, "trap", &F);
    IRBuilder<> trapBuilder(TrapBB);
    Value *TrapValue = Intrinsic::getDeclaration(F.getParent(), Intrinsic::trap);
    CallInst *TrapCall = trapBuilder.CreateCall(TrapValue);
    TrapCall->setDoesNotReturn();
    TrapCall->setDoesNotThrow();
    trapBuilder.CreateUnreachable();
    return TrapBB;
  }
  
  bool runOnFunction(Function &F) override {
    LazyValueInfo *LVI = &getAnalysis<LazyValueInfo>();
    printConstantRanges(F, LVI);
    auto &C = F.getContext();
    std::vector<Instruction *> Insts;
    std::vector<ConstantRange> CRs;
    for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      Instruction *Inst = &*I;
      if (!Inst->getType()->isIntegerTy())
	continue;
      ConstantRange CR = LVI->getConstantRange(Inst, Inst->getParent());
      if (CR.isFullSet())
	continue;
      Insts.push_back(Inst);
      CRs.push_back(CR);
    }
    BasicBlock *TrapBB = createTrapBB(F);
    for (auto Inst : Insts) {
      BasicBlock *OldBB = Inst->getParent();
      BasicBlock *Split = OldBB->splitBasicBlock(Inst);
    }
    return true;
  }
  
};
}

char TestLVI::ID = 0;
static RegisterPass<TestLVI> X("test-lvi",
			       "Test correctness of the LVI pass");

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

static void registerClangPass(const PassManagerBuilder &,
                              legacy::PassManagerBase &PM) {
  PM.add(new TestLVI());
}
static RegisterStandardPasses RegisterClangPass(PassManagerBuilder::EP_Peephole,
                                                registerClangPass);
