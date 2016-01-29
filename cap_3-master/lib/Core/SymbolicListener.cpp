/*
 * SymbolicListener.cpp
 *
 *  Created on: 2015年11月13日
 *      Author: zhy
 */

#include "SymbolicListener.h"
#include "klee/Expr.h"
#include "PTree.h"
#include "Trace.h"
#include "Transfer.h"
#include "AddressSpace.h"
#include "Memory.h"

#include <unistd.h>
#include <map>
#include <sstream>
#include <iostream>
#include <string>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/DebugInfo.h"
#else
#include "llvm/Metadata.h"
#include "llvm/Module.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Analysis/DebugInfo.h"
#endif


using namespace std;
using namespace llvm;

#define EVENTS_DEBUG 0

#define PTR 0
#define DEBUGSTRCPY 0
#define DEBUGSYMBOLIC 0
#define COND_DEBUG 0

namespace klee {

SymbolicListener::SymbolicListener(Executor* executor, RuntimeDataManager* rdManager) :
		BitcodeListener(), executor(executor), rdManager(rdManager) {
	Kind = SymbolicListenerKind;
	kleeBr = false;
}

SymbolicListener::~SymbolicListener() {
	// TODO Auto-generated destructor stub

}

//消息响应函数，在被测程序解释执行之前调用
void SymbolicListener::beforeRunMethodAsMain(ExecutionState &initialState) {

	//收集全局变量初始化值
	Trace* trace = rdManager->getCurrentTrace();
	currentEvent = trace->path.begin();
	endEvent = trace->path.end();
	//收集assert
	for (std::vector<KFunction*>::iterator i =
			executor->kmodule->functions.begin(), e =
			executor->kmodule->functions.end(); i != e; ++i) {
		KInstruction **instructions = (*i)->instructions;
		for (unsigned j = 0; j < (*i)->numInstructions; j++) {
			KInstruction *ki = instructions[j];
			Instruction* inst = ki->inst;
//			instructions[j]->inst->dump();
			if (inst->getOpcode() == Instruction::Call) {
				CallSite cs(inst);
				Value *fp = cs.getCalledValue();
				Function *f = executor->getTargetFunction(fp, initialState);
				if (f && f->getName().str() == "__assert_fail") {
					string fileName = ki->info->file;
					unsigned line = ki->info->line;
					assertMap[fileName].push_back(line);
//					printf("fileName : %s, line : %d\n",fileName.c_str(),line);
//					std::cerr << "call name : " << f->getName().str() << "\n";
				}
			}
		}
	}
}


void SymbolicListener::executeInstruction(ExecutionState &state, KInstruction *ki) {
	Trace* trace = rdManager->getCurrentTrace();
	if ((*currentEvent)) {
		Instruction* inst = ki->inst;
		Thread* thread = state.currentThread;
//		cerr << "event name : " << (*currentEvent)->eventName << " ";
//		cerr << "thread id : " << thread->threadId;
//		inst->dump();
//		cerr << "thread id : " << (*currentEvent)->threadId ;
//		(*currentEvent)->inst->inst->dump();
		switch (inst->getOpcode()) {
		case Instruction::Load: {
			ref<Expr> address = executor->eval(ki, 0, thread).value;
			if (address->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getFullName(address)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "load symbolic print");
				}
				executor->evalAgainst(ki, 0, thread, value);
			}
			break;
		}
		case Instruction::Store: {
			ref<Expr> address = executor->eval(ki, 1, thread).value;
			if (address->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getFullName(address)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "store address is symbolic");
				}
				executor->evalAgainst(ki, 1, thread, value);
			}

