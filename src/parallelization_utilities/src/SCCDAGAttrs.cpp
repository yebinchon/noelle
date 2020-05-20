/*
 * Copyright 2016 - 2019  Angelo Matni, Simone Campanoni
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "SCCDAGAttrs.hpp"

using namespace llvm;

void SCCDAGAttrs::populate (SCCDAG *loopSCCDAG, LoopsSummary &LIS, ScalarEvolution &SE, DominatorSummary &DS) {

  /*
   * Set the SCCDAG.
   */
  this->sccdag = loopSCCDAG;

  /*
   * Partition dependences between intra-iteration and iter-iteration ones.
   */
  collectDependencies(LIS, DS);

  /*
   * Tag SCCs depending on their characteristics.
   */
  loopSCCDAG->iterateOverSCCs([this, &SE, &LIS](SCC *scc) -> bool {

    /*
     * Allocate the metadata about this SCC.
     */
    auto sccInfo = new SCCAttrs(scc, this->accumOpInfo);
    this->sccToInfo[scc] = sccInfo;

    /*
     * Collect information about the current SCC.
     */
    this->checkIfInductionVariableSCC(scc, SE, LIS);
    if (sccInfo->isInductionVariableSCC()) {
      this->checkIfIVHasFixedBounds(scc, LIS);
    }
    this->checkIfClonable(scc, SE);

    /*
     * Tag the current SCC.
     */
    if (this->checkIfIndependent(scc)) {
      sccInfo->setType(SCCAttrs::SCCType::INDEPENDENT);
    } else if (this->checkIfReducible(scc, LIS)) {
      sccInfo->setType(SCCAttrs::SCCType::REDUCIBLE);
    } else {
      sccInfo->setType(SCCAttrs::SCCType::SEQUENTIAL);
    }

    return false;
  });

  collectSCCGraphAssumingDistributedClones();

  return ;
}

std::set<SCC *> SCCDAGAttrs::getSCCsWithLoopCarriedDependencies (void) const {
  std::set<SCC *> sccs;
  for (auto &sccDependencies : this->interIterDepsInternalToSCC) {
    sccs.insert(sccDependencies.first);
  }
  return sccs;
}
      
std::set<SCC *> SCCDAGAttrs::getSCCsWithLoopCarriedControlDependencies (void) const {
  std::set<SCC *> sccs;

  /*
   * Iterate over SCCs with loop-carried data dependences.
   */
  for (auto &sccDependencies : this->interIterDepsInternalToSCC) {

    /*
     * Fetch the SCC.
     */
    auto SCC = sccDependencies.first;

    /*
     * Fetch the set of loop-carried data dependences of the current SCC.
     */
    auto &deps = sccDependencies.second;

    /*
     * Check if this SCC has a control loop-carried data dependence.
     */
    auto isControl = false;
    for (auto dep : deps){
      if (dep->isControlDependence()){
        isControl = true;
        break ;
      }
    }
    if (isControl){
      sccs.insert(sccDependencies.first);
    }
  }

  return sccs;
}

std::set<SCC *> SCCDAGAttrs::getSCCsWithLoopCarriedDataDependencies (void) const {
  std::set<SCC *> sccs;

  /*
   * Iterate over SCCs with loop-carried data dependences.
   */
  for (auto &sccDependencies : this->interIterDeps) {

    /*
     * Fetch the SCC.
     */
    auto SCC = sccDependencies.first;

    /*
     * Fetch the set of loop-carried data dependences of the current SCC.
     */
    auto &deps = sccDependencies.second;

    /*
     * Check if this SCC has data loop-carried data dependence.
     */
    auto isData = false;
    for (auto dep : deps){
      if (dep->isDataDependence()){
        isData = true;
        break ;
      }
    }
    if (isData){
      sccs.insert(SCC);
    }
  }
  return sccs;
}

/*
 * Assumption(angelo): An induction variable will be the root SCC of the loop
 */
