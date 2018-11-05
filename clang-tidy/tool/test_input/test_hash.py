#!/usr/bin/env python
# -*- coding: utf-8 -*-

s1 = "/project/git/Source/kwsys/Terminal.c:52:23: warning: use of a signed integer operand with a binary bitwise operator [hicpp-signed-bitwise]"\
     "  int default_vt100 = color & kwsysTerminal_Color_AssumeVT100;"\
     "                      ^"
s2 = "/project/git/Source/kwsys/Terminal.c:52:23: warning: use of a signed integer operand with a binary bitwise operator [hicpp-signed-bitwise]"\
     "  int default_vt100 = color & kwsysTerminal_Color_AssumeVT100;"\
     "                      ^"


print(s1.__hash__() == s2.__hash__())