			ref<Expr> value = executor->eval(ki, 0, thread).value;
			ref<Expr> base = executor->eval(ki, 1, thread).value;
			Type::TypeID id = ki->inst->getOperand(0)->getType()->getTypeID();
//			cerr << "store value : " << value << std::endl;
//			cerr << "store base : " << base << std::endl;
//			cerr << "value->getKind() : " << value->getKind() << std::endl;
//			cerr << "TypeID id : " << id << std::endl;
			bool isFloat = 0;
			if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
				isFloat = 1;
			}
			if ((*currentEvent)->isGlobal) {
				if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
					Expr::Width size = executor->getWidthForLLVMType(ki->inst->getOperand(0)->getType());
					ref<Expr> address = executor->eval(ki, 1, thread).value;
					ref<Expr> symbolic = manualMakeSymbolic(state,
							(*currentEvent)->globalVarFullName, size, isFloat);
					ref<Expr> constraint = EqExpr::create(value, symbolic);
					trace->storeSymbolicExpr.push_back(constraint);
//					cerr << "event name : " << (*currentEvent)->eventName << "\n";
//					cerr << "store constraint : " << constraint << "\n";
					if (value->getKind() == Expr::Constant) {

					} else if (value->getKind() == Expr::Concat || value->getKind() == Expr::Read) {
						ref<Expr> svalue = symbolicMap[filter.getFullName(value)];
						if (svalue->getKind() != Expr::Constant) {
							assert(0 && "store value is symbolic");
						} else if (id == Type::PointerTyID && value->getKind() == Expr::Read) {
							assert (0 && "pointer is expr::read");
						}
						executor->evalAgainst(ki, 0, thread, svalue);
					} else {
						ref<Expr> svalue = (*currentEvent)->value.back();
						if (svalue->getKind() != Expr::Constant) {
							assert(0 && "store value is symbolic");
						} else 	if (id == Type::PointerTyID) {
							assert (0 && "pointer is other symbolic");
						}
						executor->evalAgainst(ki, 0, thread, svalue);
					}
				} else {
					if (value->getKind() != Expr::Constant) {
						assert(0 && "store value is symbolic and type is other");
					}
				}
			} else {
				//会丢失指针的一些关系约束，但是不影响。
				if (id == Type::PointerTyID && PTR) {
					if (value->getKind() == Expr::Concat){
						ref<Expr> svalue = symbolicMap[filter.getFullName(value)];
						if (svalue->getKind() != Expr::Constant) {
							assert(0 && "store pointer is symbolic");
						}
						executor->evalAgainst(ki, 0, thread, svalue);
						ref<Expr> address = executor->eval(ki, 1, thread).value;
						addressSymbolicMap[address] = value;
//						cerr << "address : " << address << " value : " << value << "\n";
					} else if (value->getKind() == Expr::Read) {
						assert (0 && "pointer is expr::read");
					} else {
						ref<Expr> address = executor->eval(ki, 1, thread).value;
						addressSymbolicMap[address] = value;
//						cerr << "address : " << address << " value : " << value << "\n";
					}
				} else if (isFloat || id == Type::IntegerTyID) {
					//局部非指针变量内存中可能存储符号值。
				} else {
					if (value->getKind() != Expr::Constant) {
						assert(0 && "store value is symbolic and type is other");
					}
				}
			}
			break;
		}
		case Instruction::Br: {
			BranchInst *bi = dyn_cast<BranchInst>(inst);
			if (!bi->isUnconditional()) {
				unsigned isAssert = 0;
				string fileName = ki->info->file;
				std::map<string, std::vector<unsigned> >::iterator it =
						assertMap.find(fileName);
				unsigned line = ki->info->line;
				if (it != assertMap.end()) {
					if (find(assertMap[fileName].begin(), assertMap[fileName].end(), line)
							!= assertMap[fileName].end()) {
						isAssert = 1;
					}
				}
				ref<Expr> value1 = executor->eval(ki, 0, thread).value;
				if (value1->getKind() != Expr::Constant) {
					Expr::Width width = value1->getWidth();
					ref<Expr> value2;
					if ((*currentEvent)->condition == true) {
						value2 = ConstantExpr::create(true, width);
					} else {
						value2 = ConstantExpr::create(false, width);
					}
					ref<Expr> constraint = EqExpr::create(value1, value2);
					if (isAssert) {
//						cerr << "event name : " << (*currentEvent)->eventName << "\n";
//						cerr << "assert constraint : " << constraint << "\n";
						trace->assertSymbolicExpr.push_back(constraint);
						trace->assertEvent.push_back((*currentEvent));
					} else if (kleeBr == false) {
//						cerr << "event name : " << (*currentEvent)->eventName << "\n";
//						cerr << "br constraint : " << constraint << "\n";
						trace->brSymbolicExpr.push_back(constraint);
						trace->brEvent.push_back((*currentEvent));
					}
					executor->evalAgainst(ki, 0, thread, value2);
				}
				if (kleeBr == true) {
					kleeBr = false;
				}
			}
			break;
		}
		case Instruction::Select: {

			break;
		}
		case Instruction::Call: {
			CallSite cs(inst);
			ref<Expr> function = executor->eval(ki, 0, thread).value;
			if (function->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getFullName(function)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "call function is symbolic");
				}
				executor->evalAgainst(ki, 0, thread, value);
			}