bool SCCDAGAttrs::isLoopGovernedByIV () const {
  auto topLevelNodes = this->sccdag->getTopLevelNodes();

  /*
   * Step 1: Isolate top level SCCs (excluding independent instructions in SCCDAG)
   */
  std::queue<DGNode<SCC> *> toTraverse;
  for (auto node : topLevelNodes) {
    toTraverse.push(node);
  }
  std::set<SCC *> topLevelSCCs;
  while (!toTraverse.empty()) {

    /*
     * Fetch the current SCC and its metadata.
     */
    auto node = toTraverse.front();
    auto scc = node->getT();
    toTraverse.pop();
    auto sccInfo = this->getSCCAttrs(scc);

    if (sccInfo->canExecuteIndependently()) {
      auto nextDepth = this->sccdag->getNextDepthNodes(node);
      for (auto next : nextDepth) toTraverse.push(next);
      continue;
    }
    topLevelSCCs.insert(scc);
  }

  /*
   * Step 2: Ensure there is only 1, and that it is an induction variable
   */
  if (topLevelSCCs.size() != 1) return false;
  auto topLevelSCC = *topLevelSCCs.begin();
  auto topLevelSCCInfo = this->getSCCAttrs(topLevelSCC);
  return topLevelSCCInfo->isInductionVariableSCC();
}

bool SCCDAGAttrs::areAllLiveOutValuesReducable (LoopEnvironment *env) const {

  /*
   * Iterate over live-out variables.
   */
  for (auto envIndex : env->getEnvIndicesOfLiveOutVars()) {

    /*
     * Fetch the SCC that contains the producer of the environment variable.
     */
    auto producer = env->producerAt(envIndex);
    auto scc = this->sccdag->sccOfValue(producer);

    /*
     * Check the SCC type.
     */
    auto sccInfo = this->getSCCAttrs(scc);
    if (sccInfo->getType() == SCCAttrs::SCCType::INDEPENDENT) {
      continue ;
    }
    if (sccInfo->getType() == SCCAttrs::SCCType::REDUCIBLE) {
      continue ;
    }

    return false;
  }

  return true;
}

bool SCCDAGAttrs::isSCCContainedInSubloop (LoopsSummary &LIS, SCC *scc) const {
  auto instInSubloops = true;
  auto topLoop = LIS.getLoopNestingTreeRoot();
  for (auto iNodePair : scc->internalNodePairs()) {
    if (auto inst = dyn_cast<Instruction>(iNodePair.first)) {
      instInSubloops &= LIS.getLoop(inst) != topLoop;
    } else {
      instInSubloops = false;
    }
  }

  return instInSubloops;
}

SCCAttrs * SCCDAGAttrs::getSCCAttrs (SCC *scc) const {
  auto sccInfo = this->sccToInfo.find(scc);
  if (sccInfo == this->sccToInfo.end()){
    return nullptr;
  }
  return sccInfo->second;
}

void SCCDAGAttrs::collectSCCGraphAssumingDistributedClones () {
  auto addIncomingNodes = [&](std::queue<DGNode<SCC> *> &queue, DGNode<SCC> *node) -> void {
    std::set<DGNode<SCC> *> nodes;
    auto scc = node->getT();
    for (auto edge : node->getIncomingEdges()) {
      nodes.insert(edge->getOutgoingNode());
      this->edgesViaClones[scc].insert(edge);
    }
    for (auto node : nodes) queue.push(node);
  };

  for (auto childSCCNode : this->sccdag->getNodes()) {
    auto childSCC = childSCCNode->getT();
    std::queue<DGNode<SCC> *> nodesToCheck;
    addIncomingNodes(nodesToCheck, childSCCNode);

    while (!nodesToCheck.empty()) {
      auto node = nodesToCheck.front();
      nodesToCheck.pop();
      auto scc = node->getT();
      auto sccInfo = this->getSCCAttrs(scc);
      this->parentsViaClones[childSCC].insert(scc);
      if (sccInfo->canBeCloned()) {
        addIncomingNodes(nodesToCheck, node);
      }
    }
  }

  return ;
}

