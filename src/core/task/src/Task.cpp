/*
 * Copyright 2016 - 2021  Angelo Matni, Simone Campanoni
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is furnished to do
 so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "noelle/core/Task.hpp"

namespace llvm::noelle {

Task::Task(FunctionType *taskSignature, Module &M)
  : instanceIndexV{ nullptr },
    envArg{ nullptr } {

  /*
   * Make task IDs unique
   */
  this->ID = Task::currentID;
  Task::currentID++;

  /*
   * Create the name of the function.
   */
  auto functionName = std::string{ "noelle_task_" };
  functionName.append(std::to_string(this->ID));

  /*
   * Create the empty body of the task.
   */
  auto functionCallee = M.getOrInsertFunction(functionName, taskSignature);
  this->F = cast<Function>(functionCallee.getCallee());
  if (!this->F->empty()) {
    errs() << "Task: ERROR = function " << functionName
           << " already exists in the program.\n";
    abort();
  }

  /*
   * Add the entry and exit basic blocks.
   */
  auto &cxt = M.getContext();
  this->entryBlock = BasicBlock::Create(cxt, "", this->F);
  this->exitBlock = BasicBlock::Create(cxt, "", this->F);

  /*
   * Add the return instruction.
   */
  IRBuilder<> exitB(this->exitBlock);
  exitB.CreateRetVoid();

  return;
}

uint32_t Task::getID(void) const {
  return this->ID;
}

void Task::addSkippedEnvironmentVariable(Value *v) {
  assert(this->skippedEnvironmentVariables.find(v)
             == this->skippedEnvironmentVariables.end()
         && "Skipped environment variable is skipped already\n");
  this->skippedEnvironmentVariables.insert(v);

  return;
}

bool Task::isSkippedEnvironmentVariable(Value *v) const {
  return this->skippedEnvironmentVariables.find(v)
         != this->skippedEnvironmentVariables.end();
}

bool Task::isAnOriginalLiveIn(Value *v) const {
  if (this->liveInClones.find(v) == this->liveInClones.end()) {
    return false;
  }

  return true;
}

Value *Task::getCloneOfOriginalLiveIn(Value *o) const {
  if (!this->isAnOriginalLiveIn(o)) {
    return nullptr;
  }

  return this->liveInClones.at(o);
}

void Task::addLiveIn(Value *original, Value *internal) {
  this->liveInClones[original] = internal;

  return;
}

void Task::removeLiveIn(Instruction *original) {

  /*
   * Find the element in the map.
   */
  auto it = this->liveInClones.find(original);
  if (it == this->liveInClones.end()) {
    return;
  }

  /*
   * Remove the load of the live-in
   */
  auto clonedValue = this->liveInClones[original];
  if (auto loadInst = dyn_cast<Instruction>(clonedValue)) {
    loadInst->eraseFromParent();
  }

  /*
   * Remove the live-in.
   */
  this->liveInClones.erase(it);

  return;
}

std::unordered_set<Value *> Task::getOriginalLiveIns(void) const {
  std::unordered_set<Value *> s;
  for (auto p : this->liveInClones) {
    s.insert(p.first);
  }

  return s;
}

bool Task::doesOriginalLiveOutHaveManyClones(Instruction *I) const {
  return liveOutClones.find(I) != liveOutClones.end();
}

std::unordered_set<Instruction *> Task::getClonesOfOriginalLiveOut(
    Instruction *I) const {
  if (liveOutClones.find(I) == liveOutClones.end()) {
    return {};
  }

  return liveOutClones.at(I);
}

void Task::addLiveOut(Instruction *original, Instruction *internal) {
  liveOutClones[original].insert(internal);
}

void Task::removeLiveOut(Instruction *original, Instruction *removed) {
  if (liveOutClones.find(original) == liveOutClones.end()) {
    return;
  }

  liveOutClones[original].erase(removed);
}

bool Task::isAnOriginalBasicBlock(BasicBlock *o) const {
  if (this->basicBlockClones.find(o) == this->basicBlockClones.end()) {
    return false;
  }

  return true;
}

BasicBlock *Task::getCloneOfOriginalBasicBlock(BasicBlock *o) const {
  if (!this->isAnOriginalBasicBlock(o)) {
    return nullptr;
  }

  return this->basicBlockClones.at(o);
}

void Task::removeOriginalBasicBlock(BasicBlock *b) {
  this->basicBlockClones.erase(b);

  return;
}

std::unordered_set<BasicBlock *> Task::getOriginalBasicBlocks(void) const {
  std::unordered_set<BasicBlock *> s;
  for (auto p : this->basicBlockClones) {
    s.insert(p.first);
  }

  return s;
}

void Task::addBasicBlock(BasicBlock *original, BasicBlock *internal) {
  this->basicBlockClones[original] = internal;

  // this->adjustDataAndControlFlowToUseClones();

  return;
}

