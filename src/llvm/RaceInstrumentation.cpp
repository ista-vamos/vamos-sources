#include <map>

#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

namespace {
using namespace llvm;

struct RaceInstrumentation : public FunctionPass {
    static char ID;
    StructType *thread_data_ty = nullptr;

    RaceInstrumentation() : FunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &Info) const override {
        Info.setPreservesAll();
    }

    bool runOnFunction(Function &F) override {
        if (F.isDeclaration())
            return false;
        if (F.getName().startswith("tsan.")) {
           //errs() << "Skipping: ";
           //errs().write_escaped(F.getName()) << '\n';
            return false;
        }

        bool changed = false;

        if (F.getName().equals("main")) {
            changed |= instrumentMainFunc(&F);
        }

       //errs() << "Instrumenting: ";
       //errs().write_escaped(F.getName()) << '\n';

        for (auto &BB : F) {
            changed |= runOnBasicBlock(BB);
        }

        return changed;
    }

    bool runOnBasicBlock(BasicBlock &block);

    void instrumentThreadCreate(CallInst *call, int data_idx);

    bool instrumentMainFunc(Function *fun);

    void instrumentThreadFunExit(Function *fun);
};

static inline int getThreadCreateDataIdx(Function *fun, CallInst *call) {
    if (fun->getName().equals("thrd_create")) {
        return 2;
    } else if (fun->getName().equals("pthread_create")) {
        return 3;
    }

    return -1;
}

static inline Value *getMutexLock(Function *fun, CallInst *call) {
    if (fun->getName().equals("mtx_lock") ||
        fun->getName().equals("pthread_mutex_lock")) {
        return call->getOperand(0)->stripPointerCasts();
    }

    return nullptr;
}

static inline Value *getMutexUnlock(Function *fun, CallInst *call) {
    if (fun->getName().equals("mtx_unlock") ||
        fun->getName().equals("pthread_mutex_unlock")) {
        return call->getOperand(0)->stripPointerCasts();
    }

    return nullptr;
}

static inline Value *getThreadJoinTid(Function *fun, CallInst *call) {
    if (fun->getName().equals("thrd_join") ||
        fun->getName().equals("pthread_join")) {
        return call->getOperand(0);
    }
    return nullptr;
}

static inline bool isThreadExit(Function *fun) {
    return fun->getName().equals("thrd_exit") || fun->getName().equals("pthread_exit");
}

void RaceInstrumentation::instrumentThreadCreate(CallInst *call, int data_idx) {
    assert(data_idx > 0);
    Module *module = call->getModule();
    LLVMContext &ctx = module->getContext();

    Value *data = call->getOperand(data_idx);
    Value *thr_fun = call->getOperand(data_idx - 1);
    // insert a function that creates the buffer (must be done from this thread
    // if we want to avoid locking) and creates our data structure that we pass
    // to pthread_create as data
    const FunctionCallee &vrd_fun = module->getOrInsertFunction(
        "__vrd_create_thrd", Type::getInt8PtrTy(ctx), Type::getInt8PtrTy(ctx), Type::getInt8PtrTy(ctx));
    auto *cst = CastInst::CreatePointerCast(thr_fun, Type::getInt8PtrTy(ctx), "", call);
    std::vector<Value *> args = {cst, data};
    auto *tid_call = CallInst::Create(vrd_fun, args, "", call);
    tid_call->setDebugLoc(call->getDebugLoc());

    // now replace the thread function with our wrapper
    const char *instr_fun_name = data_idx == 2 ? "__vrd_run_thread_c11" : "__vrd_run_thread";
    if (data_idx == 2) {
        module->getOrInsertFunction(
            instr_fun_name,
            Type::getInt32Ty(ctx),
            Type::getInt8PtrTy(ctx));
    } else {
        module->getOrInsertFunction(
            instr_fun_name,
            Type::getInt8PtrTy(ctx),
            Type::getInt8PtrTy(ctx));
    }

    Value *instr_fun = module->getFunction(instr_fun_name);
    call->setOperand(data_idx - 1, instr_fun);
    call->setOperand(data_idx, tid_call);

    // now insert a call that registers that a thread was created and pass there
    // our data and the thread identifier
    // create our data structure and pass it as data to the thread
#if LLVM_VERSION_MAJOR < 15
    auto *tidType =
        cast<PointerType>(call->getOperand(0)->getType())->getContainedType(0);
#else
    auto *tidType =
        cast<PointerType>(call->getOperand(0)->getType());
#endif
    const FunctionCallee &created_fun =
        module->getOrInsertFunction("__vrd_thrd_created", Type::getVoidTy(ctx),
                                    Type::getInt8PtrTy(ctx), tidType);
    auto *load =
        new LoadInst(tidType, call->getOperand(0), "", false, Align(4));
    args = {tid_call, load};
    auto *created_call = CallInst::Create(created_fun, args, "");
    created_call->setDebugLoc(call->getDebugLoc());
    created_call->insertAfter(call);
    load->insertAfter(call);
}

