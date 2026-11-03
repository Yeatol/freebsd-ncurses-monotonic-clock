PROG_CXX=	ncurses-monotonic-clock
SRCS=		ncurses-monotonic-clock.cpp
MAN=

CXXSTD=		c++23
LDADD+=		-lncursesw
DPADD+=		${LIBNCURSESW}

.include <bsd.prog.mk>
