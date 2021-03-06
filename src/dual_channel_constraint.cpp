/*
 * Copyright (C) 2018 Tuan Phong Ngo
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


#include "dual_channel_constraint.h"

/******************************/
/* DualChannelConstraint::Msg */
/******************************/

std::string DualChannelConstraint::Msg::to_short_string(const Common &common) const{
  std::stringstream ss;
  ss << "<P" << wpid << ", ";
  if(nmls.size() == 1){
    ss << common.machine.pretty_string_nml.at(nmls[0]);
  }else{
    ss << "[";
    for(int i = 0; i < nmls.size(); ++i){
      if(i != 0) ss << ", ";
      ss << common.machine.pretty_string_nml.at(nmls[i]);
    }
    ss << "]";
  }
  ss << ", ";
  ss << store[0];
  ss << ">";
  return ss.str();
}

int DualChannelConstraint::Msg::compare(const Msg &msg) const{
  if(wpid < msg.wpid){
    return -1;
  }else if(wpid > msg.wpid){
    return 1;
  }

  if(nmls < msg.nmls){
    return -1;
  }else if(nmls > msg.nmls){
    return 1;
  }

  return store.compare(msg.store);
}

/*********************************/
/* DualChannelConstraint::Common */
/*********************************/

DualChannelConstraint::Common::Common(const Machine &m) : machine(m) {
  gvar_count = machine.gvars.size();
  max_lvar_count = 0;
  for(unsigned p = 0; p < machine.lvars.size(); ++p){
    max_lvar_count = std::max<int>(max_lvar_count, machine.lvars[p].size());
    reg_count.push_back(machine.regs[p].size());
  }
  mem_size = gvar_count + machine.automata.size()*max_lvar_count;

  /* Setup messages */
  {
    /* Insert a dummy message */
    messages.insert(MsgHdr(0,VecSet<Lang::NML>()));
    /* Add messages for all writes */
    for(unsigned p = 0; p < machine.automata.size(); ++p){
      const std::vector<Automaton::State> &states = machine.automata[p].get_states();
      for(unsigned i = 0; i < states.size(); ++i){
        for(auto it = states[i].fwd_transitions.begin(); it != states[i].fwd_transitions.end(); ++it){
          VecSet<VecSet<Lang::MemLoc<int> > > wss = (*it)->instruction.get_write_sets();
          for(auto wsit = wss.begin(); wsit != wss.end(); ++wsit){
            if(wsit->size() > 0){
              VecSet<Lang::NML> nmls;
              for(auto wit = wsit->begin(); wit != wsit->end(); ++wit){
                nmls.insert(Lang::NML(*wit,p));
                this->nmls.insert(Lang::NML(*wit,p));
              }
              messages.insert(MsgHdr(p,nmls));
            }
          }
        }
      }
    }
    
    for(unsigned p = 0; p < machine.automata.size(); ++p){
      const std::vector<Automaton::State> &states = machine.automata[p].get_states();
      for(unsigned i = 0; i < states.size(); ++i){
        for(auto it = states[i].fwd_transitions.begin(); it != states[i].fwd_transitions.end(); ++it){
          VecSet<VecSet<Lang::MemLoc<int> > > wss = (*it)->instruction.get_write_sets();
          for(auto wsit = wss.begin(); wsit != wss.end(); ++wsit){
            if(wsit->size() > 0){
              VecSet<Lang::NML> nmls;
              for(auto wit = wsit->begin(); wit != wsit->end(); ++wit){
                nmls.insert(Lang::NML(*wit,p));
              }
              if ((*it)->instruction.get_type() != Lang::LOCKED) {
                if (nmls.size()>1)
                  throw new std::logic_error("PDual option does not support for lock block to write multiple memory locations.");
                else removed_lock_blocks_messages.insert(MsgHdr(p,nmls));
              }
            }
          }
        }
      }
    }

    
  }
}

DualChannelConstraint::Store DualChannelConstraint::Common::store_of_write(const Machine::PTransition &t) const{
  Store store(mem_size);
  if(t.instruction.get_type() == Lang::WRITE && t.instruction.get_expr().is_integer()){
    store = store.assign(index(Lang::NML(t.instruction.get_memloc(),t.pid)),
                         t.instruction.get_expr().get_integer());
  }else if((t.instruction.get_type() == Lang::LOCKED ||
            t.instruction.get_type() == Lang::SLOCKED) &&
           t.instruction.get_statement_count() == 1 &&
           t.instruction.get_statement(0)->get_type() == Lang::WRITE &&
           t.instruction.get_statement(0)->get_expr().is_integer()){
    store = store.assign(index(Lang::NML(t.instruction.get_statement(0)->get_memloc(),t.pid)),
                         t.instruction.get_statement(0)->get_expr().get_integer());
  }else if(t.instruction.get_type() == Lang::LOCKED &&
           t.instruction.get_statement_count() == 1 &&
           t.instruction.get_statement(0)->get_type() == Lang::SEQUENCE) {
    const Lang::Stmt<int> &seq = *t.instruction.get_statement(0);
    if (seq.get_statement_count() == 2 &&
        seq.get_statement(0)->get_type() == Lang::READASSERT &&
        seq.get_statement(1)->get_type() == Lang::WRITE &&
        seq.get_statement(1)->get_expr().is_integer()){
      store = store.assign(index(Lang::NML(seq.get_statement(1)->get_memloc(), t.pid)),
                           seq.get_statement(1)->get_expr().get_integer());
    }
  }
  /* Generalize */
  return store;
}

