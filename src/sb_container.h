/*
 * Copyright (C) 2012 Carl Leonardsson
 * 
 * This file is part of Memorax.
 *
 * Memorax is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Memorax is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __SB_CONTAINER__
#define __SB_CONTAINER__

#include "log.h"
#include "constraint_container.h"
#include "sb_constraint.h"
#include "ticket_queue.h"

/* A constraint container meant for SbConstraints. Uses
 * SbConstraint::entailment_compare for comparison and entailment upon
 * insertion.
 */
class SbContainer : public ConstraintContainer{
public:
  SbContainer();
  virtual ~SbContainer();

  virtual void insert_root(Constraint *r);
  virtual void insert(Constraint *p, const Machine::PTransition *t, Constraint *c);
  virtual Constraint *pop();
  virtual int Q_size() const { return q_size; };
  virtual int F_size() const { return f_size; };
  virtual Trace *clear_and_get_trace(Constraint *c);
  virtual void clear();
private:
  /* Keeps a SbConstraint and some extra information about it. */
  struct CWrapper{
    CWrapper(SbConstraint *sbc, CWrapper *parent = 0, const Machine::PTransition *pt = 0)
      : sbc(sbc), parent(parent), p_transition(pt), valid(true) {};
    ~CWrapper(){
      if(sbc){
        delete sbc;
      }
    };
    /* The constraint itself */
    SbConstraint *sbc;
    /* The wrapper around the parent of sbc.
     * Null if sbc is a root constraint. */
    CWrapper *parent;
    /* The transition by which parent transitioned into sbc.
     * Null if parent is null.
     *
     * The transition is not owned by the CWrapper. The pointer points
     * to some transition which ownership lies outside of the
     * SbContainer. (Most likely in
     * SbConstraint::Common::all_transitions.)
     */
    const Machine::PTransition *p_transition;
    /* A vector containing pointers to all CWrappers cw such that
     * cw.parent == this 
     * 
     * Only kept up-to-date if use_genealogy == true. */
    std::vector<CWrapper*> children;
    /* False iff this constraint has been subsumed, and should not be
     * considered part of Q. */
    bool valid;
    /* The ticket of this constraint in Q */
    long Q_ticket;
  };

  /* F[pcs][chr] maps to the set of all constraints in F that have
   * program counters pcs and channel characterization chr.
   *
   * The sets are represented as distinct, unordered vectors.
   */
  std::map<std::vector<int>,std::map<std::vector<SbConstraint::MsgCharacterization> , std::vector<CWrapper*> > > F;
  /* Stores pointers to the wrappers that have been invalidated. They
   * should not be considered in the analysis, but should be
   * deallocated upon destruction of the container. */
  std::vector<CWrapper*> invalid_from_F;
  
  /* For each constraint c in F, ptr_to_F[c] is a pointer to its
   * CWrapper in F.
   */
  std::map<SbConstraint*,CWrapper*> ptr_to_F;

  /* Caches (sbc,cw) for the last constraint sbc that was popped, and
   * cw == ptr_to_F[sbc].
   */
  std::pair<SbConstraint*,CWrapper*> last_popped;

  /* Returns ptr_to_F[sbc]. Uses the cache last_popped if possible.
   */
  CWrapper *get_cwrapper(SbConstraint *sbc) const{
    if(last_popped.first == sbc){
      return last_popped.second;
    }else{
      return ptr_to_F.at(sbc);
    }
  };

  /* ChannelPrioTicketQueue is the class for Q.
   *
   * It uses multiple TicketQueues to implement a queue where priority
   * is given to shorter channels. */
  class ChannelPrioTicketQueue{
  public:
    long push(CWrapper *cw){
      if(int(queues.size()) <= cw->sbc->get_channel_length()){
        queues.resize(cw->sbc->get_channel_length()+1);
      }
      return queues[cw->sbc->get_channel_length()].push(cw);
    };
    CWrapper *pop(){
      /* Give priority to shorter channels. */
      for(unsigned i = 1; i < queues.size(); ++i){
        if(queues[i].size() > 0){
          return queues[i].pop();
        }
      }
      return 0;
    };
    bool in_queue(long tck,int chan_len){
      return queues[chan_len].in_queue(tck);
    };
    void clear(){
      for(unsigned i = 0; i < queues.size(); ++i){
        queues[i].clear();
      }
      queues.clear();
    };
  private:
    /* queues[i] contains the constraints whose channel has length i. */
    std::vector<TicketQueue<CWrapper*> > queues;
  };

  /* The queue.
   * 
   * Pointers are to objects shared with F. Q does not have
   * ownership. */
  ChannelPrioTicketQueue Q;

  bool insert(CWrapper *cw);

  /* The number of valid constraints in F */
  int f_size;
  /* The number of valid constraints in Q */
  int q_size;

  /* Set cw->valid = false, remove it from Q and F.
   *
   * If use_genealogy, recursively do the same for all children of cw.
   */
  void invalidate(CWrapper *cw, std::vector<CWrapper*> *Fv = 0);

#ifndef NDEBUG
  struct stats_t{
    stats_t() 
      : longest_channel(0),
        longest_comparable_array(0),
        invalidate_count(0) {};
    int longest_channel;
    int longest_comparable_array;
    int invalidate_count;
    void print(){
      Log::debug << " ==========================\n"
                 << " = SbContainer statistics =\n"
                 << " ==========================\n"
                 << " longest channel: " << longest_channel << "\n"
                 << " longest comparable array: " << longest_comparable_array << "\n"
                 << " invalidated: " << invalidate_count << "\n";
    };
  };

  stats_t stats;
#endif
  void update_longest_channel(int chanlen){
#ifndef NDEBUG
    stats.longest_channel = std::max(chanlen,stats.longest_channel);
#endif
  };
  void update_longest_comparable_array(const std::vector<CWrapper*> &v){
#ifndef NDEBUG
    stats.longest_comparable_array = std::max<int>(stats.longest_comparable_array,
                                                   v.size());
#endif
  };
  void inc_invalidate_count(){
#ifndef NDEBUG
    ++stats.invalidate_count;
#endif
  };

  static const bool print_every_state_on_clear;
  static const bool use_genealogy;
};

#endif
