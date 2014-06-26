/*
 * Developed by Qingkai Shi
 * Copy Right by Prism Research Group, HKUST and State Key Lab for Novel Software Tech., Nanjing University.  
 * 
 * @TODO
 * 1. matchFunctionAndCall; e.g. if a function is considered as pthread_create, we may lock it...
 */

#include "Transformer.h"
#include <llvm/Support/Debug.h>
#include <list>

Transformer::Transformer(Module* m, set<Value*>* svs, unsigned psize) {
    module = m;
    sharedVariables = svs;
    ptrsize = psize;
}

CallInst* Transformer::insertCallInstBefore(Instruction* beforeInst, Function* tocall, ...) {
    std::vector<Value *> args;

    va_list ap;
    va_start(ap, tocall);
    Value* arg = va_arg(ap, Value*);

    while (arg != NULL) {
        args.push_back(arg);
        arg = va_arg(ap, Value*);
    }

    va_end(ap);

    CallInst *InstCall = CallInst::Create(tocall, args);
    InstCall->insertBefore(beforeInst);
    return InstCall;
}

CallInst* Transformer::insertCallInstAfter(Instruction* afterInst, Function* tocall, ...) {
    std::vector<Value *> args;

    va_list ap;
    va_start(ap, tocall);
    Value* arg = va_arg(ap, Value*);

    while (arg != NULL) {
        args.push_back(arg);
        arg = va_arg(ap, Value*);
    }

    va_end(ap);

    CallInst *InstCall = CallInst::Create(tocall, args);
    InstCall->insertAfter(afterInst);
    return InstCall;
}

CallInst* Transformer::insertCallInstAtHead(Function* theFunc, Function* tocall, ...) {
    std::vector<Value *> args;

    va_list ap;
    va_start(ap, tocall);
    Value* arg = va_arg(ap, Value*);

    while (arg != NULL) {
        args.push_back(arg);
        arg = va_arg(ap, Value*);
    }

    va_end(ap);

    ilist_iterator<BasicBlock> iterB = theFunc->getBasicBlockList().begin();
    while (iterB != theFunc->getBasicBlockList().end()) {
        BasicBlock& BB = *(theFunc->getBasicBlockList().begin());
        ilist_iterator<Instruction> iterI;
        for (iterI = BB.getInstList().begin(); iterI != BB.getInstList().end();) {
            Instruction &inst = *iterI;
            if (inst.getOpcodeName()[0] == '<') break;
            if (!(isa<AllocaInst>(inst) || isa<PHINode>(inst))) {

                CallInst *InstCall = CallInst::Create(tocall, args);
                InstCall->insertBefore(&inst);

                return InstCall;
            }
            iterI++;
        }
        iterB++;
    }
    errs() << "Instruction insert failed in insertCallInstAtHead\n";
    errs() << theFunc->getName() << "\n";
    return NULL;
}

CallInst* Transformer::insertCallInstAtTail(Function* theFunc, Function* tocall, ...) {
    std::vector<Value *> args;

    va_list ap;
    va_start(ap, tocall);
    Value* arg = va_arg(ap, Value*);

    while (arg != NULL) {
        args.push_back(arg);
        arg = va_arg(ap, Value*);
    }

    va_end(ap);

    ilist_iterator<BasicBlock> iterB = theFunc->getBasicBlockList().begin();
    for (; iterB != theFunc->getBasicBlockList().end(); iterB++) {
        BasicBlock &b = *iterB;
        ilist_iterator<Instruction> iterI;
        for (iterI = b.getInstList().begin(); iterI != b.getInstList().end();) {
            Instruction &inst = *iterI;
            if (isa<ReturnInst>(inst)) {
                CallInst *InstCall = CallInst::Create(tocall, args);
                InstCall->insertBefore(&inst);

                return InstCall;
            }
            iterI++;
        }
    }

    return NULL;
}

