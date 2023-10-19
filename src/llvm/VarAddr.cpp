#include <map>

#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"

namespace {
using namespace llvm;

static const Instruction *findInstWithDbg(const BasicBlock *block) {
    for (const auto &inst : *block) {
        if (inst.getDebugLoc())
            return &inst;
    }

    return nullptr;
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



struct VarAddr : public FunctionPass {
    static char ID;

    VarAddr() : FunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &Info) const override {
        Info.setPreservesAll();
    }

    bool runOnFunction(Function& F) override {
        Module *mod = F.getParent();
        bool changed = false;
        for (auto& B: F) {
            for (auto& I : B) {
                changed |= runOnInstruction(I, mod);
            }
        }

        if (F.getName().equals("main")) {
            changed |= instrumentGlobals(F);
        }
        return changed;
    }

    bool instrumentGlobals(Function& main_fun) {
        bool changed = false;
        Module *mod = main_fun.getParent();
        std::vector<GlobalVariable *> old_globals;
        // this is stupid, FIXME
        for (auto &G : mod->globals()) {
            old_globals.push_back(&G);
        }

        Instruction *first_inst = main_fun.getEntryBlock().getFirstNonPHIOrDbg();
        assert(first_inst);
        auto& ctx = mod->getContext();
        for (auto *G : old_globals) {
            const auto &var_name = G->getName();
            if (var_name.startswith("__vrd") || var_name.startswith("llvm."))
                continue;

            Constant *name = ConstantDataArray::getString(mod->getContext(), var_name, true);
            Constant *gname = new GlobalVariable(*mod, name->getType(), /* isConstant = */ true, GlobalValue::LinkageTypes::PrivateLinkage, name, "__vrd_var_name");

            const FunctionCallee &fun = mod->getOrInsertFunction(
                "__vrd_print_var", Type::getVoidTy(ctx), Type::getInt8PtrTy(ctx), Type::getInt8PtrTy(ctx));
            auto *cast = CastInst::CreatePointerCast(G, Type::getInt8PtrTy(ctx));
            std::vector<Value *> args = {
                cast, ConstantExpr::getPointerCast(gname, Type::getInt8PtrTy(ctx))};
            auto *call = CallInst::Create(fun, args, "", first_inst);
            cast->insertBefore(call);
            auto dbg = findFirstDbgLoc(first_inst);
            call->setDebugLoc(dbg);
            cast->setDebugLoc(dbg);
            changed = true;
        }

        return changed;
    }

    bool runOnInstruction(Instruction &I, Module *mod) {
        if (auto *DI = dyn_cast<DbgValueInst>(&I)) {
            if (!DI->getValue()->getType()->isPointerTy() || isa<UndefValue>(DI->getValue()))
                return false;

            auto *var = DI->getVariable();
            auto& ctx = mod->getContext();

            Constant *name = ConstantDataArray::getString(mod->getContext(), var->getName(), true);
            Constant *gname = new GlobalVariable(*mod, name->getType(), /* isConstant = */ true, GlobalValue::LinkageTypes::PrivateLinkage, name, "__vrd_var_name");

            const FunctionCallee &fun = mod->getOrInsertFunction(
                "__vrd_print_var", Type::getVoidTy(ctx), Type::getInt8PtrTy(ctx), Type::getInt8PtrTy(ctx));
            auto *cast = CastInst::CreatePointerCast(DI->getValue(), Type::getInt8PtrTy(ctx));
            std::vector<Value *> args = {
                cast, ConstantExpr::getPointerCast(gname, Type::getInt8PtrTy(ctx))};
            auto *call = CallInst::Create(fun, args, "", DI);
            cast->insertBefore(call);
            call->setDebugLoc(I.getDebugLoc());
            return true;
        }

        return false;
    }

};

char VarAddr::ID = 0;
static RegisterPass<VarAddr> VRD("vamos-print-vars-addr",
                                  "VAMOS debugging pass",
                                  true /* Only looks at CFG */,
                                  true /* Analysis Pass */);

#if LLVM_VERSION_MAJOR < 16
static RegisterStandardPasses VRDP(
    PassManagerBuilder::EP_FullLinkTimeOptimizationLast,
    [](const PassManagerBuilder &, legacy::PassManagerBase &PM) {
        PM.add(new VarAddr());
    });
#endif

}
