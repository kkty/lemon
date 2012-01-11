/* -*- mode: C++; indent-tabs-mode: nil; -*-
 *
 * This file is a part of LEMON, a generic C++ optimization library.
 *
 * Copyright (C) 2003-2011
 * Egervary Jeno Kombinatorikus Optimalizalasi Kutatocsoport
 * (Egervary Research Group on Combinatorial Optimization, EGRES).
 *
 * Permission to use, modify and distribute this software is granted
 * provided that this copyright notice appears in all copies. For
 * precise terms see the accompanying LICENSE file.
 *
 * This software is provided "AS IS" with no warranty of any kind,
 * express or implied, and with no claim as to its suitability for any
 * purpose.
 *
 */

#include <iostream>
#include <fstream>
#include <limits>

#include <lemon/list_graph.h>
#include <lemon/lgf_reader.h>

#include <lemon/network_simplex.h>

#include <lemon/concepts/digraph.h>
#include <lemon/concept_check.h>

#include "test_tools.h"

using namespace lemon;

char test_lgf[] =
  "@nodes\n"
  "label  sup1 sup2 sup3 sup4 sup5 sup6\n"
  "    1    20   27    0   30   20   30\n"
  "    2    -4    0    0    0   -8   -3\n"
  "    3     0    0    0    0    0    0\n"
  "    4     0    0    0    0    0    0\n"
  "    5     9    0    0    0    6   11\n"
  "    6    -6    0    0    0   -5   -6\n"
  "    7     0    0    0    0    0    0\n"
  "    8     0    0    0    0    0    3\n"
  "    9     3    0    0    0    0    0\n"
  "   10    -2    0    0    0   -7   -2\n"
  "   11     0    0    0    0  -10    0\n"
  "   12   -20  -27    0  -30  -30  -20\n"
  "\n"
  "@arcs\n"
  "       cost  cap low1 low2 low3\n"
  " 1  2    70   11    0    8    8\n"
  " 1  3   150    3    0    1    0\n"
  " 1  4    80   15    0    2    2\n"
  " 2  8    80   12    0    0    0\n"
  " 3  5   140    5    0    3    1\n"
  " 4  6    60   10    0    1    0\n"
  " 4  7    80    2    0    0    0\n"
  " 4  8   110    3    0    0    0\n"
  " 5  7    60   14    0    0    0\n"
  " 5 11   120   12    0    0    0\n"
  " 6  3     0    3    0    0    0\n"
  " 6  9   140    4    0    0    0\n"
  " 6 10    90    8    0    0    0\n"
  " 7  1    30    5    0    0   -5\n"
  " 8 12    60   16    0    4    3\n"
  " 9 12    50    6    0    0    0\n"
  "10 12    70   13    0    5    2\n"
  "10  2   100    7    0    0    0\n"
  "10  7    60   10    0    0   -3\n"
  "11 10    20   14    0    6  -20\n"
  "12 11    30   10    0    0  -10\n"
  "\n"
  "@attributes\n"
  "source 1\n"
  "target 12\n";


enum SupplyType {
  EQ,
  GEQ,
  LEQ
};

// Check the interface of an MCF algorithm
template <typename GR, typename Value, typename Cost>
class McfClassConcept
{
public:

  template <typename MCF>
  struct Constraints {
    void constraints() {
      checkConcept<concepts::Digraph, GR>();

      const Constraints& me = *this;

      MCF mcf(me.g);
      const MCF& const_mcf = mcf;

      b = mcf.reset()
             .lowerMap(me.lower)
             .upperMap(me.upper)
             .costMap(me.cost)
             .supplyMap(me.sup)
             .stSupply(me.n, me.n, me.k)
             .run();

      c = const_mcf.totalCost();
      x = const_mcf.template totalCost<double>();
      v = const_mcf.flow(me.a);
      c = const_mcf.potential(me.n);
      const_mcf.flowMap(fm);
      const_mcf.potentialMap(pm);
    }

    typedef typename GR::Node Node;
    typedef typename GR::Arc Arc;
    typedef concepts::ReadMap<Node, Value> NM;
    typedef concepts::ReadMap<Arc, Value> VAM;
    typedef concepts::ReadMap<Arc, Cost> CAM;
    typedef concepts::WriteMap<Arc, Value> FlowMap;
    typedef concepts::WriteMap<Node, Cost> PotMap;

    GR g;
    VAM lower;
    VAM upper;
    CAM cost;
    NM sup;
    Node n;
    Arc a;
    Value k;

    FlowMap fm;
    PotMap pm;
    bool b;
    double x;
    typename MCF::Value v;
    typename MCF::Cost c;
  };

};


// Check the feasibility of the given flow (primal soluiton)
template < typename GR, typename LM, typename UM,
           typename SM, typename FM >
