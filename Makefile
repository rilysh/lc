PROGRAM = lc
CFLAGS  = -Wall -Wextra -O2 -s

all:
	${CC} ${CFLAGS} -o ${PROGRAM} ${PROGRAM}.c

clean:
	@rm -f ${PROGRAM}