//			std::cerr<<"isFunctionWithSourceCode : ";
//					(*currentEvent)->inst->inst->dump();
//			std::cerr<<"isFunctionWithSourceCode : ";
//					inst->dump();
//			std::cerr<<"isFunctionWithSourceCode : "<<(*currentEvent)->isFunctionWithSourceCode<<"\n";
			if (!(*currentEvent)->isFunctionWithSourceCode) {
				unsigned numArgs = cs.arg_size();
				for (unsigned j = numArgs; j > 0; j--) {
					ref<Expr> value = executor->eval(ki, j, thread).value;
					Type::TypeID id = cs.getArgument(j-1)->getType()->getTypeID();
//					cerr << "value->getKind() : " << value->getKind() << std::endl;
//					cerr << "TypeID id : " << id << std::endl;
//		    		cerr<<"value : " << value << "\n";
					bool isFloat = 0;
					if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
						isFloat = 1;
					}
					if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
						if (value->getKind() == Expr::Constant) {

						} else if (value->getKind() == Expr::Concat || value->getKind() == Expr::Read) {
							ref<Expr> svalue = symbolicMap[filter.getFullName(value)];
							if (svalue->getKind() != Expr::Constant) {
								assert(0 && "store value is symbolic");
							} else if (id == Type::PointerTyID && value->getKind() == Expr::Read) {
								assert (0 && "pointer is expr::read");
							}
							executor->evalAgainst(ki, j, thread, svalue);
						} else {
							ref<Expr> svalue = (*currentEvent)->value.back();
							if (svalue->getKind() != Expr::Constant) {
								assert(0 && "store value is symbolic");
							} else 	if (id == Type::PointerTyID) {
								assert (0 && "pointer is other symbolic");
							}
							executor->evalAgainst(ki, j, thread, svalue);
						}
					} else {
						if (value->getKind() != Expr::Constant) {
							assert(0 && "store value is symbolic and type is other");
						}
					}
					(*currentEvent)->value.pop_back();
				}
			}
			break;
		}
		case Instruction::GetElementPtr: {
			KGEPInstruction *kgepi = static_cast<KGEPInstruction*>(ki);
			ref<Expr> base = executor->eval(ki, 0, thread).value;
			if (base->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getFullName(base)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "GetElementPtr symbolic print");
				}
				executor->evalAgainst(ki, 0, thread, value);
			} else if (base->getKind() == Expr::Read) {
				assert (0 && "pointer is expr::read");
			}
