# Makefile.ql : OpenDUNE for the Sinclair QL + QLion Gold card
#
# Put this file in the OpenDUNE top-level folder and run:
#     make -f Makefile.ql
#
# Cross-compiled with qdos-gcc (jas assembler). QL-specific files:
#   src/video/video_ql.c   src/video/ql_blit.s    src/video/ql_pointer.c
#   src/video/ql_mouse.s   src/audio/dsp_ql.c     src/audio/midi_ql.c
#   src/os/readdir_ql.c    src/os/qlcompat.c      src/os/qlfix.h
#   src/video/video_ql.h   src/audio/ql_midiwatch.s
# plus QL branches in src/file.c, src/timer.c, src/gui/gui.c, src/sprites.c,
# src/os/endian.h, src/os/sleep.h and src/os/strings.h, and a hand-made src/rev.c.

CC      = qdos-gcc
CFLAGS  = -O2 -DQL -include src/os/qlfix.h \
          -DOrientation_Orientation256ToOrientation8=Ori256To8 \
          -DOrientation_Orientation256ToOrientation16=Ori256To16 \
          -Iinclude
LDFLAGS =
LIBS    =
TARGET  = opendune

SRC = \
	src/animation.c \
	src/audio/driver.c \
	src/audio/dsp_ql.c \
	src/audio/midi_ql.c \
	src/audio/mt32mpu.c \
	src/audio/sound.c \
	src/codec/format40.c \
	src/codec/format80.c \
	src/config.c \
	src/crashlog/crashlog.c \
	src/crashlog/crashlog_none.c \
	src/cutscene.c \
	src/explosion.c \
	src/file.c \
	src/gfx.c \
	src/gui/editbox.c \
	src/gui/font.c \
	src/gui/gui.c \
	src/gui/mentat.c \
	src/gui/security.c \
	src/gui/viewport.c \
	src/gui/widget.c \
	src/gui/widget_click.c \
	src/gui/widget_draw.c \
	src/house.c \
	src/ini.c \
	src/inifile.c \
	src/input/input.c \
	src/input/mouse.c \
	src/load.c \
	src/map.c \
	src/object.c \
	src/opendune.c \
	src/os/endian.c \
	src/os/error.c \
	src/os/qlcompat.c \
	src/os/readdir_ql.c \
	src/pool/house.c \
	src/pool/structure.c \
	src/pool/team.c \
	src/pool/unit.c \
	src/rev.c \
	src/save.c \
	src/saveload/house.c \
	src/saveload/info.c \
	src/saveload/map.c \
	src/saveload/object.c \
	src/saveload/saveload.c \
	src/saveload/scenario.c \
	src/saveload/scriptengine.c \
	src/saveload/structure.c \
	src/saveload/team.c \
	src/saveload/unit.c \
	src/scenario.c \
	src/script/general.c \
	src/script/script.c \
	src/script/structure.c \
	src/script/team.c \
	src/script/unit.c \
	src/sprites.c \
	src/string.c \
	src/structure.c \
	src/table/actioninfo.c \
	src/table/animation.c \
	src/table/explosion.c \
	src/table/houseanimation.c \
	src/table/houseinfo.c \
	src/table/landscapeinfo.c \
	src/table/movementtype.c \
	src/table/selectiontype.c \
	src/table/sound.c \
	src/table/structureinfo.c \
	src/table/teamaction.c \
	src/table/tilediff.c \
	src/table/unitinfo.c \
	src/table/widget.c \
	src/table/widgetinfo.c \
	src/table/windowdesc.c \
	src/team.c \
	src/tile.c \
	src/timer.c \
	src/tools.c \
	src/unit.c \
	src/video/video_ql.c \
	src/video/ql_pointer.c \
	src/video/video_fps.c \
	src/wsa.c

ASRC = src/video/ql_mouse.s \
       src/video/ql_blit.s \
       src/audio/ql_midiwatch.s

OBJ = $(SRC:.c=.o) $(ASRC:.s=.o)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(CC) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: clean
