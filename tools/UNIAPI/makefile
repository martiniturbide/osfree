#
# A Makefile for UNIAPI
# (c) osFree project,
#

PROJ = uni2h
TRGT = $(PROJ).exe
DESC = Unified Interfaces to C/C++ header files converter
INSTALL_ADD = 1

!include $(%ROOT)/mk/toolspas.mk

install_add: .SYMBOLIC
 @cd $(ROOT)$(SEP)UNI
 @$(MAKE) $(MAKEOPT) h
 @cd $(ROOT)$(SEP)tools$(SEP)UNIAPI