/*************************/
/* DualChannelConstraint */
/*************************/

DualChannelConstraint::DualChannelConstraint(std::vector<int> pcs, const Common::MsgHdr &msg, Common &c)
: pcs(pcs), common(c) {
  for(int ci=0; ci<pcs.size(); ci++) {
    std::vector<Msg> chni;
    if(ci==msg.wpid) {
      chni.push_back(Msg(Store(common.mem_size),msg.wpid,msg.nmls));
    }
    channels.push_back(chni);
  }
  mems.push_back(Store(common.mem_size));
  for(unsigned p = 0; p < pcs.size(); p++){
    reg_stores.push_back(Store(common.reg_count[p]));
  }
}

DualChannelConstraint::DualChannelConstraint(std::vector<int> pcs, Common &c)
: pcs(pcs), common(c) {
  for(int ci=0; ci<pcs.size(); ci++) {
    std::vector<Msg> chni;
    channels.push_back(chni);
  }
  mems.push_back(Store(common.mem_size));
  for(unsigned p = 0; p < pcs.size(); p++){
    reg_stores.push_back(Store(common.reg_count[p]));
  }
}

bool DualChannelConstraint::is_init_state() const{
  for(unsigned p = 0; p < pcs.size(); ++p){
    if(pcs[p] != 0){
      return false;
    }
  }
  for(unsigned ci = 0; ci < channels.size(); ++ci){
    if(channels[ci].size() != 0){
      return false;
    }
  }
  /* Check all memory locations against their intended initial values */
  const Store &str = mems[0];
  for(unsigned i = 0; i < common.machine.gvars.size(); ++i){
    assert(common.index(Lang::NML::global(i)) == int(i));
    if(str[i] != value_t::STAR && !common.machine.gvars[i].value.is_wild() &&
       str[i].get_int() != common.machine.gvars[i].value.get_value()){
      return false;
    }
  }
  for(unsigned p = 0; p < pcs.size(); ++p){
    for(unsigned i = 0; i < common.machine.lvars[p].size(); ++i){
      int ix = common.index(Lang::NML::local(i,p));
      if(str[ix] != value_t::STAR && !common.machine.lvars[p][i].value.is_wild() &&
         str[ix].get_int() != common.machine.lvars[p][i].value.get_value()){
        return false;
      }
    }
  }
  /* Check all registers against their intended initial values */
  for(unsigned p = 0; p < reg_stores.size(); ++p){
    for(int i = 0; i < common.reg_count[p]; ++i){
      if(reg_stores[p][i] != value_t::STAR && !common.machine.regs[p][i].value.is_wild() &&
         reg_stores[p][i].get_int() != common.machine.regs[p][i].value.get_value()){
        return false;
      }
    }
  }
  return true;
}

std::string DualChannelConstraint::to_string() const noexcept {
  std::stringstream ss;
  bool first_var = true;
  for(unsigned p = 0; p < pcs.size(); ++p){
    process_to_string(p, ss);
    ss << "\n";
  };
  ss << "Channels:\n";
  for(int ci=0; ci<channels.size(); ci++) {
    first_var = true;
    ss << "c[P" << ci << "]: [";
    for(unsigned msgi = 0; msgi < channels[ci].size(); msgi++){
      if (!first_var){
        ss << ", ";
        first_var = false;
      }
      ss << channels[ci][msgi].to_short_string(common);
    }
    ss << "]\n";
  }
  ss << "Memmory: {";
  first_var = true;
  for(int i = 0; i < common.gvar_count; ++i){
    if(!first_var) ss << ", ";
    first_var = false;
    ss << common.machine.gvars[i].name << "=";
    if(mems[0][common.index(Lang::NML::global(i))] == value_t::STAR){
      ss << "*";
    }else{
      ss << mems[0][common.index(Lang::NML::global(i))];
    }
  }
  for(unsigned p = 0; p < common.reg_count.size(); ++p){
    for(unsigned i = 0; i < common.machine.lvars[p].size(); ++i){
      if(!first_var) ss << ", ";
      first_var = false;
      ss << common.machine.lvars[p][i].name << "[P" << p << "]=";
      if(mems[0][common.index(Lang::NML::local(i,p))] == value_t::STAR){
        ss << "*";
      }else{
        ss << mems[0][common.index(Lang::NML::local(i,p))];
      }
    }
  }
  
  ss << "}\n";
  return ss.str();
};