//			std::cerr << "kgepi->base : " << base << std::endl;
			std::vector<ref<klee::Expr> >::iterator first = (*currentEvent)->value.begin();
			for (std::vector<std::pair<unsigned, uint64_t> >::iterator
					it = kgepi->indices.begin(), ie = kgepi->indices.end();
					it != ie; ++it) {
				ref<Expr> index = executor->eval(ki, it->first, thread).value;
//				std::cerr << "kgepi->index : " << index << std::endl;
//				std::cerr << "first : " << *first << std::endl;
				if (index->getKind() != Expr::Constant) {
					executor->evalAgainst(ki, it->first, thread, *first);
					ref<Expr> constraint = EqExpr::create(index, *first);
//					cerr << "event name : " << (*currentEvent)->eventName << "\n";
//					cerr << "constraint : " << constraint << "\n";
					trace->brSymbolicExpr.push_back(constraint);
					trace->brEvent.push_back((*currentEvent));
				} else {
					if (index != *first) {
						assert(0 && "index != first");
					}
				}
				++first;
			}
			if (kgepi->offset) {
//				std::cerr<<"kgepi->offset : "<<kgepi->offset<<std::endl;
				//目前没有这种情况...
//				assert(0 &&"kgepi->offset");
			}
			break;
		}
		case Instruction::Switch: {
//			SwitchInst *si = cast<SwitchInst>(inst);
			ref<Expr> cond1 = executor->eval(ki, 0, thread).value;
			if (cond1->getKind() != Expr::Constant) {
				ref<Expr> cond2 = (*currentEvent)->value.back();
				ref<Expr> constraint = EqExpr::create(cond1, cond2);
				trace->brSymbolicExpr.push_back(constraint);
				trace->brEvent.push_back((*currentEvent));
				executor->evalAgainst(ki, 0, thread, cond2);
			}
			break;
		}
		case Instruction::PtrToInt: {
//			CastInst *ci = cast<CastInst>(inst);
//			Expr::Width iType = executor->getWidthForLLVMType(ci->getType());
			ref<Expr> arg = executor->eval(ki, 0, thread).value;
			if (arg->getKind() == Expr::Concat) {
				ref<Expr> value = symbolicMap[filter.getFullName(arg)];
				if (value->getKind() != Expr::Constant) {
					assert(0 && "GetElementPtr symbolic print");
				}
				executor->evalAgainst(ki, 0, thread, value);
			} else if (arg->getKind() == Expr::Read) {
				assert (0 && "pointer is expr::read");
			}
			break;
		}
		default: {
			break;
		}
		}
	}
}

