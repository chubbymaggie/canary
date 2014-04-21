/* 
 * File:   Transformer4CanaryRecord.h
 * Author: jack
 *
 * Created on March 25, 2014, 17:00 PM
 */

#ifndef TRANSFORMER4CANARYRECORD_H
#define	TRANSFORMER4CANARYRECORD_H

#include "Transformer.h"

class Transformer4CanaryRecord : public Transformer{
private:
    map<Value *, int> sv_idx_map;

    Function *F_preload, *F_load, *F_prestore, *F_store;
    Function *F_prelock, *F_lock, *F_preunlock, *F_unlock;
    Function *F_prefork, *F_fork, *F_prejoin, *F_join;
    Function *F_prewait, *F_wait, *F_prenotify, *F_notify;
    Function *F_init, *F_exit/*, *F_thread_init, *F_thread_exit*/;
private:
    static int stmt_idx;

public:

    Transformer4CanaryRecord(Module * m, set<Value*> * svs, unsigned psize);

    void transform(AliasAnalysis& AA);
    
public:
    public:
    virtual void beforeTransform(AliasAnalysis& AA) ;
    virtual void afterTransform(AliasAnalysis& AA) ;
    virtual bool functionToTransform(Function * f) ;
    virtual bool blockToTransform(BasicBlock * bb) ;
    virtual bool instructionToTransform(Instruction * ins) ;
    virtual void transformLoadInst(LoadInst* ins, AliasAnalysis& AA) ;
    virtual void transformStoreInst(StoreInst* ins, AliasAnalysis& AA) ;
    virtual void transformPthreadCreate(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformPthreadJoin(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformPthreadMutexLock(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformPthreadMutexUnlock(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformPthreadCondWait(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformPthreadCondTimeWait(CallInst* ins, AliasAnalysis& AA);
    virtual void transformPthreadCondSignal(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformSystemExit(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformMemCpyMov(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformMemSet(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformSpecialFunctionCall(CallInst* ins, AliasAnalysis& AA) ;
    virtual void transformSpecialFunctionInvoke(InvokeInst* ins, AliasAnalysis& AA);
    virtual bool isInstrumentationFunction(Function *f);
    
    virtual bool debug();

private:

    int getValueIndex(Value * v, AliasAnalysis& AA);
    
};



#endif	/* TRANSFORMER4CANARYRECORD_H */
