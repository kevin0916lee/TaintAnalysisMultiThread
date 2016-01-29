/*
 * PSOListener.h
 *
 *  Created on: May 16, 2014
 *      Author: ylc
 */

#ifndef PSOLISTENER_H_
#define PSOLISTENER_H_

#include "AddressSpace.h"
#include "Executor.h"
#include "Memory.h"
#include "BarrierInfo.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/ExecutionState.h"
#include "RuntimeDataManager.h"
#include "BitcodeListener.h"

#include <sstream>
#include <iostream>
#include <fstream>
#include <string>
#include <map>

#include "llvm/Support/CallSite.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/DerivedTypes.h"
#else
#include "llvm/Instructions.h"
#include "llvm/Type.h"
#include "llvm/DerivedTypes.h"
#endif

namespace llvm {
//class Type;
//class Constant;
}

namespace klee {

class PSOListener: public BitcodeListener {
public:
	PSOListener(Executor* executor, RuntimeDataManager* rdManager);
	~PSOListener();

	void beforeRunMethodAsMain(ExecutionState &initialState);
	void executeInstruction(ExecutionState &state, KInstruction *ki);
	void instructionExecuted(ExecutionState &state, KInstruction *ki);
	void afterRunMethodAsMain();
	void executionFailed(ExecutionState &state, KInstruction *ki);

//	void createMutex(ExecutionState &state, Mutex* mutex);
//	void createCondition(ExecutionState &state, Condition* condition);
	void createThread(ExecutionState &state, Thread* thread);

private:
	Executor* executor;
	RuntimeDataManager* rdManager;
	std::stringstream ss;
	std::map<uint64_t, unsigned> loadRecord;
	std::map<uint64_t, unsigned> storeRecord;
	std::map<uint64_t, llvm::Type*> usedGlobalVariableRecord;
	std::map<uint64_t, BarrierInfo*> barrierRecord;
	std::map<llvm::Instruction*, VectorInfo*> getElementPtrRecord; // 记录getElementPtr解析的数组信息

private:
	//std::vector<string> monitoredFunction;
	void handleInitializer(llvm::Constant* initializer, MemoryObject* mo,
			uint64_t& startAddress);
	void handleConstantExpr(llvm::ConstantExpr* expr);
	void insertGlobalVariable(ref<Expr> address, llvm::Type* type);
	ref<Expr> getExprFromMemory(ref<Expr> address, ObjectPair& op, llvm::Type* type);
	llvm::Constant* handleFunctionReturnValue(ExecutionState& state, KInstruction *ki);
	void handleExternalFunction(ExecutionState& state, KInstruction *ki);
	void analyzeInputValue(uint64_t& address, ObjectPair& op, llvm::Type* type);
	unsigned getLoadTime(uint64_t address);
	unsigned getStoreTime(uint64_t address);
	llvm::Function* getPointeredFunction(ExecutionState& state, KInstruction* ki);

	std::string createVarName(unsigned memoryId, ref<Expr> address,
			bool isGlobal) {
		char signal;
		ss.str("");
		if (isGlobal) {
			signal = 'G';
		} else {
			signal = 'L';
		}
		ss << signal;
		ss << memoryId;
		ss << '_';
		ss << address;
		return ss.str();
	}
	std::string createVarName(unsigned memoryId, uint64_t address,
			bool isGlobal) {
		char signal;
		ss.str("");
		if (isGlobal) {
			signal = 'G';
		} else {
			signal = 'L';
		}
		ss << signal;
		ss << memoryId;
		ss << '_';
		ss << address;
		return ss.str();
	}

	std::string createGlobalVarFullName(std::string varName, unsigned time,
			bool isStore) {
		char signal;
		ss.str("");
		ss << varName;
		if (isStore) {
			signal = 'S';
		} else {
			signal = 'L';
		}
		ss << signal;
		ss << time;
		return ss.str();
	}

	std::string createBarrierName(uint64_t address, unsigned releasedCount) {
		ss.str("");
		ss << address;
		ss << "#";
		ss << releasedCount;
		return ss.str();
	}

};

}

#endif /* PSOLISTENER_H_ */