void SCCDAGAttrs::collectDependencies (LoopsSummary &LIS, DominatorSummary &DS) {

  for (auto sccNode : this->sccdag->getNodes()) {
    auto scc = sccNode->getT();
    for (auto edge : scc->getEdges()) {
      if (!isa<Instruction>(edge->getOutgoingT())) continue ;
      if (!isa<Instruction>(edge->getIncomingT())) continue ;

      auto instFrom = dyn_cast<Instruction>(edge->getOutgoingT());
      auto instTo = dyn_cast<Instruction>(edge->getIncomingT());
      if (LIS.getLoop(instFrom) == nullptr || LIS.getLoop(instTo) == nullptr) {
        continue;
      }

      if (instFrom == instTo || !DS.DT.dominates(instFrom, instTo)) {
        interIterDeps[scc].insert(edge);
      } else {
        intraIterDeps[scc].insert(edge);
      }
    }

    if (interIterDeps.find(scc) != interIterDeps.end()) {
      for (auto dependency : interIterDeps[scc]) {
        if (scc->isInternal(dependency->getOutgoingT()) && scc->isInternal(dependency->getIncomingT())) {
          interIterDepsInternalToSCC[scc].insert(dependency);
        }
      }
    }
  }

  return ;
}

// TODO: Consolidate this logic and its equivalent in PDGAnalysis
bool SCCDAGAttrs::checkIfReducible (SCC *scc, LoopsSummary &LIS) {

  /*
   * Fetch the attributes of the current SCC.
   */
  auto sccInfo = this->getSCCAttrs(scc);

  /*
   * Requirement: all instructions of the SCC belong to the same loop.
   */
  LoopSummary *loopOfSCC = nullptr;
  for (auto instNodePair : scc->internalNodePairs()){
    if (auto inst = dyn_cast<Instruction>(instNodePair.first)){
      auto currentLoop = LIS.getLoop(inst);
      if (loopOfSCC == nullptr){
        loopOfSCC = currentLoop ;
        continue ;
      }
      if (loopOfSCC != currentLoop){
        return false;
      }
    }
  }

  /*
   * Requirement: There are no memory dependences that connect an instruction of the SCC with another one outside that SCC.
   * Requirement: There are no outgoing control or data dependencies to any non-trivial SCC
   * TODO: improvement: we can also accept if a memory dependence exists from an instruction of the SCC with another one outside the loop the SCC is contained in (and any sub-loop of it).
   */
  auto sccNode = sccdag->fetchNode(scc);
  for (auto edge : sccNode->getAllConnectedEdges()) {
    for (auto subEdge : edge->getSubEdges()) {
      if (subEdge->isMemoryDependence()) {
        return false;
      }
    }
  }

  for (auto edge : sccNode->getOutgoingEdges()) {
    auto dependentSCC = edge->getIncomingT();
    if (dependentSCC == scc) continue;

    /*
    * TODO: This is a bit conservative. Ideally, we would check that all transitively dependent SCCs
    * are trivial, which still allows this SCC to be reduced.
    */
    auto dependentSCCNode = edge->getIncomingNode();
    if (dependentSCC->numInternalNodes() > 1 || dependentSCCNode->numOutgoingEdges() > 0) {
      return false;
    }
  }

  /*
   * Requirement: There is a single loop-carried data dependence between instructions of the SCC via variables.
   */
  uint32_t loopCarriedDataDeps = 0;
  if (interIterDepsInternalToSCC.find(scc) == interIterDepsInternalToSCC.end()) {
    return false;
  }
  for (auto edge : interIterDepsInternalToSCC[scc]) {

    /*
     * Check that the loop-carried dependence is a data dependence one.
     */
    if (edge->isControlDependence()) {
      return false;
    }

    /*
     * Check that the loop-carried data dependence is through variables.
     */
    if (edge->isMemoryDependence()) {
      return false;
    }

    /*
     * Check that the source and destination of the dependence are instructions.
     */
    auto outI = isa<Instruction>(edge->getOutgoingT())
      ? cast<Instruction>(edge->getOutgoingT()) : nullptr;
    auto inI = isa<Instruction>(edge->getIncomingT())
      ? cast<Instruction>(edge->getIncomingT()) : nullptr;
    if (!outI || !inI) {
      return false;
    }

    /*
     * Increase the counter.
     */
    loopCarriedDataDeps++;
  }
  if (loopCarriedDataDeps > 1) {
    return false;
  }

  /*
   * Requirement: Control flow is intra-iteration; conditions are
   * determined externally to the SCC
   */
  for (auto pair : sccInfo->controlPairs) {
    if (scc->isInternal(pair.first)) {
      return false;
    }
  }

  /*
   * Requirement: all PHI incoming values from within a loop iteration
   * are from other internal PHIs (no PHI = constant, etc... business)
   * so that accumulation is truly expressed solely by accumulators
   */
  for (auto phi : sccInfo->getPHIs()) {

    /*
     * Fetch the loop that contains the current PHI.
     */
    auto loopOfPHI = LIS.getLoop(phi);

    /*
     * Check all incoming values of the current PHI.
     */
    for (auto i = 0; i < phi->getNumIncomingValues(); ++i) {

      /*
       * Fetch the current incoming value.
       */
      auto incomingValue = phi->getIncomingValue(i);
      auto incomingBB = phi->getIncomingBlock(i);
      auto loopOfIncoming = LIS.getLoop(incomingBB);

      /*
       * Check whether the incoming value is from any loop.
       */
      if (loopOfIncoming == nullptr){

        /*
         * It is from outside any loop, so it is not a problem as being loop invariant.
         */
        continue ;
      }

      /*
       * Check if the incoming value is from a different loop of the one that contains the PHI.
       */
      if (loopOfIncoming != loopOfPHI) {
        continue;
      }

      /*
       * The incoming value is from the same loop of the PHI.
       * Check if it comes from a different PHI or a unique accumulator of the current SCC.
       */
      if (!this->isDerivedPHIOrAccumulator(incomingValue, scc)) {
        return false;
      }
    }
  }

  /*
   * Requirement: 1+ accumulators that are all side effect free
   * Requirement: all accumulators act on one PHI/accumulator in the SCC
   *  and one constant or external value
   */
  if (sccInfo->numberOfAccumulators() == 0) {
    return false;
  }
  auto accums = sccInfo->getAccumulators();
  for (auto accum : accums) {
    unsigned opCode = accum->getOpcode();
    if (accumOpInfo.sideEffectFreeOps.find(opCode) == accumOpInfo.sideEffectFreeOps.end()) {
      return false;
    }

    auto opL = accum->getOperand(0);
    auto opR = accum->getOperand(1);
    if (!(isDerivedWithinSCC(opL, scc) ^ isDerivedWithinSCC(opR, scc))) {
      return false;
    }
    if (!(isDerivedPHIOrAccumulator(opL, scc) ^ isDerivedPHIOrAccumulator(opR, scc))) {
      return false;
    }
  }

  /*
   * Requirement: instructions are all Add/Sub or all Mul
   * Requirement: second operand of subtraction must be external
   */
  bool isFirstMul = accumOpInfo.isMulOp((*accums.begin())->getOpcode());
  for (auto accum : accums) {
    bool isMul = accumOpInfo.isMulOp(accum->getOpcode());
    if (isMul ^ isFirstMul) {
      return false;
    }
    if (accumOpInfo.isSubOp(accum->getOpcode())) {
      if (scc->isInternal(accum->getOperand(1))) {
        return false;
      }
    }
  }

  return true;
}