static void instrumentThreadJoin(CallInst *call, Value *tid) {
    Module *module = call->getModule();
    LLVMContext &ctx = module->getContext();

    const FunctionCallee &before_join_fun = module->getOrInsertFunction(
        "__vrd_thrd_join", Type::getInt8PtrTy(ctx), Type::getInt64Ty(ctx));
    std::vector<Value *> args = {tid};
    auto *new_call = CallInst::Create(before_join_fun, args, "");
    new_call->setDebugLoc(call->getDebugLoc());
    new_call->insertBefore(call);

    const FunctionCallee &after_join_fun = module->getOrInsertFunction(
        "__vrd_thrd_joined", Type::getVoidTy(ctx), Type::getInt8PtrTy(ctx));
    args = {new_call};
    new_call = CallInst::Create(after_join_fun, args, "");
    new_call->setDebugLoc(call->getDebugLoc());
    new_call->insertAfter(call);
}

static void instrumentThreadExit(CallInst *call) {
    Module *module = call->getModule();
    LLVMContext &ctx = module->getContext();

    const FunctionCallee &fun = module->getOrInsertFunction(
        "__vrd_thrd_exit", Type::getVoidTy(ctx));
    std::vector<Value *> args = {};
    auto *new_call = CallInst::Create(fun, args, "", call);
    new_call->setDebugLoc(call->getDebugLoc());
}


static void insertMutexLockOrUnlock(CallInst *call, Value *mtx,
                                    const std::string &fun,
                                    bool isunlock = false) {
    Module *module = call->getModule();
    LLVMContext &ctx = module->getContext();
    const FunctionCallee &instr_fun = module->getOrInsertFunction(
        fun, Type::getVoidTy(ctx), Type::getInt8PtrTy(ctx));
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
    for (const auto &inst : *block) {
        if (inst.getDebugLoc())
            return &inst;
    }

    return nullptr;
}

static inline bool isThreadCreateCall(CallInst *call) {
    auto *calledop = call->getCalledOperand()->stripPointerCastsAndAliases();
    if (auto *calledfun = dyn_cast<Function>(calledop)) {
        return calledfun->getName().equals("thrd_create") ||
               calledfun->getName().equals("pthread_create");
    }

    return false;
}

bool isSelectOfThreadFun(SelectInst *select) {
    for (auto &use : select->uses()) {
        auto *user = use.getUser();
        if (auto *call = dyn_cast<CallInst>(user)) {
            // XXX: we should also check that it is the right operand
            if (isThreadCreateCall(call)) {
                return true;
            }
        }
    }
    return false;
}

static DebugLoc findFirstDbgLoc(const Instruction *I) {
    if (auto *i = findInstWithDbg(I->getParent())) {
        return i->getDebugLoc();
    }

    // just find _some_ debug info. I know this is just wrong, but TSAN not
    // putting debug info is also wrong and we need to workaround it somehow
    for (const auto &block : *I->getFunction()) {
        if (auto *i = findInstWithDbg(&block)) {
            return i->getDebugLoc();
        }
    }

    // assert(false && "Found no debugging loc");
    return DebugLoc();
}