void DualChannelConstraint::process_to_string(int p, std::stringstream &ss) const noexcept {
  ss << "P" << p << " @Q" << pcs[p] << " {";
  bool first = true;
  for(int r = 0; r < common.reg_count[p]; ++r){
    if(!first){
      ss << ", ";
    }
    first = false;
    ss << common.machine.regs[p][r].name << "=";
    if(reg_stores[p][r] == value_t::STAR){
      ss << "*";
    }else{
      ss << reg_stores[p][r];
    }
  }
  ss << "}";
};


// return -1 for the first element by propagation of the channel
// return number>=0 for newest element by own writing
// return -2 for empty channel
int DualChannelConstraint::index_of_read(Lang::NML nml, int pid) const{
  int i = channels[pid].size()-1;
  while(i>=0) {
    if(channels[pid][i].wpid == pid && channels[pid][i].nmls.count(nml)){
      return i;
    }
    i--;
  }
  
  if (channels[pid].size()>0) {
    if(channels[pid][0].wpid == -1 && channels[pid][0].nmls.count(nml)!=0) {
      return -1;
    }
  }
  return -2;
}

Constraint::Comparison DualChannelConstraint::entailment_compare(const Constraint &c) const{
  assert(dynamic_cast<const DualChannelConstraint*>(&c));
  return entailment_compare_impl(static_cast<const DualChannelConstraint&>(c));
}

