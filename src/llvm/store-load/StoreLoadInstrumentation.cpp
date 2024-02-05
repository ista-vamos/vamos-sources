#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/AliasAnalysis.h"

#include "llvm/IR/LegacyPassManager.h"

#include <set>

namespace {

using namespace llvm;

struct StoreLoadInstrumentation : public FunctionPass {
  static char ID;
  StoreLoadInstrumentation() : FunctionPass(ID) {}

  std::set<Value *> watching;

  void getWatching(Module &M, const StringRef& fun) {
      auto *watch = cast<Function>(M.getFunction(fun));
      if (!watch)
          return;

      for (auto *user : watch->users()) {
          auto *C = dyn_cast<CallInst>(user);
          if (!C) {
              errs() << "WARNING: unhandled use: " << *user << "\n";
              continue;
          }
          errs() << "WATCHING: " << *C->getOperand(0) << "\n";
          watching.insert(C->getOperand(0));
      }
  }

  bool doInitialization(Module &M) override {
      getWatching(M, "__vamos_private");
      getWatching(M, "__vamos_public");
      return false;
  }

  bool surelyNotWatched(Value *v, AliasAnalysis *AA) const {
      for (auto *ptr : watching) {
          if (AA->alias(ptr, v))
              return false;
      }

      return true;
  }

  void watchStore(StoreInst *S, Module &mod, LLVMContext &ctx) {
    auto bytes = mod.getDataLayout().getTypeAllocSize(S->getOperand(0)->getType());
    Function *fun;
    if (bytes == 4) {
          fun = cast<Function>(mod.getOrInsertFunction("__vamos_watch_store4",
                                Type::getVoidTy(ctx),
                                Type::getInt32Ty(ctx),
                                                       Type::getInt8PtrTy(ctx)).getCallee());
    } else if (bytes == 8) {
          fun = cast<Function>(mod.getOrInsertFunction("__vamos_watch_store8",
                      Type::getVoidTy(ctx),
                      Type::getInt64Ty(ctx),
                                                       Type::getInt8PtrTy(ctx)).getCallee());
    } else {
        errs() << "Unsupported store: " << *S << "\n";
    }

    std::vector<Value *> args = {S->getOperand(0), S->getOperand(1)};
    auto *call = CallInst::Create(fun, args, "", S);
    call->setDebugLoc(S->getDebugLoc());
  }

  bool runOnFunction(Function &F) override {
    errs() << "StoreLoadInstrumentation: ";
    errs().write_escaped(F.getName()) << '\n';

    AliasAnalysis *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
    auto &mod = *F.getParent();
    auto &ctx = mod.getContext();
    bool changed = false;

    for (auto &block : F) {
        for (auto I = block.begin(), E = block.end(); I != E; ) {
           //if (auto *Alloca = dyn_cast<AllocaInst>(&*I)) {
           //    errs() << "ALLOCA: " << *Alloca << "\n";
           //}
           //if (auto *Call = dyn_cast<CallInst>(&*I)) {
           //    errs() << "CALL: " << *Call << "\n";
           //}
            if (auto *Store = dyn_cast<StoreInst>(&*I)) {
                if (surelyNotWatched(Store->getOperand(1), AA)) {
                  ++I;
                  continue;
                }

                errs() << "WATCHING STORE: " << *Store << "\n";
                watchStore(Store, mod, ctx);
                changed = true;
            }
            ++I;
        }
    }

    return changed;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<AAResultsWrapperPass>();
  }

}; // StoreLoadInstrumentation

}  // anonymous

char StoreLoadInstrumentation::ID = 0;
static RegisterPass<StoreLoadInstrumentation> X("vamos-store-load-instr", "StoreLoadInstrumentation Pass",
                             false /* Only looks at CFG */,
                             false /* Analysis Pass */);
