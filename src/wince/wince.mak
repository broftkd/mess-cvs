# only OS specific output files and rules
OSOBJS = \
		$(OBJ)/wince/crtsuppl.o \
		$(OBJ)/wince/ceblit.o \
		$(OBJ)/wince/ceinput.o \
		$(OBJ)/wince/cesound.o \
		$(OBJ)/wince/video.o \
		$(OBJ)/wince/window.o \
		$(OBJ)/wince/cemain.o \
		$(OBJ)/wince/cemenu.o \
		$(OBJ)/wince/misc.o \
		$(OBJ)/wince/dirent.o \
		$(OBJ)/wince/playgame.o \
		$(OBJ)/wince/invokegx.o \
		$(OBJ)/wince/rc.o \
		$(OBJ)/wince/ticker.o \
		$(OBJ)/wince/fronthlp.o \
		$(OBJ)/wince/emitx86.o \
		$(OBJ)/windows/fileio.o \
		$(OBJ)/windows/snprintf.o \
		$(OBJ)/mess/windows/menu.o \
		$(OBJ)/mess/windows/dialog.o \
		$(OBJ)/mess/windows/strconv.o \
		$(OBJ)/mess/windowsui/SmartListView.o \
		$(OBJ)/mess/windowsui/SoftwareList.o \
		$(OBJ)/zlib/crc32.o \
		$(OBJ)/zlib/adler32.o \
		$(OBJ)/zlib/compress.o \
		$(OBJ)/zlib/uncompr.o \
		$(OBJ)/zlib/deflate.o \
		$(OBJ)/zlib/zutil.o \
		$(OBJ)/zlib/trees.o \
		$(OBJ)/zlib/infcodes.o \
		$(OBJ)/zlib/infutil.o \
		$(OBJ)/zlib/inftrees.o \
		$(OBJ)/zlib/inflate.o \
		$(OBJ)/zlib/infblock.o \
		$(OBJ)/zlib/inffast.o

ifdef MESS
OSOBJS += \
		$(OBJ)/mess/windows/dirio.o \
		$(OBJ)/mess/windows/messwin.o
endif

RES = $(OBJ)/wince/mamece.res