Constraint::Comparison DualChannelConstraint::entailment_compare_impl(const DualChannelConstraint &chc) const{
  if(pcs != chc.pcs){
    return Constraint::INCOMPARABLE;
  }

  Constraint::Comparison cmp = Constraint::EQUAL;

  for(unsigned p = 0; p < reg_stores.size(); ++p){
    cmp = Constraint::comb_comp(cmp,reg_stores[p].entailment_compare(chc.reg_stores[p]));
    if(cmp == Constraint::INCOMPARABLE) return cmp;
  }

  cmp = Constraint::comb_comp(cmp,mems[0].entailment_compare(chc.mems[0]));
  if(cmp == Constraint::INCOMPARABLE) return cmp;
  
  return entailment_compare_channels(chc,cmp);
}
    
    
Constraint::Comparison DualChannelConstraint::entailment_compare_channels(const DualChannelConstraint &dcc, Constraint::Comparison cmp) const{

  
  for (int ci=0; ci<channels.size(); ci++) {
    if(channels[ci].size() == dcc.channels[ci].size()){
      /* Each message in the channel must match the corresponding message in the other channel */
      
      for(unsigned i = 0; i < channels[ci].size(); ++i){
        cmp = Constraint::comb_comp(cmp,channels[ci][i].entailment_compare(dcc.channels[ci][i]));
        if(cmp == Constraint::INCOMPARABLE) return cmp;
      }
      if(ci==channels.size()-1) return cmp;
    }else{
      if(channels[ci].size() > dcc.channels[ci].size()){
        
        /* dcc.channels[ci] should be a strict subword of this->channels[ci] */
        if(Constraint::comb_comp(cmp,Constraint::GREATER) == Constraint::INCOMPARABLE){
          return Constraint::INCOMPARABLE;
        }
        
        VecSet<VecSet<Lang::NML> >  has_written_this, has_written_dcc;
        int j = int(dcc.channels[ci].size())-1;
        int i = int(channels[ci].size())-1;
        while(j >= 0){
          bool own_check = (has_written_dcc.count(dcc.channels[ci][j].nmls) == 0) && (dcc.channels[ci][j].wpid == ci);
          if(own_check){
            if (has_written_this.count(dcc.channels[ci][j].nmls) != 0) return Constraint::INCOMPARABLE;
            bool found = 0;
            while (i>=0) {
              if(i < j){
                /* There are not enough messages left in dcc.channel to
                 * match the ones in this->channel */
                return Constraint::INCOMPARABLE;
              }
              if(channels[ci][i].wpid == ci) {
                has_written_this.insert(channels[ci][i].nmls);
                if (channels[ci][i].nmls == dcc.channels[ci][j].nmls &&
                    !Constraint::comb_comp(Constraint::LESS,dcc.channels[ci][j].entailment_compare(channels[ci][i])) )
                {
                  found = 1;
                  i--;
                  break;
                }
              }
              i--;
            }
            if(found) {
                has_written_dcc.insert(dcc.channels[ci][j].nmls);
                j--;
            }
            else return Constraint::INCOMPARABLE;
          } else {
            bool found = 0;
            while (i>=0) {
              if(i < j) return Constraint::INCOMPARABLE;
              if(channels[ci][i].wpid == ci) has_written_this.insert(channels[ci][i].nmls);
              
              if (channels[ci][i].nmls == dcc.channels[ci][j].nmls && channels[ci][i].wpid == dcc.channels[ci][j].wpid &&
                  !Constraint::comb_comp(Constraint::LESS,dcc.channels[ci][j].entailment_compare(channels[ci][i])) ) {
                found = 1;
                i--;
                break;
              }
              i--;
            }
            if(found) {
              if(dcc.channels[ci][j].wpid == ci) has_written_dcc.insert(dcc.channels[ci][j].nmls);
              j--;
            }
            else return Constraint::INCOMPARABLE;
          }
        }
        assert(j == -1 && i >= j);
        if (ci==channels.size()-1) return Constraint::GREATER;
        else {
          cmp = Constraint::GREATER;
        }
        
        
      }else{
        /* this->channels[ci] should be a strict subword of dcc.channels[ci] */
        if(Constraint::comb_comp(cmp,Constraint::LESS) == Constraint::INCOMPARABLE){
          return Constraint::INCOMPARABLE;
        }
        
        VecSet<VecSet<Lang::NML> >  has_written_this, has_written_dcc;
        int j = int(dcc.channels[ci].size())-1;
        int i = int(channels[ci].size())-1;
        while(i >= 0){
          bool own_check = (has_written_this.count(channels[ci][i].nmls) == 0) && (channels[ci][i].wpid == ci);
          if(own_check){
            
            if (has_written_dcc.count(channels[ci][i].nmls) != 0) return Constraint::INCOMPARABLE;
            bool found = 0;
            while (j>=0) {
              if(j < i){
                /* There are not enough messages left in dcc.channel to
                 * match the ones in this->channel */
                return Constraint::INCOMPARABLE;
              }
              if(dcc.channels[ci][j].wpid == ci) {
                has_written_dcc.insert(dcc.channels[ci][j].nmls);
                if (dcc.channels[ci][j].nmls == channels[ci][i].nmls &&
                    !Constraint::comb_comp(Constraint::LESS,channels[ci][i].entailment_compare(dcc.channels[ci][j])) )
                {
                  found = 1;
                  j--;
                  break;
                }
              }
              j--;
            }
            if(found) {
              has_written_this.insert(channels[ci][i].nmls);
              i--;
            }
            else return Constraint::INCOMPARABLE;
          } else {
            bool found = 0;
            while (j>=0) {
              if(j < i) return Constraint::INCOMPARABLE;
              if(dcc.channels[ci][j].wpid == ci) has_written_dcc.insert(dcc.channels[ci][j].nmls);

              if (dcc.channels[ci][j].nmls == channels[ci][i].nmls && dcc.channels[ci][j].wpid == channels[ci][i].wpid &&
                  !Constraint::comb_comp(Constraint::LESS,channels[ci][i].entailment_compare(dcc.channels[ci][j])) ) {
                found = 1;
                j--;
                break;
              }
              j--;
            }
            //if(found) i--;
            if(found) {
              if(channels[ci][i].wpid == ci) has_written_this.insert(channels[ci][i].nmls);
              i--;
            }
            else return Constraint::INCOMPARABLE;
          }
        }
        assert(i == -1 && j >= i);
        if (ci==channels.size()-1) return Constraint::LESS;
        else {
          cmp =  Constraint::LESS;
        }
      }
    }
  }
  return Constraint::INCOMPARABLE;
}

std::vector<std::vector<DualChannelConstraint::MsgCharacterization>> DualChannelConstraint::characterize_channels() const{
    std::vector<std::vector<MsgCharacterization>> res;
  
    std::vector<VecSet<VecSet<Lang::NML> > > has_written(pcs.size());
    for(unsigned ci = 0; ci < channels.size(); ci++){
      has_written[ci].reserve(channels[ci].size());
    }
  
    for(int ci=0; ci < channels.size(); ci++) {
      std::vector<MsgCharacterization> chni;
      
      for(int msgi=channels[ci].size()-1; msgi>=0; msgi--) {
        if (channels[ci][msgi].wpid == ci && has_written[ci].count(channels[ci][msgi].nmls) == 0) { // process ci owns this msg
          chni.push_back(MsgCharacterization(channels[ci][msgi].wpid,channels[ci][msgi].nmls));
          has_written[ci].insert(channels[ci][msgi].nmls);
        }
      }
      std::vector<MsgCharacterization> w;
      w.reserve(chni.size());
      for(unsigned i = 0; i < chni.size(); i++){
        w.push_back(chni[chni.size() - i - 1]);
      }

      
      res.push_back(w);
    }
  
    return res;

}