/*
 * The SCC is independent if it doesn't have loop carried data dependencies
 */
bool SCCDAGAttrs::checkIfIndependent (SCC *scc) {
  return interIterDepsInternalToSCC.find(scc) == interIterDepsInternalToSCC.end();
}

bool SCCDAGAttrs::checkIfInductionVariableSCC (SCC *scc, ScalarEvolution &SE, LoopsSummary &LIS) {
  auto sccInfo = this->getSCCAttrs(scc);
  auto setHasIV = [&](bool hasIV) -> bool {
    sccInfo->setSCCToBeInductionVariable(hasIV);
    return sccInfo->isInductionVariableSCC();
  };

  /*
   * Check whether there is a single conditional branch that dictates control flow in the SCC
   */
  auto termPtr = sccInfo->getSingleInstructionThatControlLoopExit();
  if (termPtr == nullptr) {
    return setHasIV(false);
  }

  /*
   * Identify single conditional branch that dictates control flow in the SCC
   */
  auto& termPair = *termPtr;
  auto term = termPair.second;
  assert(term->isTerminator());
  if (!term || !isa<BranchInst>(term)) return setHasIV(false);
  auto condition = termPair.first;
  if (!condition || !isa<CmpInst>(condition)) return setHasIV(false);
  CmpInst *cmp = (CmpInst*)condition;

  /*
   * Identify, on the CmpInst, a PHINode or accumulator, and some value not
   * derived within the SCC
   */
  auto opL = cmp->getOperand(0), opR = cmp->getOperand(1);
  if (!(isDerivedWithinSCC(opL, scc) ^ isDerivedWithinSCC(opR, scc))) return setHasIV(false);
  if (!(isDerivedPHIOrAccumulator(opL, scc) ^ isDerivedPHIOrAccumulator(opR, scc))) return setHasIV(false);

  /*
   * Ensure a single PHI with induction accumulation only
   */
  auto singlePHI = sccInfo->getSinglePHI();
  if (!singlePHI) return setHasIV(false);
  auto loopOfPHI = LIS.getLoop(sccInfo->getSinglePHI());
  for (auto i = 0; i < singlePHI->getNumIncomingValues(); ++i) {
    auto incomingBB = singlePHI->getIncomingBlock(i);
    auto loopOfIncoming = LIS.getLoop(incomingBB);
    if (  false
          || (loopOfIncoming == nullptr) 
          || (loopOfIncoming != loopOfPHI)
      ){
      continue;
    }
    if (!isDerivedPHIOrAccumulator(singlePHI->getIncomingValue(i), scc)) return setHasIV(false);
  }

  for (auto I : sccInfo->getAccumulators()) {
    auto scev = SE.getSCEV(I);
    if (scev->getSCEVType() != scAddRecExpr) {
      return setHasIV(false);
    }
  }

  // scc->printMinimal(errs() << "IS an IV:\n") << "\n";
  return setHasIV(true);
}

