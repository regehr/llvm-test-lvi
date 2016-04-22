#define DEBUG_TYPE "test-lvi"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

STATISTIC(TotalBits, "Total bits");
STATISTIC(KnownBits, "Known bits");
STATISTIC(IntervalBits, "Interval bits");

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

  std::string knownString(APInt &Zero, APInt &One) {
    std::string S = "";
    assert(Zero.getBitWidth() == One.getBitWidth());
    for (unsigned i = 0; i < Zero.getBitWidth(); ++i) {
      assert(!Zero.isNegative() || !One.isNegative());
      if (Zero.isNegative())
        S += "0";
      else if (One.isNegative())
        S += "1";
      else
        S += "x";
      Zero <<= 1;
      One <<= 1;
    }
    return S;
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
    const DataLayout &DL = F.getParent()->getDataLayout();
    LazyValueInfo *LVI = &getAnalysis<LazyValueInfo>();
    /*
     * record the dataflow facts before we start messing with the function
     */
    std::vector<Instruction *> Insts;
    std::vector<ConstantRange> CRs;
    std::vector<APInt> Zeros;
    std::vector<APInt> Ones;
    std::vector<unsigned> Widths;
    for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      Instruction *Inst = &*I;
      if (!Inst->getType()->isIntegerTy())
	continue;
      if (Inst->getOpcode() == Instruction::PHI)
        continue;
      ConstantRange CR = LVI->getConstantRange(Inst, Inst->getParent());
      unsigned Width = Inst->getType()->getIntegerBitWidth();
      APInt Zero(Width, 0), One(Width, 0);
      computeKnownBits(Inst, Zero, One, DL);
      TotalBits += Width;
      IntervalBits += Width - CR.getSetSize().logBase2();
      KnownBits += Zero.countPopulation() + One.countPopulation();
      if (!CR.isFullSet() || !Zero.isMinValue() || !One.isMinValue()) {
        Insts.push_back(Inst);
        CRs.push_back(CR);
        Zeros.push_back(Zero);
        Ones.push_back(One);
        Widths.push_back(Width);
      }
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
      auto Zero = Zeros[i];
      auto One = Ones[i];
      auto Width = Widths[i];
      errs() << "instrumenting " << CR << " " <<
        knownString(Zero, One) << " at:" << *Inst << "\n";
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
         * eventually we'll trap if this gets set
         */
        Value *NeedTrap = builder.getInt(APInt(1, 0));
        if (!CR.isFullSet()) {
          /*
           * the only trick here is that for a regular interval the value must be
           * >= Lower && < Upper whereas for a wrapped interval the value only has
           * to be >= Lower || < Upper
           *
           * we can do this using either signed or unsigned as long as we're
           * consistent about it
           */
          auto Res1 = builder.CreateICmpULT(Inst, builder.getInt(CR.getLower()));
          auto Res2 = builder.CreateICmpUGE(Inst, builder.getInt(CR.getUpper()));
          Value *Res3;
          if (CR.getLower().ult(CR.getUpper()))
            Res3 = builder.CreateOr(Res1, Res2);
          else
            Res3 = builder.CreateAnd(Res1, Res2);
          NeedTrap = builder.CreateOr(NeedTrap, Res3);
        }
        if (!Zero.isMinValue()) {
          auto Mask = builder.getInt(~Zero);
          auto Res = builder.CreateAnd(Inst, Mask);
          auto B = builder.CreateICmpEQ(Res, Mask);
          NeedTrap = builder.CreateOr(NeedTrap, B);
        }
        if (!One.isMinValue()) {
          auto Mask = builder.getInt(One);
          auto Res = builder.CreateAnd(Inst, Mask);
          auto B = builder.CreateICmpEQ(Res, Mask);
          NeedTrap = builder.CreateOr(NeedTrap, B);
        }
        builder.CreateCondBr(NeedTrap, TrapBB, Split);
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
