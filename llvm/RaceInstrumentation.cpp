#include <map>

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CFG.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"


namespace {
using namespace llvm;

struct RaceInstrumentation : public FunctionPass {
  static char ID;
  StructType *thread_data_ty = nullptr;

  RaceInstrumentation() : FunctionPass(ID) {}

  /*
  StructType *getThreadDataTy(LLVMContext& ctx) {
    if (!thread_data_ty) {
      thread_data_ty = StructType::create("struct.__vrd_thread_data",
                                          Type::getInt8PtrTy(ctx),
                                          Type::getInt64Ty(ctx));
    }
    return thread_data_ty;
  }
  */

  void getAnalysisUsage(AnalysisUsage &Info) const override {
      Info.setPreservesAll();
  }

  bool runOnFunction(Function& F) override {
      if (F.isDeclaration())
          return false;
     errs() << "Instrumenting: ";
     errs().write_escaped(F.getName()) << '\n';

     bool changed = false;
     for (auto& BB: F) {
        changed |= runOnBasicBlock(BB);
     }

     return changed;
  }

  bool runOnBasicBlock(BasicBlock& block);

  void instrumentThreadCreate(CallInst *call, Value *data);

  void instrumentTSanFuncEntry(CallInst *call);

  void instrumentThreadFunExit(Function *fun);
};

static inline Value *getThreadCreateData(Function *fun, CallInst *call) {
    if (fun->getName().equals("thrd_create")) {
        return call->getOperand(2);
    } else  if (fun->getName().equals("pthread_create")) {
        return call->getOperand(3);
    }

    return nullptr;
}


static inline Value *getMutexLock(Function *fun, CallInst *call) {
    if (fun->getName().equals("mtx_lock") || fun->getName().equals("pthread_mutex_lock")) {
        return call->getOperand(0)->stripPointerCasts();
    }

    return nullptr;
}

static inline Value *getMutexUnlock(Function *fun, CallInst *call) {
    if (fun->getName().equals("mtx_unlock") || fun->getName().equals("pthread_mutex_unlock")) {
        return call->getOperand(0)->stripPointerCasts();
    }

    return nullptr;
}

static inline bool isTSanFuncEntry(Function *fun) {
    return fun->getName().equals("__tsan_func_entry");
}

void RaceInstrumentation::instrumentThreadCreate(CallInst *call, Value *data) {
    Module *module = call->getModule();
    LLVMContext &ctx = module->getContext();

    // create our data structure and pass it as data to the thread
    const FunctionCallee &vrd_fun = module->getOrInsertFunction("__vrd_create_thrd",
                                                                Type::getInt8PtrTy(ctx),
                                                                Type::getInt8PtrTy(ctx));
    std::vector<Value *> args = {data};
    auto *tid_call = CallInst::Create(vrd_fun, args, "", call);
    tid_call->setDebugLoc(call->getDebugLoc());

    // XXX: couldn't be the 'data' value used in more places?
    call->replaceUsesOfWith(data, tid_call);

    // now insert a call that registers that a thread was created and pass there
    // our data and the thread identifier
    // create our data structure and pass it as data to the thread
    auto *tidType = cast<PointerType>(call->getOperand(0)->getType())->getContainedType(0);
    const FunctionCallee &created_fun = module->getOrInsertFunction("__vrd_thrd_created",
                                                                    Type::getVoidTy(ctx),
                                                                    Type::getInt8PtrTy(ctx),
                                                                    tidType);
    auto *load = new LoadInst(tidType, call->getOperand(0), "", false, Align(4));
    args = {tid_call, load};
    auto *created_call = CallInst::Create(created_fun, args, "");
    created_call->setDebugLoc(call->getDebugLoc());
    created_call->insertAfter(call);
    load->insertAfter(call);
}

static void insertMutexLockOrUnlock(CallInst *call, Value *mtx, const std::string& fun, bool isunlock=false) {
    Module *module = call->getModule();
    LLVMContext &ctx = module->getContext();
    const FunctionCallee &instr_fun = module->getOrInsertFunction(fun,
                                                                  Type::getVoidTy(ctx),
                                                                  Type::getInt8PtrTy(ctx));
    CastInst *cast = CastInst::CreatePointerCast(mtx, Type::getInt8PtrTy(ctx));
    std::vector<Value *> args = {cast};
    auto *new_call = CallInst::Create(instr_fun, args, "");
    new_call->setDebugLoc(call->getDebugLoc());

    if (isunlock) {
        new_call->insertBefore(call);
    } else {
        new_call->insertAfter(call);
    }
    cast->insertBefore(new_call);
}

static const Instruction *findInstWithDbg(const BasicBlock *block) {
    for (const auto& inst : *block) {
        if (inst.getDebugLoc())
            return &inst;
    }

    return nullptr;
}

static DebugLoc findFirstDbgLoc(const Instruction *I) {
    if (auto *i = findInstWithDbg(I->getParent())) {
        return i->getDebugLoc();
    }

    // just find _some_ debug info. I know this is just wrong, but TSAN not putting
    // debug info is also wrong and we need to workaround it somehow
    for (const auto& block : *I->getFunction()) {
        if (auto *i = findInstWithDbg(&block)) {
            return i->getDebugLoc();
        }
    }

    //assert(false && "Found no debugging loc");
    return DebugLoc();
}

void RaceInstrumentation::instrumentTSanFuncEntry(CallInst *call) {
    Module *module = call->getModule();
    Function *fun = call->getFunction();
    LLVMContext &ctx = module->getContext();

    const FunctionCallee &instr_fun = module->getOrInsertFunction("__vrd_thrd_entry",
                                                                  Type::getInt8PtrTy(ctx),
                                                                  Type::getInt8PtrTy(ctx));

    if (fun->getName().equals("main")) {
        std::vector<Value *> args = {Constant::getNullValue(Type::getInt8PtrTy(ctx))};
        auto *new_call = CallInst::Create(instr_fun, args, "", call);
        new_call->setDebugLoc(call->getDebugLoc());
        instrumentThreadFunExit(fun);
        return;
    }
    if (fun->arg_size() != 1) {
        errs() << "Ignoring func_entry in function " << fun->getName() << " due to wrong number of operands\n" ;
        return;
    }

    /* Replace all uses of arg with a dummy value,
     * then get the original arg and replace the dummy value
     * with the original arg */
    Value *arg = fun->getArg(0);
    auto *dummy_phi = PHINode::Create(Type::getInt8PtrTy(ctx), 0);
    arg->replaceAllUsesWith(dummy_phi);

    std::vector<Value *> args = {arg};
    auto *new_call = CallInst::Create(instr_fun, args, "", call);
    new_call->setDebugLoc(call->getDebugLoc());

    dummy_phi->replaceAllUsesWith(new_call);
    delete dummy_phi;

    instrumentThreadFunExit(fun);
}

static inline bool blockHasNoSuccessors(BasicBlock &block) {
    return succ_begin(&block) == succ_end(&block);
}

static bool instrumentNoreturn(BasicBlock &block, const FunctionCallee &instr_fun) {
    for (auto& I : block) {
        if (auto *call = dyn_cast<CallInst>(&I)) {
            if (auto *fun = call->getCalledFunction()) {
                if (fun->hasFnAttribute(Attribute::NoReturn)) {
                    auto *new_call = CallInst::Create(instr_fun, {}, "", &I);
                    new_call->setDebugLoc(call->getDebugLoc());
                    return true;
                }
            }
        }
    }
    return false;
}

void RaceInstrumentation::instrumentThreadFunExit(Function *fun) {
    Module *module = fun->getParent();
    LLVMContext &ctx = module->getContext();
    const FunctionCallee &instr_fun = module->getOrInsertFunction("__vrd_thrd_exit",
                                                                  Type::getVoidTy(ctx));

    for (auto &block : *fun) {
        if (blockHasNoSuccessors(block)) {
            if (instrumentNoreturn(block, instr_fun)) {
                continue;
            }

            /* Failed finding noreturn call, so insert the call before the terminator */
            auto *new_call = CallInst::Create(instr_fun, {}, "", block.getTerminator());
            new_call->setDebugLoc(block.getTerminator()->getDebugLoc());
        }
    }
}


bool RaceInstrumentation::runOnBasicBlock(BasicBlock& block) {
    for (auto& I : block) {
        if (CallInst *call = dyn_cast<CallInst>(&I)) {
            auto *calledop = call->getCalledOperand()->stripPointerCastsAndAliases();
            auto *calledfun = dyn_cast<Function>(calledop);
            if (calledfun == nullptr) {
                errs() << "A call via function pointer ignored: " << *call << "\n";
                continue;
            }

            if (calledfun->getName().startswith("__tsan_")) {
                /* __tsan_* functions may not have dbgloc, workaround that */
                if (!call->getDebugLoc())
                    call->setDebugLoc(findFirstDbgLoc(call));
            }

            if (auto *mtx = getMutexLock(calledfun, call)) {
                insertMutexLockOrUnlock(call, mtx, "__vrd_mutex_lock");
            } else if (auto *mtx = getMutexUnlock(calledfun, call)) {
                insertMutexLockOrUnlock(call, mtx, "__vrd_mutex_unlock", /* isunlock */true);
            } else if (auto *data = getThreadCreateData(calledfun, call)) {
                instrumentThreadCreate(call, data);
            } else if (isTSanFuncEntry(calledfun)) {
                instrumentTSanFuncEntry(call);
            }
        }
    }

    return false;
}

}  // namespace

char RaceInstrumentation::ID = 0;
static RegisterPass<RaceInstrumentation> VRD("vamos-race-instrumentation",
                                   "VAMOS race instrumentation pass",
                                   true /* Only looks at CFG */,
                                   true /* Analysis Pass */);

static RegisterStandardPasses VRDP(
    PassManagerBuilder::EP_FullLinkTimeOptimizationLast,
    [](const PassManagerBuilder &,
       legacy::PassManagerBase &PM) { PM.add(new RaceInstrumentation()); });