static inline bool blockHasNoSuccessors(BasicBlock &block) {
    return succ_begin(&block) == succ_end(&block);
}

static bool instrumentNoreturn(BasicBlock &block,
                               const FunctionCallee &instr_fun) {
    for (auto &I : block) {
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

bool RaceInstrumentation::instrumentMainFunc(Function *fun) {
    Module *module = fun->getParent();
    LLVMContext &ctx = module->getContext();

    auto *insert_pt = &fun->getEntryBlock().front();

    const FunctionCallee &setup_fun = module->getOrInsertFunction(
        "__vrd_setup_main_thread", Type::getInt8PtrTy(ctx), Type::getInt8PtrTy(ctx));
    const FunctionCallee &exit_fun =
        module->getOrInsertFunction("__vrd_exit_main_thread", Type::getVoidTy(ctx));

    std::vector<Value *> args = {
        Constant::getNullValue(Type::getInt8PtrTy(ctx))};
    auto *new_call = CallInst::Create(setup_fun, args, "", insert_pt);
    new_call->setDebugLoc(findFirstDbgLoc(insert_pt));

    for (auto &block : *fun) {
        if (blockHasNoSuccessors(block)) {
            if (instrumentNoreturn(block, exit_fun)) {
                continue;
            }

            /* Failed finding noreturn call, so insert the call before the
             * terminator */
            auto *new_call =
                CallInst::Create(exit_fun, {}, "", block.getTerminator());
            new_call->setDebugLoc(block.getTerminator()->getDebugLoc());
        }
    }

    return true;
}

bool RaceInstrumentation::runOnBasicBlock(BasicBlock &block) {
    std::vector<CallInst *> remove;
    int data_idx = -1;
    for (auto &I : block) {
        if (CallInst *call = dyn_cast<CallInst>(&I)) {
            auto *calledop =
                call->getCalledOperand()->stripPointerCastsAndAliases();
            auto *calledfun = dyn_cast<Function>(calledop);
            if (calledfun == nullptr) {
                errs() << "A call via function pointer ignored: " << *call
                       << "\n";
                continue;
            }

            if (calledfun->getName().startswith("__tsan_")) {
                // __tsan_* functions may not have dbgloc, workaround that.
                // We must set it also when we will remove the call,
                // becase our methods copy dbgloc from this call
                if (!call->getDebugLoc())
                    call->setDebugLoc(findFirstDbgLoc(call));

                remove.push_back(call);
            }

            if (auto *mtx = getMutexLock(calledfun, call)) {
                insertMutexLockOrUnlock(call, mtx, "__vrd_mutex_lock");
            } else if (auto *mtx = getMutexUnlock(calledfun, call)) {
                insertMutexLockOrUnlock(call, mtx, "__vrd_mutex_unlock",
                                        /* isunlock */ true);
            } else if ((data_idx = getThreadCreateDataIdx(calledfun, call)) >= 0) {
                instrumentThreadCreate(call, data_idx);
            } else if (auto *data = getThreadJoinTid(calledfun, call)) {
                instrumentThreadJoin(call, data);
            } else if (isThreadExit(calledfun)) {
                instrumentThreadExit(call);
            }
        }
    }

    /*
    for (CallInst *call : remove) {
        call->eraseFromParent();
    }
    */

    return remove.size() > 0;
}

}  // namespace

char RaceInstrumentation::ID = 0;
static RegisterPass<RaceInstrumentation> VRD("vamos-race-instrumentation",
                                             "VAMOS race instrumentation pass",
                                             true /* Only looks at CFG */,
                                             true /* Analysis Pass */);

static RegisterStandardPasses VRDP(
    PassManagerBuilder::EP_FullLinkTimeOptimizationLast,
    [](const PassManagerBuilder &, legacy::PassManagerBase &PM) {
        PM.add(new RaceInstrumentation());
    });