void SCCDAGAttrs::checkIfIVHasFixedBounds (SCC *scc, LoopsSummary &LIS) {
  auto fixedIVBounds = new FixedIVBounds();
  auto &IVBounds = *fixedIVBounds;
  auto notSimple = [&]() -> void {
    delete fixedIVBounds;
    return;
  };

  /*
   * Fetch the single PHI and single accumulator.
   */
  auto sccInfo = this->getSCCAttrs(scc);
  auto singlePHI = sccInfo->getSinglePHI();
  auto singleAccumulator = sccInfo->getSingleAccumulator();

  /*
   * IV is described by single PHI with a start and recurrence incoming value
   * The IV has one accumulator only
   */
  if (!singlePHI || !singleAccumulator) return notSimple();
  if (singlePHI->getNumIncomingValues() != 2) return notSimple();
  auto singleControlPair = sccInfo->getSingleInstructionThatControlLoopExit();
  if (singleControlPair == nullptr) return notSimple();

  auto accum = singleAccumulator;
  auto incomingStart = singlePHI->getIncomingValue(0);
  if (incomingStart == accum) incomingStart = singlePHI->getIncomingValue(1);
  IVBounds.start = incomingStart;

  /*
   * The IV recurrence is integer, by +-1 
   */
  auto stepValue = accum->getOperand(0);
  if (stepValue == singlePHI) stepValue = accum->getOperand(1);
  if (!isa<ConstantInt>(stepValue)) return notSimple();
  IVBounds.step = (ConstantInt*)stepValue;
  auto stepSize = IVBounds.step->getValue();
  if (stepSize != 1 && stepSize != -1) return notSimple();

  auto cmp = cast<CmpInst>(singleControlPair->first);
  auto cmpLHS = cmp->getOperand(0);
  unsigned cmpToInd = cmpLHS == singlePHI || cmpLHS == accum;
  IVBounds.cmpIVTo = cmp->getOperand(cmpToInd);
  IVBounds.isCmpOnAccum = cmp->getOperand((cmpToInd + 1) % 2) == accum;
  IVBounds.isCmpIVLHS = cmpToInd;

  /*
   * The CmpInst compare value is constant, or a chain (length 0 or more)
   * of independent nodes in the SCC that ends in a loop external value
   */
  if (!isa<ConstantData>(IVBounds.cmpIVTo)) {
    if (!isa<Instruction>(IVBounds.cmpIVTo)) return notSimple();
    IVBounds.cmpToDerivation.push_back((Instruction*)IVBounds.cmpIVTo);
    if (!collectDerivationChain(IVBounds.cmpToDerivation, scc)) return notSimple();

    auto chainEnd = IVBounds.cmpToDerivation.back();
    if (isDerivedWithinSCC(chainEnd, scc)) return notSimple();
  }

  /*
   * The last value before the end value reached by the IV can be determined
   */
  if (!isIVUpperBoundSimple(scc, IVBounds, LIS)) return notSimple();

  sccIVBounds[scc] = fixedIVBounds;

  return ;
}