BasicBlock *Task::addBasicBlockStub(BasicBlock *original) {

  /*
   * Fetch the context.
   */
  auto &c = this->getLLVMContext();

  /*
   * Allocate a new basic block.
   */
  auto newBB = BasicBlock::Create(c, "", this->F);

  /*
   * Keep track of the mapping.
   */
  this->addBasicBlock(original, newBB);

  return newBB;
}

BasicBlock *Task::cloneAndAddBasicBlock(BasicBlock *original) {
  auto f = [](Instruction *o) -> bool { return true; };
  auto newBB = this->cloneAndAddBasicBlock(original, f);

  // this->adjustDataAndControlFlowToUseClones();

  return newBB;
}

BasicBlock *Task::cloneAndAddBasicBlock(
    BasicBlock *original,
    std::function<bool(Instruction *origInst)> filter) {

  /*
   * Create a stub.
   */
  auto cloneBB = this->addBasicBlockStub(original);

  /*
   * Copy the original instructions into the cloned basic block.
   */
  IRBuilder<> builder(cloneBB);
  for (auto &I : *original) {

    /*
     * Check if we should add the current instruction.
     */
    if (!filter(&I)) {
      continue;
    }

    /*
     * Add the current instruction to the task.
     */
    auto cloneI = builder.Insert(I.clone());
    this->instructionClones[&I] = cloneI;
    this->instructionCloneToOriginal[cloneI] = &I;
  }

  // this->adjustDataAndControlFlowToUseClones();

  return cloneBB;
}

void Task::cloneAndAddBasicBlocks(const std::unordered_set<BasicBlock *> &bbs) {
  auto filter = [](Instruction *i) -> bool { return true; };
  this->cloneAndAddBasicBlocks(bbs, filter);

  return;
}

void Task::cloneAndAddBasicBlocks(
    const std::unordered_set<BasicBlock *> &bbs,
    std::function<bool(Instruction *origInst)> filter) {

  /*
   * Clone all the basic blocks given as input.
   */
  for (auto originBB : bbs) {
    this->cloneAndAddBasicBlock(originBB, filter);
  }

  // this->adjustDataAndControlFlowToUseClones();

  return;
}

Value *Task::getTaskInstanceID(void) const {
  return this->instanceIndexV;
}

Value *Task::getEnvironment(void) const {
  return this->envArg;
}

Function *Task::getTaskBody(void) const {
  return this->F;
}

BasicBlock *Task::getEntry(void) const {
  return this->entryBlock;
}

BasicBlock *Task::getExit(void) const {
  return this->exitBlock;
}

void Task::tagBasicBlockAsLastBlock(BasicBlock *b) {
  this->lastBlocks.push_back(b);

  return;
}

uint32_t Task::getNumberOfLastBlocks(void) const {
  return this->lastBlocks.size();
}

BasicBlock *Task::getLastBlock(uint32_t blockID) const {
  return this->lastBlocks[blockID];
}

LLVMContext &Task::getLLVMContext(void) const {
  auto &c = this->F->getContext();

  return c;
}

Instruction *Task::getCloneOfOriginalInstruction(Instruction *o) const {
  if (!this->isAnOriginalInstruction(o)) {
    return nullptr;
  }

  return this->instructionClones.at(o);
}

Instruction *Task::getOriginalInstructionOfClone(Instruction *c) const {
  if (this->instructionCloneToOriginal.find(c)
      == this->instructionCloneToOriginal.end()) {
    return nullptr;
  }

  auto o = this->instructionCloneToOriginal.at(c);
  return o;
}

bool Task::isAnOriginalInstruction(Instruction *i) const {
  if (this->instructionClones.find(i) == this->instructionClones.end()) {
    return false;
  }

  return true;
}

bool Task::isAClonedInstruction(Instruction *i) const {
  if (i->getFunction() != this->getTaskBody()) {
    return false;
  }

  return true;
}

void Task::addInstruction(Instruction *original, Instruction *internal) {
  this->instructionClones[original] = internal;
  this->instructionCloneToOriginal[internal] = original;

  // this->adjustDataAndControlFlowToUseClones();

  return;
}

std::unordered_set<Instruction *> Task::getOriginalInstructions(void) const {
  std::unordered_set<Instruction *> s;
  for (auto p : this->instructionClones) {
    s.insert(p.first);
  }

  return s;
}

Instruction *Task::cloneAndAddInstruction(Instruction *original) {
  auto cloneI = original->clone();

  this->addInstruction(original, cloneI);

  // this->adjustDataAndControlFlowToUseClones();

  return cloneI;
}

void Task::removeOriginalInstruction(Instruction *o) {
  this->instructionClones.erase(o);
  for (auto pair : this->instructionCloneToOriginal) {
    if (pair.second == o) {
      pair.second = nullptr;
    }
  }

  return;
}

Task::~Task() {
  return;
}

uint64_t Task::currentID = 0;

} // namespace llvm::noelle