void Transformer::transform(AliasAnalysis& AA) {
    this->beforeTransform(AA);

    unsigned functionsNum = module->getFunctionList().size();
    unsigned handledNum = 0;

    for (ilist_iterator<Function> iterF = module->getFunctionList().begin(); iterF != module->getFunctionList().end(); iterF++) {
        Function& f = *iterF;
        if (!this->functionToTransform(&f)) {
            outs() << "Remaining... " << (functionsNum - handledNum++) << " functions             \r";
            continue;
        }

        outs() << "Remaining... " << (functionsNum - handledNum++) << " functions             \r";

        bool allocHasHandled = false;
        vector<AllocaInst*> allocas;
        for (ilist_iterator<BasicBlock> iterB = f.getBasicBlockList().begin(); iterB != f.getBasicBlockList().end(); iterB++) {
            BasicBlock &b = *iterB;
            if (!this->blockToTransform(&b)) {
                continue;
            }
            for (ilist_iterator<Instruction> iterI = b.getInstList().begin(); iterI != b.getInstList().end(); iterI++) {
                Instruction &inst = *iterI;
                
                if (!this->instructionToTransform(&inst)) {
                    continue;
                }
                
                DEBUG_WITH_TYPE("transform", errs() << "[Transforming] " << inst << "\n");

                if (isa<AllocaInst>(inst)) {
                    // record a vector
                    allocas.push_back((AllocaInst*) & inst);
                }

                if (!isa<AllocaInst>(inst)
                        && !(isa<CallInst>(inst) && ((CallInst*) & inst)->getCalledFunction() != NULL && ((CallInst*) & inst)->getCalledFunction()->isIntrinsic())
                        && !allocHasHandled) {
                    allocHasHandled = true;

                    for (unsigned i = 0; i < allocas.size(); i++) {
                        AllocaInst * a = allocas[i];
                        this->transformAllocaInst(a, & inst, AA);
                    }
                }

                if (isa<LoadInst>(inst)) {
                    this->transformLoadInst((LoadInst*) & inst, AA);
                }

                if (isa<StoreInst>(inst)) {
                    this->transformStoreInst((StoreInst*) & inst, AA);
                }

                if (isa<AtomicRMWInst>(inst)) {
                    this->transformAtomicRMWInst((AtomicRMWInst*) & inst, AA);
                }

                if (isa<AtomicCmpXchgInst>(inst)) {
                    this->transformAtomicCmpXchgInst((AtomicCmpXchgInst*) & inst, AA);
                }

                if (isa<VAArgInst>(inst)) {
                    this->transformVAArgInst((VAArgInst*) & inst, AA);
                }

                if (isa<CallInst>(inst)) {
                    CallInst &call = *(CallInst*) & inst;
                    Value* calledValue = call.getCalledValue();

                    if (calledValue == NULL) continue;

                    if (isa<Function>(calledValue)) {
                        handleCalls((CallInst*) & inst, (Function*) calledValue, AA);
                    } else if (calledValue->getType()->isPointerTy()) {
                        set<Function*> may;
                        ((DyckAliasAnalysis*) & AA)->get_aliased_functions(&may, NULL, (CallInst*) & inst);

                        set<Function*>::iterator it = may.begin();
                        while (it != may.end()) {
                            Function* cf = *it;
                            handleCalls((CallInst*) & inst, cf, AA);
                            it++;
                        }
                    }
                }

                /*all invokes have been lowered to calls
                 * 
                 * if (isa<InvokeInst>(inst)) {
                    InvokeInst &call = *(InvokeInst*) & inst;
                    Value* calledValue = call.getCalledValue();

                    if (calledValue == NULL) continue;

                    if (isa<Function>(calledValue)) {
                        handleInvokes((InvokeInst*) & inst, (Function*) calledValue, AA);
                    } else if (calledValue->getType()->isPointerTy()) {
                        set<Function*> may;
                        ((ExtraAliasAnalysisInterface*) & AA)->get_aliased_functions(&may, &inst);

                        set<Function*>::iterator it = may.begin();
                        while (it != may.end()) {
                            Function* cf = *it;
                            handleInvokes((InvokeInst*) & inst, cf, AA);
                            it++;
                        }
                    }
                }*/
            }
        }
    }

    outs() << "                                                            \r";

    this->afterTransform(AA);
}

bool Transformer::handleCalls(CallInst* call, Function* calledFunction, AliasAnalysis & AA) {
    Function &cf = *calledFunction;
    // fork & join
    if (cf.getName().str() == "pthread_create" && cf.getArgumentList().size() == 4) {
        transformPthreadCreate(call, AA);
        return true;
    } else if (cf.getName().str() == "pthread_join") {
        transformPthreadJoin(call, AA);
        return true;
    } else if (cf.getName().str() == "pthread_mutex_lock") {
        // lock & unlock
        transformPthreadMutexLock(call, AA);
        return true;
    } else if (cf.getName().str() == "pthread_mutex_unlock") {
        transformPthreadMutexUnlock(call, AA);
        return true;
    } else if (cf.getName().str() == "pthread_cond_wait") {
        // wait & notify
        transformPthreadCondWait(call, AA);
        return true;
    } else if (cf.getName().str() == "pthread_cond_timedwait") {
        // wait & notify
        transformPthreadCondTimeWait(call, AA);
        return true;
    } else if (cf.getName().str() == "pthread_cond_signal") {
        transformPthreadCondSignal(call, AA);
        return true;
    } else if (cf.getName().str() == "pthread_mutex_init") {
        transformPthreadMutexInit(call, AA);
        return true;
    } else if (cf.getName().str() == "exit") {
        // system exit
        transformSystemExit(call, AA);
        return true;
    } else if (cf.getName().str() == "malloc" || cf.getName().str() == "calloc"
            || cf.getName().str() == "realloc"
            || cf.getName().str() == "_Znaj"
            || cf.getName().str() == "_ZnajRKSt9nothrow_t"
            || cf.getName().str() == "_Znam"
            || cf.getName().str() == "_ZnamRKSt9nothrow_t"
            || cf.getName().str() == "_Znwj"
            || cf.getName().str() == "_ZnwjRKSt9nothrow_t"
            || cf.getName().str() == "_Znwm"
            || cf.getName().str() == "_ZnwmRKSt9nothrow_t") {
        // new / malloc
        transformAddressInit(call, AA);
        return true;
    } else if (cf.isIntrinsic()) {
        switch (cf.getIntrinsicID()) {
            case Intrinsic::memmove:
            case Intrinsic::memcpy:
            {
                transformMemCpyMov(call, AA);
            }
                break;
            case Intrinsic::memset:
            {
                transformMemSet(call, AA);
            }
                break;
            default:
                break;
        }
        return true;
    } else if (!this->functionToTransform(&cf) && !this->isInstrumentationFunction(&cf)) {
        // other empty functions
        transformSpecialFunctionCall(call, AA);
        return true;
    }
    //*/
    return false;
}

bool Transformer::handleInvokes(InvokeInst* call, Function* calledFunction, AliasAnalysis & AA) {
    Function &cf = *calledFunction;
    if (!this->functionToTransform(&cf)) {
        transformSpecialFunctionInvoke(call, AA);
        return true;
    }
    return false;
}