bool SCCDAGAttrs::isIVUpperBoundSimple (SCC *scc, FixedIVBounds &IVBounds, LoopsSummary &LIS) {
  auto sccInfo = this->getSCCAttrs(scc);
  auto singleControlPair = sccInfo->getSingleInstructionThatControlLoopExit();
  assert(singleControlPair != nullptr);
  auto cmp = cast<CmpInst>(singleControlPair->first);
  auto br = cast<BranchInst>(singleControlPair->second);

  /*
   * Branch statement has two successors, one in the loop body, one outside the loop
   */
  auto loop = LIS.getLoop(br);
  auto brLHSInLoop = loop->isBasicBlockWithin(br->getSuccessor(0));
  auto brRHSInLoop = loop->isBasicBlockWithin(br->getSuccessor(1));
  if (!(brLHSInLoop ^ brRHSInLoop)) return false;

  bool exitOnCmp = !brLHSInLoop;
  auto signedPred = cmp->isUnsigned() ? cmp->getSignedPredicate() : cmp->getPredicate();
  signedPred = IVBounds.isCmpIVLHS ? signedPred : ICmpInst::getSwappedPredicate(signedPred);
  int stepSize = IVBounds.step->getValue().getSExtValue();

  auto cmpPredAbort = [&]() -> void {
    errs() << "SCCDAGAttrs:   Error: comparison and branch of top level IV misunderstood\n";
    abort();
  };

  if (!exitOnCmp) { 
    if (stepSize == 1) {
      switch (signedPred) {
        case CmpInst::Predicate::ICMP_SLE:
          IVBounds.endOffset = 1;
        case CmpInst::Predicate::ICMP_NE:
        case CmpInst::Predicate::ICMP_SLT:
          break;
        default:
          cmpPredAbort();
      }
    } else {
      switch (signedPred) {
        case CmpInst::Predicate::ICMP_SGE:
          IVBounds.endOffset = -1;
        case CmpInst::Predicate::ICMP_NE:
        case CmpInst::Predicate::ICMP_SGT:
          break;
        default:
          cmpPredAbort();
      }
    }
  } else {
    if (stepSize == 1) {
      switch (signedPred) {
        case CmpInst::Predicate::ICMP_SGT:
          IVBounds.endOffset = 1;
        case CmpInst::Predicate::ICMP_SGE:
        case CmpInst::Predicate::ICMP_EQ:
          break;
        default:
          cmpPredAbort();
      }
    } else {
      switch (signedPred) {
        case CmpInst::Predicate::ICMP_SLT:
          IVBounds.endOffset = -1;
        case CmpInst::Predicate::ICMP_SLE:
        case CmpInst::Predicate::ICMP_EQ:
          break;
        default:
          cmpPredAbort();
      }
    }
  }

  IVBounds.endOffset -= stepSize * IVBounds.isCmpOnAccum;
  return true;
}

void SCCDAGAttrs::checkIfClonable (SCC *scc, ScalarEvolution &SE) {

  /*
   * Check the simple cases.
   */
  if ( false
       || isClonableByInductionVars(scc)
       || isClonableBySyntacticSugarInstrs(scc)
       || isClonableByCmpBrInstrs(scc)
      ) {
    this->getSCCAttrs(scc)->setSCCToBeClonable();
    clonableSCCs.insert(scc);
    return ;
  }

  return ;
}

