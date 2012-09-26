AUTOMAKE_OPTIONS = -Wno-override ## Make autotools quit complaining about explicit rule for gui

dist_EXTRA = Makefile.am
EXTRA_PROGRAMS = memorax-gui
bin_PROGRAMS = memorax @GUI@
memorax_SOURCES = ap_list.tcc ap_list.h automaton.cpp automaton.h cegar_reachability.cpp cegar_reachability.h cmsat.h constraint_container1.cpp constraint_container1.h constraint_container.h constraint.h exact_bwd.cpp exact_bwd.h lang.tcc lang.h lexer.cpp lexer.h log.cpp log.h machine.cpp machine.h main.cpp parser.cpp parser.h pb_cegar.cpp pb_cegar.h pb_constraint.cpp pb_constraint.h pb_container1.cpp pb_container1.h pb_container2.cpp pb_container2.h predicates.tcc predicates.h reachability.cpp reachability.h sb_constraint.cpp sb_constraint.h sb_container.cpp sb_container.h sb_tso_bwd.cpp sb_tso_bwd.h shared.h sharinglist.tcc sharinglist.h shellcmd.cpp shellcmd.h syntax_string.tcc syntax_string.h test.h test.cpp ticket_queue.h trace.cpp trace.h tso_cycle.cpp tso_cycle.h tso_cycle_lock.cpp tso_cycle_lock.h tso_fencins.cpp tso_fencins.h tso_var.cpp tso_var.h vecset.h vecset.tcc
memorax_gui_SOURCES = gui.py

memorax-gui$(EXEEXT): $(srcdir)/gui.py
	cat $(srcdir)/gui.py | sed 's#%%PYTHON%%#$(PYTHON)#g' | \
	sed 's#%%BINARY%%#$(bindir)/memorax#g' | \
	sed 's#%%GUI_STRING%%#@PACKAGE_NAME@ GUI @PACKAGE_VERSION@#g' > memorax-gui