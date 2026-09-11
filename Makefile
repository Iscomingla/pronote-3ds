#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
# GFXBUILD is the directory where converted graphics files will be placed
#   If set to $(BUILD), it will statically link in the converted
#   graphics into the executable (useful for games!)
# MUSIC is a list of directories containing music files
# ROMFS is the directory which will be packaged into the game as readable content
# GFXBUILD is the directory where converted graphics files will be placed
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	src
DATA		:=	data  
INCLUDES	:=	include
ROMFS		:=	romfs

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard

CFLAGS	:=	-g -Wall -O2 -mword-relocations \
			-fomit-frame-pointer -ffast-math \
			$(ARCH)

CFLAGS	+=	$(INCLUDE) -DARM11 -D_3DS

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS	:= -lctru -lcurl -lmbedtls -lmbedcrypto -lmbedx509 -lz

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:= $(CTRULIB)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.shlist)))
GFXFILES	:=	$(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))

ifneq ($(MUSIC),)
	export ROMFS_DIRS	:= $(CURDIR)/$(NOTEDIR) $(CURDIR)/$(ROMFS)
	GFXBUILD	:=	$(CURDIR)/$(BUILD)
else
	export ROMFS_DIRS	:= $(CURDIR)/$(ROMFS)
	GFXBUILD	:=	$(CURDIR)/$(BUILD)
endif

export OFILES_SOURCES 	:= $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(SFILES:.s=.o)

export OFILES 	:=	$(PICAFILES:.v.pica=.shbin.o) $(SHLISTFILES:.shlist=.shbin.o) \
			$(GFXFILES:.t3s=.t3o.o) $(OFILES_SOURCES)

export HFILES	:=	$(PICAFILES:.v.pica=_shbin.h) $(SHLISTFILES:.shlist=.shbin.h) \
			$(GFXFILES:.t3s=.t3o.h)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.png)
	ifneq (,$(findstring $(TARGET).png,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).png
	else
		ifneq (,$(findstring icon.png,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.png
		endif
	endif
endif

ifeq ($(strip $(NO_SMDH)),)
	export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

.PHONY: all clean

#---------------------------------------------------------------------------------
all: $(BUILD) $(GFXBUILD) $(OUTPUT).3dsx $(OUTPUT).elf
	@echo "Build complete"

$(BUILD):
	@mkdir -p $@

ifneq ($(GFXBUILD),$(BUILD))
$(GFXBUILD):
	@mkdir -p $@
endif

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -rf $(BUILD) $(OUTPUT).3dsx $(OUTPUT).elf $(OUTPUT).smdh

#---------------------------------------------------------------------------------
else

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXFLAGS)

$(OUTPUT).elf	:	$(OFILES)

#------.o targets
%.o	:	%.c
	@echo $(notdir $<)
	@$(CC) -MMD -MP -MF $(DEPSDIR)/$*.d $(CFLAGS) -c $< -o $@

%.o	:	%.cpp
	@echo $(notdir $<)
	@$(CXX) -MMD -MP -MF $(DEPSDIR)/$*.d $(CXXFLAGS) -c $< -o $@

%.o	:	%.s
	@echo $(notdir $<)
	@$(AS) -MMD -MP -MF $(DEPSDIR)/$*.d $(ASFLAGS) -c $< -o $@

define shader-as
	$(eval FILEPATH := $3/$1)
	$(eval FILEBASE := $(basename $(notdir $(FILEPATH))))
	echo "$(FILEPATH)" > $(dir $(FILEPATH))$(FILEBASE).shbin.d
	picasso -o $(GFXBUILD)/$(FILEBASE).shbin $(FILEPATH)
	bin2s $(GFXBUILD)/$(FILEBASE).shbin | $(AS) -o $(GFXBUILD)/$(FILEBASE).shbin.o

endef
%.shbin.o	:	%.v.pica
	$(call shader-as,$<,$(basename $@),$(dir $@))

%.shbin.o	:	%.shlist
	@echo $(notdir $<)
	@cp $< $(GFXBUILD)/$(notdir $<)
	@cd $(GFXBUILD) && picasso $(notdir $<)
	@bin2s $(GFXBUILD)/$(patsubst %.shlist,%.shbin,$(notdir $<)) | $(AS) -o $(GFXBUILD)/$(notdir $@)
	@rm $(GFXBUILD)/$(notdir $<)

%.t3o	:	%.t3s
	@echo $(notdir $<)
	@tex3ds -i $< -H $*.h -d $(GFXBUILD) -o $(GFXBUILD)/$*.t3o

%.t3o.o	:	%.t3o
	@echo $(notdir $<)
	@bin2s $(GFXBUILD)/$< | $(AS) -o $(GFXBUILD)/$@

-include $(DEPSDIR)/*.d

endif
#---------------------------------------------------------------------------