bool SCCDAGAttrs::isClonableByInductionVars (SCC *scc) const {

  /*
   * FIXME: This check should not exist; instead, SCC where cloning
   * is trivial should be separated out by the parallelization scheme
   */
  if (this->sccdag->fetchNode(scc)->numOutgoingEdges() == 0) return false;

  /*
   * Fetch the SCC metadata.
   */
  auto sccInfo = this->getSCCAttrs(scc);

  return sccInfo->isInductionVariableSCC();
}

bool SCCDAGAttrs::isClonableBySyntacticSugarInstrs (SCC *scc) const {

  /*
   * FIXME: This check should not exist; instead, SCC where cloning
   * is trivial should be separated out by the parallelization scheme
   */
  if (this->sccdag->fetchNode(scc)->numOutgoingEdges() == 0) return false;

  if (scc->numInternalNodes() > 1) return false;
  auto I = scc->begin_internal_node_map()->first;
  if (isa<PHINode>(I) || isa<GetElementPtrInst>(I) || isa<CastInst>(I)) {
    return true;
  }
  return false;
}

bool SCCDAGAttrs::isClonableByCmpBrInstrs (SCC *scc) const {
  for (auto iNodePair : scc->internalNodePairs()) {
    auto V = iNodePair.first;
    if (auto inst = dyn_cast<Instruction>(V)){
      if (isa<CmpInst>(inst) || inst->isTerminator()) {
        continue;
      }
    }
    return false;
  }
  return true;
}

/*
 * NOTE: Derivation within an SCC requires inclusion in the SCC object
 * and dependency to a value in the strongly connected component, in the cycle
 * TODO: Derivation should only consider data dependency cycles, not control
 */
bool SCCDAGAttrs::isDerivedWithinSCC (Value *val, SCC *scc) const {
  if (!scc->isInternal(val)) {
    return false;
  }

  auto &sccInfo = sccToInfo.find(scc)->second;
  auto isStrongly = sccInfo->stronglyConnectedDataValues.find(val)
    != sccInfo->stronglyConnectedDataValues.end();
  auto isWeakly = sccInfo->weaklyConnectedDataValues.find(val)
    != sccInfo->weaklyConnectedDataValues.end();
  if (isStrongly) return true;
  if (isWeakly) {
    return false;
  }

  // Traversing both outgoing OR incoming edges leads back to the node
  // if it is in the SCC; otherwise, it is just a merged in node
  auto startNode = scc->fetchNode(val);
  std::queue<DGNode<Value> *> toOutgoing;
  std::set<DGNode<Value> *> seen;
  toOutgoing.push(startNode);
  bool inCycle = false;
  while (!toOutgoing.empty()) {
    auto node = toOutgoing.front();
    toOutgoing.pop();

    for (auto edge : node->getOutgoingEdges()) {
      if (edge->isControlDependence()) continue;
      auto inNode = edge->getIncomingNode();
      if (scc->isExternal(inNode->getT())) continue;
      if (inNode == startNode) inCycle = true;
      if (seen.find(inNode) == seen.end()) {
        // inNode->getT()->print(errs() << "GOING TO: "); errs() << "\n";
        seen.insert(inNode);
        toOutgoing.push(inNode);
      }
    }
    if (inCycle) break;
  }
  // errs() << "IN CYCLE with outgoings: " << inCycle << "\n";

  if (!inCycle) {
    sccInfo->weaklyConnectedDataValues.insert(val);
    // val->print(errs() << "WEAKLY CONNECTED: "); errs() << "\n";
    return false;
  }

  inCycle = false;
  seen.clear();
  std::queue<DGNode<Value> *> toIncoming;
  toIncoming.push(startNode);
  while (!toIncoming.empty()) {
    auto node = toIncoming.front();
    toIncoming.pop();

    for (auto edge : node->getIncomingEdges()) {
      if (edge->isControlDependence()) continue;
      auto outNode = edge->getOutgoingNode();
      if (scc->isExternal(outNode->getT())) continue;
      if (outNode == startNode) inCycle = true;
      if (seen.find(outNode) == seen.end()) {
        // outNode->getT()->print(errs() << "GOING TO: "); errs() << "\n";
        seen.insert(outNode);
        toIncoming.push(outNode);
      }
    }
    if (inCycle) break;
  }
  // errs() << "IN CYCLE with incomings: " << inCycle << "\n";

  if (!inCycle) {
    sccInfo->weaklyConnectedDataValues.insert(val);
    // val->print(errs() << "WEAKLY CONNECTED: "); errs() << "\n";
    return false;
  }

  sccInfo->stronglyConnectedDataValues.insert(val);
  return true;
}

