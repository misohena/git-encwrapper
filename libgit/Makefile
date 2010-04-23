
LIB_FILE=libgit.a
LIB_H =
LIB_H += cache.h
LIB_H += attr.h
LIB_H += strbuf.h

LIB_OBJS =
LIB_OBJS += attr.o
LIB_OBJS += strbuf.o
LIB_OBJS += wrapper.o



$(LIB_FILE): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

attr.o: $(LIB_H)
strbuf.o: $(LIB_H)
wrapper.o: $(LIB_H)


clean:
	rm -f $(LIB_OBJS)
