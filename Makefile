PROG_CXX=	curses_test
SRCS=		main.cpp
MAN=

CXXSTD=		c++23
LDADD+=		-lncursesw
DPADD+=		${LIBNCURSESW}

.include <bsd.prog.mk>