void SymbolicListener::instructionExecuted(ExecutionState &state, KInstruction *ki) {
	Trace* trace = rdManager->getCurrentTrace();
	if ((*currentEvent)) {
		Instruction* inst = ki->inst;
		Thread* thread = state.currentThread;
		switch (inst->getOpcode()) {
		case Instruction::Load: {
			ref<Expr> value = executor->getDestCell(state.currentThread, ki).value;
//			cerr << "value : " << value << "\n";
			bool isFloat = 0;
			Type::TypeID id = ki->inst->getType()->getTypeID();
			if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
				isFloat = 1;
			}
			if ((*currentEvent)->isGlobal) {

				//指针！！！
#if PTR
				if (isFloat || id == Type::IntegerTyID || id == Type::PointerTyID) {
#else
				if (isFloat || id == Type::IntegerTyID) {
#endif

					Expr::Width size = executor->getWidthForLLVMType(ki->inst->getType());
					ref<Expr> address = executor->eval(ki, 0, thread).value;
					ref<Expr> value = executor->getDestCell(thread, ki).value;
					ref<Expr> symbolic = manualMakeSymbolic(state,
							(*currentEvent)->globalVarFullName, size, isFloat);
					executor->setDestCell(thread, ki, symbolic);
					symbolicMap[(*currentEvent)->globalVarFullName] = value;
//					cerr << "load globalVarFullName : " << (*currentEvent)->globalVarFullName << "\n";
//					cerr << "load value : " << value << "\n";
					ref<Expr> constraint = EqExpr::create(value, symbolic);
//					cerr << "rwSymbolicExpr : " << constraint << "\n";
					trace->rwSymbolicExpr.push_back(constraint);
					trace->rwEvent.push_back(*currentEvent);
				}
			} else {
				//会丢失指针的一些关系约束，但是不影响。
				if (id == Type::PointerTyID && PTR) {
					ref<Expr> address = executor->eval(ki, 0, thread).value;
					for (std::map<ref<Expr>, ref<Expr> >::iterator it = addressSymbolicMap.begin(), ie =
							addressSymbolicMap.end(); it != ie; ++it) {
						if (it->first == address){
//							cerr << "it->first : " << it->first << " it->second : " << it->second << "\n";
							executor->setDestCell(state.currentThread, ki, it->second);
							break;
						}
					}
				} else {

				}
			}
			if (isFloat) {
				thread->stack.back().locals[ki->dest].value.get()->isFloat =
						true;
			}
			break;
		}

		case Instruction::Store: {
			break;
		}
		case Instruction::Call: {
			CallSite cs(inst);
			Function *f = (*currentEvent)->calledFunction;
			//可能存在未知错误
//			Value *fp = cs.getCalledValue();
//			Function *f = executor->getTargetFunction(fp, state);
//			if (!f) {
//				ref<Expr> expr = executor->eval(ki, 0, thread).value;
//				ConstantExpr* constExpr = dyn_cast<ConstantExpr>(expr.get());
//				uint64_t functionPtr = constExpr->getZExtValue();
//				f = (Function*) functionPtr;
//			}

			//有待考证
//			if (!f->getName().startswith("klee") && !executor->kmodule->functionMap[f]) {
			if (!(*currentEvent)->isFunctionWithSourceCode) {
				ref<Expr> returnValue = executor->getDestCell(state.currentThread, ki).value;
				bool isFloat = 0;
				Type::TypeID id = inst->getType()->getTypeID();
				if ((id >= Type::HalfTyID) && (id <= Type::DoubleTyID)) {
					isFloat = 1;
				}
				if (isFloat) {
					returnValue.get()->isFloat = true;
				}
				executor->setDestCell(state.currentThread, ki, returnValue);
			}
//			if (!executor->kmodule->functionMap[f] && !inst->getType()->isVoidTy()) {
//				ref<Expr> value = executor->getDestCell(state.currentThread, ki).value;
//				cerr << "value : " << value << "\n";
//			}

			//需要添加Map操作
			if (f->getName().startswith("klee_div_zero_check")) {
				kleeBr = true;
			} else if (f->getName().startswith("klee_overshift_check")) {
				kleeBr = true;
			} else if (f->getName() == "strcpy") {
				//地址可能还有问题
				ref<Expr> destAddress = executor->eval(ki, 1, state.currentThread).value;
//				ref<Expr> scrAddress = executor->eval(ki, 0,
//						state.currentThread).value;
//				ObjectPair scrop;
				ObjectPair destop;
//				getMemoryObject(scrop, state, scrAddress);
				executor->getMemoryObject(destop, state, destAddress);
				const ObjectState* destos = destop.second;
				const MemoryObject* destmo = destop.first;
//				std::cerr<<destAddress<<std::endl;
//				std::cerr<<destmo->address<<std::endl;
//				std::cerr<<"destmo->size : "<<destmo->size<<std::endl;
				Expr::Width size = 8;
				for (unsigned i = 0; i < (*currentEvent)->implicitGlobalVar.size(); i++) {
//					std::cerr<<"dest"<<std::endl;
					ref<Expr> address = AddExpr::create(destAddress, ConstantExpr::create(i, BIT_WIDTH));
					ref<Expr> value = destos->read(destmo->getOffsetExpr(address), size);
//					std::cerr<<"value : "<<value<<std::endl;
//					std::cerr<<"value : "<<value<<std::endl;
					if (executor->isGlobalMO(destmo)) {
						ref<Expr> value2 = manualMakeSymbolic(state,
								(*currentEvent)->implicitGlobalVar[i], size, false);
						ref<Expr> value1 = value;
						ref<Expr> constraint = EqExpr::create(value1, value2);
						trace->storeSymbolicExpr.push_back(constraint);
//						cerr << "constraint : " << constraint << "\n";
//						cerr << "Store Map varName : " << (*currentEvent)->varName << "\n";
//						cerr << "Store Map value : " << value << "\n";
					}
					if (value->isZero()) {
						break;
					}
				}
			} else if (f->getName() == "pthread_create") {
				ref<Expr> pthreadAddress = executor->eval(ki, 1, state.currentThread).value;
				ObjectPair pthreadop;
				executor->getMemoryObject(pthreadop, state, pthreadAddress);
				const ObjectState* pthreados = pthreadop.second;
				const MemoryObject* pthreadmo = pthreadop.first;
				Expr::Width size = BIT_WIDTH;
				ref<Expr> value = pthreados->read(0, size);
				if (executor->isGlobalMO(pthreadmo)) {
					string globalVarFullName = (*currentEvent)->globalVarFullName;
//					cerr << "globalVarFullName : " << globalVarFullName << "\n";
					symbolicMap[globalVarFullName] = value;
				}
//				cerr << "pthread id : " << value << "\n";
			}
			break;
		}
		case Instruction::PHI: {
//			ref<Expr> result = executor->eval(ki, thread->incomingBBIndex, thread).value;
//			cerr << "PHI : " << result << "\n";
			break;
		}
		case Instruction::GetElementPtr: {
//			ref<Expr> value = executor->getDestCell(state.currentThread, ki).value;
//			cerr << "value : " << value << "\n";
			break;
		}
		case Instruction::SExt: {
//			ref<Expr> value = executor->getDestCell(state.currentThread, ki).value;
//			cerr << "value : " << value << "\n";
			break;
		}
		default: {

			break;
		}

		}
	}
	if (currentEvent != endEvent)
		currentEvent++;
}


