#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace {

class TestLVI : public llvm::FunctionPass {

private:
  /*
   * make a new basic block that traps out
   */
  BasicBlock *createTrapBB(Function &F) {
    BasicBlock *TrapBB = BasicBlock::Create(F.getContext(), "trap", &F);
    IRBuilder<> trapBuilder(TrapBB);
    Value *TrapValue = Intrinsic::getDeclaration(F.getParent(), Intrinsic::trap);
    CallInst *TrapCall = trapBuilder.CreateCall(TrapValue);
    TrapCall->setDoesNotReturn();
    TrapCall->setDoesNotThrow();
    trapBuilder.CreateUnreachable();
    return TrapBB;
  }

  /*
   * well this is stupid but I don't know the better way to do it
   */
  Instruction *getNextInst(Instruction *I) {
    auto BB = I->getParent();
    bool gotit = false;
    for (BasicBlock::iterator iter = BB->begin(), e = BB->end(); iter != e; ++iter) {
      Instruction *IP = &*iter;
      if (gotit)
        return IP;
      if (IP == I)
        gotit = true;
    }
    llvm_unreachable("instuction not found");
  }

public:
  static char ID;

  TestLVI() : llvm::FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &Info) const override {
    Info.addRequired<LazyValueInfo>();
  }

  /*
   * TODO
   * - also test dataflow facts from LazyValueInfo
   * - consider emitting diagnostics instead of just trapping
   */
  bool runOnFunction(Function &F) override {
    LazyValueInfo *LVI = &getAnalysis<LazyValueInfo>();
    /*
     * record the dataflow facts before we start messing with the function
     */
    std::vector<Instruction *> Insts;
    std::vector<ConstantRange> CRs;
    for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      Instruction *Inst = &*I;
      if (!Inst->getType()->isIntegerTy())
	continue;
      if (Inst->getOpcode() == Instruction::PHI)
        continue;
      ConstantRange CR = LVI->getConstantRange(Inst, Inst->getParent());
      if (CR.isFullSet())
	continue;
      Insts.push_back(Inst);
      CRs.push_back(CR);
    }
    /*
     * bail early if we didn't find anything to check
     */
    if (Insts.size() == 0)
      return false;

    BasicBlock *TrapBB = createTrapBB(F);
    for (int i = 0; i < Insts.size(); ++i) {
      auto Inst = Insts[i];
      auto CR = CRs[i];
      errs() << "instrumenting " << CR << " at " << *Inst << "\n";
      BasicBlock *OldBB = Inst->getParent();

      /*
       * splitBasicBlock() splits before the argument instruction but we need to
       * split after
       */
      BasicBlock *Split = OldBB->splitBasicBlock(getNextInst(Inst));

      /*
       * nuke the unconditional branch that got inserted by splitBasicBlock()
       */
      auto branch = cast<BranchInst>(OldBB->getTerminator());
      branch->eraseFromParent();

      IRBuilder<> builder(OldBB);
      if (CR.isEmptySet()) {
        /*
         * an empty interval indicates dead code so unconditionally trap
         */
        builder.CreateBr(TrapBB);
      } else {
        /*
         * the only trick here is that for a regular interval the value must be
         * >= Lower && < Upper whereas for a wrapped interval the value only has
         * to be >= Lower || < Upper
         *
         * we can do this using either signed or unsigned as long as we're
         * consistent about it
         */
        auto Res1 = builder.CreateICmpUGE(Inst, builder.getInt(CR.getLower()));
        auto Res2 = builder.CreateICmpULT(Inst, builder.getInt(CR.getUpper()));
        Value *Res3;
        if (CR.getLower().ult(CR.getUpper()))
          Res3 = builder.CreateAnd(Res1, Res2);
        else
          Res3 = builder.CreateOr(Res1, Res2);
        builder.CreateCondBr(Res3, Split, TrapBB);
      }
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
static RegisterStandardPasses RegisterClangPass(PassManagerBuilder::EP_OptimizerLast,
                                                registerClangPass);
