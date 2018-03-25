#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/SCCIterator.h"
#include <set>
#include <unordered_map>

#include "../include/DGGraphTraits.hpp"
#include "../include/SCCDG.hpp"

llvm::SCCDG::SCCDG() {}

llvm::SCCDG::~SCCDG() {
  for (auto *edge : allEdges)
    if (edge) delete edge;
  for (auto *node : allNodes)
    if (node) delete node;
}


SCCDG *llvm::SCCDG::createSCCGraphFrom(PDG *pdg) {
  auto sccDG = new SCCDG();

  auto components = pdg->collectConnectedComponents();

  for (auto componentNodes : components) {
    auto componentPDG = new PDG();
    pdg->extractNodesFromSelfInto(*cast<DG<Instruction>>(componentPDG), *componentNodes, *componentNodes->begin(), false);
    delete componentNodes;

    for (auto pdgI = scc_begin(componentPDG); pdgI != scc_end(componentPDG); ++pdgI)
    {
      std::vector<DGNode<Instruction> *> nodes;
      for (auto node : *pdgI) nodes.push_back(node);

      auto scc = new SCC(nodes);
      sccDG->createNodeFrom(scc, /*inclusion=*/ true);
    }
  }
  /*
   * Maintain association of each internal node to its SCC
   */
  auto nodeSCCMap = unordered_map<DGNode<Instruction> *, SCC *>();
  for (auto sccNode : make_range(sccDG->begin_nodes(), sccDG->end_nodes()))
  {
    auto scc = sccNode->getT();
    for (auto nodePair : scc->internalNodePairs())
    {
      nodeSCCMap[nodePair.second] = scc;
    }
  }

  /*
   * Helper function to find or create an SCC from a node
   */
  auto fetchOrCreateSCC = [&nodeSCCMap, sccDG](DGNode<Instruction> *node) -> SCC* {
    auto sccI = nodeSCCMap.find(node);
    if (sccI == nodeSCCMap.end()) {
      vector<DGNode<Instruction> *> sccNodes = { node };
      auto scc = new SCC(sccNodes);
      sccDG->createNodeFrom(scc, /*inclusion=*/ false);
      nodeSCCMap[node] = scc;
      return scc;
    }
    return sccI->second;
  };

  /*
   * Add internal/external edges between SCCs
   */
  for (auto pdgEI = pdg->begin_edges(); pdgEI != pdg->end_edges(); ++pdgEI) {
    auto edge = *pdgEI;
    auto nodePair = edge->getNodePair();
    auto fromSCC = fetchOrCreateSCC(nodePair.first);
    auto toSCC = fetchOrCreateSCC(nodePair.second);

    /*
     * If the edge points to external SCCs or is contained in a single SCC, ignore 
     */
    if ((sccDG->isExternal(fromSCC) && sccDG->isExternal(toSCC)) || fromSCC == toSCC) continue;

    /*
     * Create edge between SCCs with same properties as the edge between instructions within the SCCs
     */
    auto sccEdge = sccDG->createEdgeFromTo(fromSCC, toSCC);
    sccEdge->setMemMustRaw(edge->isMemoryDependence(), edge->isMustDependence(), edge->isRAWDependence());
    sccEdge->addSubEdge(edge);
  }

  return sccDG;
}

SCCDG *llvm::SCCDG::extractSCCIntoGraph(DGNode<SCC> *sccNode)
{
  SCCDG *sccDG = new SCCDG();
  std::vector<DGNode<SCC> *> sccNodes = { sccNode };
  extractNodesFromSelfInto(*cast<DG<SCC>>(sccDG), sccNodes, sccNode, /*removeFromSelf=*/ true);
  return sccDG;
}

bool llvm::SCCDG::isPipeline()
{
  /*
   * Traverse from arbitrary SCC to the top, if one exists
   */
  auto topOfPipeline = *begin_nodes();
  while (topOfPipeline->numIncomingEdges() != 0)
  {
    if (topOfPipeline->numIncomingEdges() > 1) return false;
    topOfPipeline = *topOfPipeline->begin_incoming_nodes();
  }

  /*
   * Traverse from top SCC to the bottom, if one exists
   */
  unsigned visitedNodes = 1;
  while (topOfPipeline->numOutgoingEdges() != 0)
  {
    if (topOfPipeline->numOutgoingEdges() > 1) return false;
    topOfPipeline = *topOfPipeline->begin_outgoing_nodes();
    ++visitedNodes;
  }
  return visitedNodes == numNodes();
}