//消息响应函数，在被测程序解释执行之后调用
void SymbolicListener::afterRunMethodAsMain() {
	//TODO: Add Encoding Feature
	symbolicMap.clear();
	addressSymbolicMap.clear();
	assertMap.clear();

	cerr << "######################本条路径为新路径####################\n";
#if EVENTS_DEBUG
	//true: output to file; false: output to terminal
	rdManager.printCurrentTrace(true);
	//			encode.showInitTrace();//need to be modified
#endif
}


//消息相应函数，在创建了新线程之后调用
void SymbolicListener::createThread(ExecutionState &state, Thread* thread) {

}


//消息相应函数，在前缀执行出错之后程序推出之前调用
void SymbolicListener::executionFailed(ExecutionState &state, KInstruction *ki) {
	rdManager->getCurrentTrace()->traceType = Trace::FAILED;
}

ref<Expr> SymbolicListener::manualMakeSymbolic(ExecutionState& state,
		std::string name, unsigned size, bool isFloat) {

	//添加新的符号变量
	const Array *array = new Array(name, size, isFloat);
	ObjectState *os = new ObjectState(size, array);
	ref<Expr> offset = ConstantExpr::create(0, BIT_WIDTH);
	ref<Expr> result = os->read(offset, size);
	if (isFloat) {
		result.get()->isFloat = true;
	}
#if DEBUGSYMBOLIC
	cerr << "Event name : " << (*currentEvent)->eventName << "\n";
	cerr << "make symboic:" << name << std::endl;
	cerr << "is float:" << isFloat << std::endl;
	std::cerr << "result : ";
	result->dump();
#endif
	return result;
}

ref<Expr> SymbolicListener::readExpr(ExecutionState &state, ref<Expr> address,
		Expr::Width size) {
	ObjectPair op;
	executor->getMemoryObject(op, state, address);
	const MemoryObject *mo = op.first;
	ref<Expr> offset = mo->getOffsetExpr(address);
	const ObjectState *os = op.second;
	ref<Expr> result = os->read(offset, size);
	return result;
}

void SymbolicListener::storeZeroToExpr(ExecutionState& state, ref<Expr> address,
		Expr::Width size) {

	ref<Expr> value = ConstantExpr::create(0, size);
	executor->executeMemoryOperation(state, true, address, value, 0);
}

}


