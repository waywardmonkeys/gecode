/* -*- mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*- */
/*
 *  Main authors:
 *     Guido Tack <tack@gecode.org>
 *
 *  Copyright:
 *     Guido Tack, 2004
 *
 *  Last modified:
 *     $Date$ by $Author$
 *     $Revision$
 *
 *  This file is part of Gecode, the generic constraint
 *  development environment:
 *     http://www.gecode.org
 *
 *  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
 *  "Software"), to deal in the Software without restriction, including
 *  without limitation the rights to use, copy, modify, merge, publish,
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "gecode/set/distinct.hh"

/*
 * These propagators implement the scheme discussed in
 *
 * Andrew Sadler and Carment Gervet: Global Reasoning on Sets.
 * FORMUL'01 workshop in conjunction with CP 2001.
 *
 * Todo: make the propagators incremental.
 */

namespace Gecode { namespace Set { namespace Distinct {

  /*
   * "AtMostOneIntersection" propagator
   *
   */

  Actor*
  AtmostOne::copy(Space* home, bool share) {
    return new (home) AtmostOne(home,share,*this);
  }

  Support::Symbol
  AtmostOne::ati(void) {
    return Support::Symbol("Gecode::Set::Distinct::AtmostOne");
  }

  Reflection::ActorSpec
  AtmostOne::spec(const Space* home, Reflection::VarMap& m) const {
    return NaryPropagator<SetView, PC_SET_ANY>::spec(home, m, ati())
      << c;
  }

  ExecStatus
  AtmostOne::propagate(Space* home, ModEventDelta) {

    GECODE_AUTOARRAY(LubRanges<SetView>, lubs, x.size());
    for (int i = x.size(); i--; ) {
      lubs[i].init(x[i]);
    }
    Iter::Ranges::NaryUnion<LubRanges<SetView> > bigT(lubs, x.size());
    Iter::Ranges::Cache<Iter::Ranges::NaryUnion<LubRanges<SetView> > >
      bigTC(bigT);

    Iter::Ranges::ToValues<Iter::Ranges::Cache<Iter::Ranges::NaryUnion<LubRanges<SetView> > > >
      as(bigTC);

    while (as()) {
      int a = as.val(); ++as;

      // cardSa is the number of sets that contain a in the glb
      int cardSa = 0;
      for (int i=x.size(); i--;)
        if (x[i].contains(a))
          cardSa++;

      // bigTa is the union of all lubs that contain a
      GLBndSet bigTa(home);
      for (int i=x.size(); i--;) {
        if (!x[i].notContains(a)) {
          LubRanges<SetView> xilub(x[i]);
          bigTa.includeI(home, xilub);
        }
      }

      // maxa is the maximum number of sets that can contain a
      int maxa = (bigTa.size() - 1) / (c - 1);
      bigTa.dispose(home);

      // Conditional Rule A:
      // If more sets already contain a than allowed, fail.
      if (maxa < cardSa)
        return ES_FAILED;

      if (maxa == cardSa) {
        // Conditional Rule B:
        // All a used up. All other sets (those that don't have a in their
        // glb already) cannot contain a.
        for (int i=x.size(); i--;) {
          if (!x[i].contains(a)) {
            GECODE_ME_CHECK(x[i].exclude(home, a));
          }
        }
      } else {
        GECODE_AUTOARRAY(LubRanges<SetView>, lubs2, x.size());
        for (int i = x.size(); i--; ) {
          lubs2[i].init(x[i]);
        }
        Iter::Ranges::NaryUnion<LubRanges<SetView> > bigT2(lubs2, x.size());
        
        GECODE_AUTOARRAY(GlbRanges<SetView>, glbs, cardSa);
        int count = 0;
        for (int i=x.size(); i--; ) {
          if (x[i].contains(a)) {
            glbs[count].init(x[i]);
            count++;
          }
        }
        Iter::Ranges::NaryUnion<GlbRanges<SetView> > glbsa(glbs, cardSa);
        Iter::Ranges::Diff<Iter::Ranges::NaryUnion<LubRanges<SetView> >,
          Iter::Ranges::NaryUnion<GlbRanges<SetView> > > deltaA(bigT2, glbsa);
        Iter::Ranges::Cache<
        Iter::Ranges::Diff<Iter::Ranges::NaryUnion<LubRanges<SetView> >,
          Iter::Ranges::NaryUnion<GlbRanges<SetView> > > > deltaAC(deltaA);
        // deltaAC contains all elements that are not yet known to be
        // in a set together with a.
        // Formally: \cup_i lub(x_i) - \cup_i {glb(s_i) | a\in glb(s_i)}


        if (Iter::Ranges::size(deltaAC) == c - 1) {
          // Conditional Rule C:
          // If deltaA has exactly c-1 elements, all sets that are not yet
          // known to contain a cannot contain a if it is impossible that
          // they contain all of deltaA. Or the other way around:
          // If a is added to the glb of a set s, deltaA must be a subset of
          // s, because otherwise s would share at least one more element
          // with another set that has a in its lower bound.
          // Weird, eh?
          for (int i=x.size(); i--; ) {
            if (!x[i].contains(a) && !x[i].notContains(a)) {
              deltaAC.reset();
              LubRanges<SetView> xilub(x[i]);
              if (!Iter::Ranges::subset(deltaAC, xilub)) {
                GECODE_ME_CHECK(x[i].exclude(home, a));
              }
            }
          }
        }

      }

    }

    return ES_NOFIX;
  }

  void
  AtmostOne::post(Space* home, Reflection::VarMap& vars,
                  const Reflection::ActorSpec& spec) {
    spec.checkArity(2);
    ViewArray<SetView> s(home, vars, spec[0]);
    int c = spec[1]->toInt();
    (void) new (home) AtmostOne(home,s,c);
  }

}}

namespace {
  GECODE_REGISTER1(Set::Distinct::AtmostOne);
}

}

// STATISTICS: set-prop