bool SCCDAGAttrs::isDerivedPHIOrAccumulator (Value *val, SCC *scc) const {
  auto derived = val;
  if (auto cast = dyn_cast<CastInst>(val)) {
    derived = cast->getOperand(0);
  }

  auto &sccInfo = this->sccToInfo.find(scc)->second;
  bool isInternalPHI = isa<PHINode>(derived) && sccInfo->doesItContainThisPHI(cast<PHINode>(derived));
  bool isInternalAccum = isa<Instruction>(derived) && sccInfo->doesItContainThisInstructionAsAccumulator(cast<Instruction>(derived));

  return isDerivedWithinSCC(derived, scc) && (isInternalPHI || isInternalAccum);
}

bool SCCDAGAttrs::collectDerivationChain (std::vector<Instruction *> &chain, SCC *scc) {
  Instruction *deriving = chain[0];
  if (!scc->isInternal(deriving)) return true;

  std::set<Instruction *> valuesSeen;
  chain.pop_back();
  while (scc->isInternal(deriving)) {
    chain.push_back(deriving);
    if (valuesSeen.find(deriving) != valuesSeen.end()) {
      return false;
    }
    valuesSeen.insert(deriving);

    auto node = scc->fetchNode(deriving);
    std::set<Value *> incomingDataDeps;
    for (auto edge : node->getIncomingEdges()) {
      if (edge->isControlDependence()) {
        continue;
      }
      auto instructionSrc = edge->getOutgoingT();
      incomingDataDeps.insert(instructionSrc);
    }
    incomingDataDeps.erase(deriving);

    /*
     * Continue down the dependency graph only if it is a linear chain
     */
    if (incomingDataDeps.size() == 0) break;
    if (incomingDataDeps.size() != 1) {
      return false;
    }
    auto V = *incomingDataDeps.begin();
    if (!isa<Instruction>(V)) {
      return false;
    }
    deriving = (Instruction*)V;
  }

  return true;
}

bool SCCDAGAttrs::isALoopCarriedDependence (SCC *scc, DGEdge<Value> *dependence) {

  /*
   * Fetch the set of loop-carried data dependences of a SCC.
   */
  if (this->interIterDeps.find(scc) == this->interIterDeps.end()){
    return false;
  }
  auto lcDeps = this->interIterDeps[scc];

  /*
   * Check whether the dependence is inside lcDeps.
   */
  return lcDeps.find(dependence) != lcDeps.end();
}
      
void SCCDAGAttrs::iterateOverLoopCarriedDataDependences (
  SCC *scc, 
  std::function<bool (DGEdge<Value> *dependence)> func
  ){

  /*
   * Iterate over internal edges of the SCC.
   */
  for (auto valuePair : scc->internalNodePairs()) {
    for (auto edge : valuePair.second->getIncomingEdges()) {

      /*
       * Check if the current edge is a loop-carried data dependence.
       */
      if (!this->isALoopCarriedDependence(scc, edge)){
        continue ;
      }

      /*
       * The current edge is a loop-carried data dependence.
       */
      auto result = func(edge);

      /*
       * Check if the caller wants us to stop iterating.
       */
      if (result){
        return ;
      }
    }
  }

  return ;
}

SCCDAG * SCCDAGAttrs::getSCCDAG (void){
  return this->sccdag;
}

bool SCCAttrs::mustExecuteSequentially (void) const {
  return this->getType() == SCCAttrs::SCCType::SEQUENTIAL;
}

bool SCCAttrs::canExecuteReducibly (void) const {
  return this->getType() == SCCAttrs::SCCType::REDUCIBLE;
}

bool SCCAttrs::canExecuteIndependently (void) const {
  return this->getType() == SCCAttrs::SCCType::INDEPENDENT;
}

bool SCCAttrs::canBeCloned (void) const {
  return this->isClonable;
}

bool SCCAttrs::isInductionVariableSCC (void) const {
  return this->hasIV;
}