bool checkFlow( const GR& gr, const LM& lower, const UM& upper,
                const SM& supply, const FM& flow,
                SupplyType type = EQ )
{
  TEMPLATE_DIGRAPH_TYPEDEFS(GR);

  for (ArcIt e(gr); e != INVALID; ++e) {
    if (flow[e] < lower[e] || flow[e] > upper[e]) return false;
  }

  for (NodeIt n(gr); n != INVALID; ++n) {
    typename SM::Value sum = 0;
    for (OutArcIt e(gr, n); e != INVALID; ++e)
      sum += flow[e];
    for (InArcIt e(gr, n); e != INVALID; ++e)
      sum -= flow[e];
    bool b = (type ==  EQ && sum == supply[n]) ||
             (type == GEQ && sum >= supply[n]) ||
             (type == LEQ && sum <= supply[n]);
    if (!b) return false;
  }

  return true;
}

// Check the feasibility of the given potentials (dual soluiton)
// using the "Complementary Slackness" optimality condition
template < typename GR, typename LM, typename UM,
           typename CM, typename SM, typename FM, typename PM >
bool checkPotential( const GR& gr, const LM& lower, const UM& upper,
                     const CM& cost, const SM& supply, const FM& flow,
                     const PM& pi, SupplyType type )
{
  TEMPLATE_DIGRAPH_TYPEDEFS(GR);

  bool opt = true;
  for (ArcIt e(gr); opt && e != INVALID; ++e) {
    typename CM::Value red_cost =
      cost[e] + pi[gr.source(e)] - pi[gr.target(e)];
    opt = red_cost == 0 ||
          (red_cost > 0 && flow[e] == lower[e]) ||
          (red_cost < 0 && flow[e] == upper[e]);
  }

  for (NodeIt n(gr); opt && n != INVALID; ++n) {
    typename SM::Value sum = 0;
    for (OutArcIt e(gr, n); e != INVALID; ++e)
      sum += flow[e];
    for (InArcIt e(gr, n); e != INVALID; ++e)
      sum -= flow[e];
    if (type != LEQ) {
      opt = (pi[n] <= 0) && (sum == supply[n] || pi[n] == 0);
    } else {
      opt = (pi[n] >= 0) && (sum == supply[n] || pi[n] == 0);
    }
  }

  return opt;
}

// Check whether the dual cost is equal to the primal cost
template < typename GR, typename LM, typename UM,
           typename CM, typename SM, typename PM >
bool checkDualCost( const GR& gr, const LM& lower, const UM& upper,
                    const CM& cost, const SM& supply, const PM& pi,
                    typename CM::Value total )
{
  TEMPLATE_DIGRAPH_TYPEDEFS(GR);

  typename CM::Value dual_cost = 0;
  SM red_supply(gr);
  for (NodeIt n(gr); n != INVALID; ++n) {
    red_supply[n] = supply[n];
  }
  for (ArcIt a(gr); a != INVALID; ++a) {
    if (lower[a] != 0) {
      dual_cost += lower[a] * cost[a];
      red_supply[gr.source(a)] -= lower[a];
      red_supply[gr.target(a)] += lower[a];
    }
  }

  for (NodeIt n(gr); n != INVALID; ++n) {
    dual_cost -= red_supply[n] * pi[n];
  }
  for (ArcIt a(gr); a != INVALID; ++a) {
    typename CM::Value red_cost =
      cost[a] + pi[gr.source(a)] - pi[gr.target(a)];
    dual_cost -= (upper[a] - lower[a]) * std::max(-red_cost, 0);
  }

  return dual_cost == total;
}

// Run a minimum cost flow algorithm and check the results
template < typename MCF, typename GR,
           typename LM, typename UM,
           typename CM, typename SM,
           typename PT >
void checkMcf( const MCF& mcf, PT mcf_result,
               const GR& gr, const LM& lower, const UM& upper,
               const CM& cost, const SM& supply,
               PT result, bool optimal, typename CM::Value total,
               const std::string &test_id = "",
               SupplyType type = EQ )
{
  check(mcf_result == result, "Wrong result " + test_id);
  if (optimal) {
    typename GR::template ArcMap<typename SM::Value> flow(gr);
    typename GR::template NodeMap<typename CM::Value> pi(gr);
    mcf.flowMap(flow);
    mcf.potentialMap(pi);
    check(checkFlow(gr, lower, upper, supply, flow, type),
          "The flow is not feasible " + test_id);
    check(mcf.totalCost() == total, "The flow is not optimal " + test_id);
    check(checkPotential(gr, lower, upper, cost, supply, flow, pi, type),
          "Wrong potentials " + test_id);
    check(checkDualCost(gr, lower, upper, cost, supply, pi, total),
          "Wrong dual cost " + test_id);
  }
}

int main()
{
  // Check the interfaces
  {
    typedef concepts::Digraph GR;
    checkConcept< McfClassConcept<GR, int, int>,
                  NetworkSimplex<GR> >();
    checkConcept< McfClassConcept<GR, double, double>,
                  NetworkSimplex<GR, double> >();
    checkConcept< McfClassConcept<GR, int, double>,
                  NetworkSimplex<GR, int, double> >();
  }

  // Run various MCF tests
  typedef ListDigraph Digraph;
  DIGRAPH_TYPEDEFS(ListDigraph);

  // Read the test digraph
  Digraph gr;
  Digraph::ArcMap<int> c(gr), l1(gr), l2(gr), l3(gr), u(gr);
  Digraph::NodeMap<int> s1(gr), s2(gr), s3(gr), s4(gr), s5(gr), s6(gr);
  ConstMap<Arc, int> cc(1), cu(std::numeric_limits<int>::max());
  Node v, w;

  std::istringstream input(test_lgf);
  DigraphReader<Digraph>(gr, input)
    .arcMap("cost", c)
    .arcMap("cap", u)
    .arcMap("low1", l1)
    .arcMap("low2", l2)
    .arcMap("low3", l3)
    .nodeMap("sup1", s1)
    .nodeMap("sup2", s2)
    .nodeMap("sup3", s3)
    .nodeMap("sup4", s4)
    .nodeMap("sup5", s5)
    .nodeMap("sup6", s6)
    .node("source", v)
    .node("target", w)
    .run();

  // Build test digraphs with negative costs
  Digraph neg_gr;
  Node n1 = neg_gr.addNode();
  Node n2 = neg_gr.addNode();
  Node n3 = neg_gr.addNode();
  Node n4 = neg_gr.addNode();
  Node n5 = neg_gr.addNode();
  Node n6 = neg_gr.addNode();
  Node n7 = neg_gr.addNode();

  Arc a1 = neg_gr.addArc(n1, n2);
  Arc a2 = neg_gr.addArc(n1, n3);
  Arc a3 = neg_gr.addArc(n2, n4);
  Arc a4 = neg_gr.addArc(n3, n4);
  Arc a5 = neg_gr.addArc(n3, n2);
  Arc a6 = neg_gr.addArc(n5, n3);
  Arc a7 = neg_gr.addArc(n5, n6);
  Arc a8 = neg_gr.addArc(n6, n7);
  Arc a9 = neg_gr.addArc(n7, n5);

  Digraph::ArcMap<int> neg_c(neg_gr), neg_l1(neg_gr, 0), neg_l2(neg_gr, 0);
  ConstMap<Arc, int> neg_u1(std::numeric_limits<int>::max()), neg_u2(5000);
  Digraph::NodeMap<int> neg_s(neg_gr, 0);

  neg_l2[a7] =  1000;
  neg_l2[a8] = -1000;

  neg_s[n1] =  100;
  neg_s[n4] = -100;

  neg_c[a1] =  100;
  neg_c[a2] =   30;
  neg_c[a3] =   20;
  neg_c[a4] =   80;
  neg_c[a5] =   50;
  neg_c[a6] =   10;
  neg_c[a7] =   80;
  neg_c[a8] =   30;
  neg_c[a9] = -120;

  Digraph negs_gr;
  Digraph::NodeMap<int> negs_s(negs_gr);
  Digraph::ArcMap<int> negs_c(negs_gr);
  ConstMap<Arc, int> negs_l(0), negs_u(1000);
  n1 = negs_gr.addNode();
  n2 = negs_gr.addNode();
  negs_s[n1] = 100;
  negs_s[n2] = -300;
  negs_c[negs_gr.addArc(n1, n2)] = -1;


  // A. Test NetworkSimplex with the default pivot rule
  {
    NetworkSimplex<Digraph> mcf(gr);

    // Check the equality form
    mcf.upperMap(u).costMap(c);
    checkMcf(mcf, mcf.supplyMap(s1).run(),
             gr, l1, u, c, s1, mcf.OPTIMAL, true,   5240, "#A1");
    checkMcf(mcf, mcf.stSupply(v, w, 27).run(),
             gr, l1, u, c, s2, mcf.OPTIMAL, true,   7620, "#A2");
    mcf.lowerMap(l2);
    checkMcf(mcf, mcf.supplyMap(s1).run(),
             gr, l2, u, c, s1, mcf.OPTIMAL, true,   5970, "#A3");
    checkMcf(mcf, mcf.stSupply(v, w, 27).run(),
             gr, l2, u, c, s2, mcf.OPTIMAL, true,   8010, "#A4");
    mcf.reset();
    checkMcf(mcf, mcf.supplyMap(s1).run(),
             gr, l1, cu, cc, s1, mcf.OPTIMAL, true,   74, "#A5");
    checkMcf(mcf, mcf.lowerMap(l2).stSupply(v, w, 27).run(),
             gr, l2, cu, cc, s2, mcf.OPTIMAL, true,   94, "#A6");
    mcf.reset();
    checkMcf(mcf, mcf.run(),
             gr, l1, cu, cc, s3, mcf.OPTIMAL, true,    0, "#A7");
    checkMcf(mcf, mcf.lowerMap(l2).upperMap(u).run(),
             gr, l2, u, cc, s3, mcf.INFEASIBLE, false, 0, "#A8");
    mcf.reset().lowerMap(l3).upperMap(u).costMap(c).supplyMap(s4);
    checkMcf(mcf, mcf.run(),
             gr, l3, u, c, s4, mcf.OPTIMAL, true,   6360, "#A9");

    // Check the GEQ form
    mcf.reset().upperMap(u).costMap(c).supplyMap(s5);
    checkMcf(mcf, mcf.run(),
             gr, l1, u, c, s5, mcf.OPTIMAL, true,   3530, "#A10", GEQ);
    mcf.supplyType(mcf.GEQ);
    checkMcf(mcf, mcf.lowerMap(l2).run(),
             gr, l2, u, c, s5, mcf.OPTIMAL, true,   4540, "#A11", GEQ);
    mcf.supplyMap(s6);
    checkMcf(mcf, mcf.run(),
             gr, l2, u, c, s6, mcf.INFEASIBLE, false,  0, "#A12", GEQ);

    // Check the LEQ form
    mcf.reset().supplyType(mcf.LEQ);
    mcf.upperMap(u).costMap(c).supplyMap(s6);
    checkMcf(mcf, mcf.run(),
             gr, l1, u, c, s6, mcf.OPTIMAL, true,   5080, "#A13", LEQ);
    checkMcf(mcf, mcf.lowerMap(l2).run(),
             gr, l2, u, c, s6, mcf.OPTIMAL, true,   5930, "#A14", LEQ);
    mcf.supplyMap(s5);
    checkMcf(mcf, mcf.run(),
             gr, l2, u, c, s5, mcf.INFEASIBLE, false,  0, "#A15", LEQ);

    // Check negative costs
    NetworkSimplex<Digraph> neg_mcf(neg_gr);
    neg_mcf.lowerMap(neg_l1).costMap(neg_c).supplyMap(neg_s);
    checkMcf(neg_mcf, neg_mcf.run(), neg_gr, neg_l1, neg_u1,
      neg_c, neg_s, neg_mcf.UNBOUNDED, false,    0, "#A16");
    neg_mcf.upperMap(neg_u2);
    checkMcf(neg_mcf, neg_mcf.run(), neg_gr, neg_l1, neg_u2,
      neg_c, neg_s, neg_mcf.OPTIMAL, true,  -40000, "#A17");
    neg_mcf.reset().lowerMap(neg_l2).costMap(neg_c).supplyMap(neg_s);
    checkMcf(neg_mcf, neg_mcf.run(), neg_gr, neg_l2, neg_u1,
      neg_c, neg_s, neg_mcf.UNBOUNDED, false,    0, "#A18");

    NetworkSimplex<Digraph> negs_mcf(negs_gr);
    negs_mcf.costMap(negs_c).supplyMap(negs_s);
    checkMcf(negs_mcf, negs_mcf.run(), negs_gr, negs_l, negs_u,
      negs_c, negs_s, negs_mcf.OPTIMAL, true, -300, "#A19", GEQ);
  }

  // B. Test NetworkSimplex with each pivot rule
  {
    NetworkSimplex<Digraph> mcf(gr);
    mcf.supplyMap(s1).costMap(c).upperMap(u).lowerMap(l2);

    checkMcf(mcf, mcf.run(NetworkSimplex<Digraph>::FIRST_ELIGIBLE),
             gr, l2, u, c, s1, mcf.OPTIMAL, true,   5970, "#B1");
    checkMcf(mcf, mcf.run(NetworkSimplex<Digraph>::BEST_ELIGIBLE),
             gr, l2, u, c, s1, mcf.OPTIMAL, true,   5970, "#B2");
    checkMcf(mcf, mcf.run(NetworkSimplex<Digraph>::BLOCK_SEARCH),
             gr, l2, u, c, s1, mcf.OPTIMAL, true,   5970, "#B3");
    checkMcf(mcf, mcf.run(NetworkSimplex<Digraph>::CANDIDATE_LIST),
             gr, l2, u, c, s1, mcf.OPTIMAL, true,   5970, "#B4");
    checkMcf(mcf, mcf.run(NetworkSimplex<Digraph>::ALTERING_LIST),
             gr, l2, u, c, s1, mcf.OPTIMAL, true,   5970, "#B5");
  }

  return 0;
}
