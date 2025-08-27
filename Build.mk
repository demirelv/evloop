WSDIR		?= $(CURDIR)/../
PROJECT_DIR	?= $(CURDIR)
OUTDIR		:= $(PROJECT_DIR)/build
DESTDIR		?= $(PROJECT_DIR)/install
PROJECT_NAME	?= $(subst /,_, ${PROJECT_DIR})
PROJECT_PACKAGE ?= ${PROJECT_NAME}.tar.bz2

CPP		:= $(CROSS_COMPILE_PREFIX)g++
CC		:= $(CROSS_COMPILE_PREFIX)gcc
LD		:= $(CROSS_COMPILE_PREFIX)ld
AR		:= $(CROSS_COMPILE_PREFIX)ar
RM		:= rm -rf
MKDIR		:= mkdir -p
CP		:= cp -rf
CD		:= cd
TAR		:= tar
CCS		:= checkpatch.pl --no-tree -f

_CPPFLAGS	:= -Wall -Wextra -Werror -pipe -g3 -O2 -fsigned-char -fno-strict-aliasing -fPIC -Werror=unused-result $(CPPFLAGS) $(EXTRA_CPPFLAGS) -I.
_CFLAGS		:= -Wall -Wextra -Werror -pipe -g3 -O2 -fsigned-char -fno-strict-aliasing -fPIC -Werror=unused-result $(CFLAGS) $(EXTRA_CFLAGS) -I.
_LDFLAGS	:= $(LDFLAGS) $(EXTRA_LDFLAGS) -L.

MAKE		:= CPPFLAGS="$(CPPFLAGS)" EXTRA_CPPFLAGS="$(EXTRA_CPPFLAGS)" CFLAGS="$(CFLAGS)" EXTRA_CFLAGS="$(EXTRA_CFLAGS)" LDFLAGS="$(LDFLAGS)" EXTRA_LDFLAGS="$(EXTRA_LDFLAGS)" $(MAKE)

ifneq ($(V)$(VERBOSE),)
    Q =
    MAKE += V="$(V)$(VERBOSE)"
else
    Q = @
    MAKE += --no-print-directory
endif

MAKEDIR		:= WSDIR="${WSDIR}" PROJECT_DIR="$(PROJECT_DIR)" DESTDIR="$(DESTDIR)" $(MAKE)

_depends_c	= $(CC) $(_CFLAGS) $($1-cflags-y) $($1-incs) -M $$< > $$@.d
_compile_c	= $(CC) $(_CFLAGS) $($1-cflags-y) $($1-incs) -c $$< -o $$@
_compile_cpp	= $(CPP) $(_CPPFLAGS) $($1-cppflags-y) $($1-incs) -c $$< -o $$@
_link_cpp	= $(CPP) $($1-objs) -o $$@ ${_LDFLAGS} $($1-ldflags-y) $($1-libps) $($1-library-y)
_link_c		= $(CC) $($1-objs) -o $$@ ${_LDFLAGS} $($1-ldflags-y) $($1-libps) $($1-library-y)
_link_so_c	= $(CC) -shared $($1-objs) -o $$@ ${_LDFLAGS} $($1-ldflags-y) $($1-libps) $($1-library-y)
_link_so_cpp	= $(CPP) -shared $($1-objs) -o $$@ ${_LDFLAGS} $($1-ldflags-y) $($1-libps) $($1-library-y)

depends_c	= echo "$(_depends_c)" > $$@.d.cmd ; $(_depends_c)
compile_c	= echo "$(_compile_c)" > $$@.cmd ; $(_compile_c)
compile_cpp	= echo "$(_compile_cpp)" > $$@.cmd ; $(_compile_cpp)
link_c		= echo "$(_link_c)" > $$@.cmd ; $(_link_c)
link_cpp	= echo "$(_link_cpp)" > $$@.cmd ; $(_link_cpp)
link_so_c	= echo "$(_link_so_c)" > $$@.cmd ; $(_link_so_c)
link_so_cpp	= echo "$(_link_so_cpp)" > $$@.cmd ; $(_link_so_cpp)

define proj-define
$(addsuffix _all, $1):
	$(Q) $(CD) $1 && $(MAKE) WSDIR=$(CURDIR) build install
$(addsuffix _build, $1):
	$(Q) $(CD) $1 && $(MAKE) WSDIR=$(CURDIR) build
$(addsuffix _clean, $1):
	$(Q) $(CD) $1 && $(MAKE) WSDIR=$(CURDIR) clean
$(addsuffix _install, $1):
	$(Q) $(CD) $1 && $(MAKE) WSDIR=$(CURDIR) install
$(addsuffix _uninstall, $1):
	$(Q) $(CD) $1 && $(MAKE) WSDIR=$(CURDIR) uninstall
$(addsuffix _codestyle, $1):
	$(Q) $(CD) $1 && $(MAKE) WSDIR=$(CURDIR) checkstyle
$(addsuffix _package, $1):
	$(Q) $(CD) $1 && $(MAKE) WSDIR=$(CURDIR) package
endef

define depends-define
$(addsuffix _depend_build_ins, $1):
	$(Q) $(CD) $(WSDIR)/$1 && $(MAKE) WSDIR=$(WSDIR) \
		PROJECT_DIR=$(WSDIR)/$1 DESTDIR=$(WSDIR)/$1/install OUTDIR=$(WSDIR)/$1/build build install
endef

define dir-define
$(addsuffix _all, $1):
	$(Q) $(MAKEDIR) OUTDIR=${OUTDIR}/$1 -C '$1' all
$(addsuffix _build, $1):
	$(Q) $(MAKEDIR) OUTDIR=${OUTDIR}/$1 -C '$1' build
$(addsuffix _clean, $1):
	$(Q) $(MAKEDIR) OUTDIR=${OUTDIR}/$1 -C '$1' clean
$(addsuffix _install, $1):
	$(Q) $(MAKEDIR) OUTDIR=${OUTDIR}/$1 -C '$1' install
$(addsuffix _uninstall, $1):
	$(Q) $(MAKEDIR) OUTDIR=${OUTDIR}/$1 -C '$1' uninstall
$(addsuffix _codestyle, $1):
	$(Q) $(MAKEDIR) OUTDIR=${OUTDIR}/$1 -C '$1' checkstyle
endef

define header-define
${OUTDIR}:
	$(Q)$(MKDIR) $$@
$(addsuffix _header, $1): ${OUTDIR}
	$(Q) echo HEADER $1; $(CP) $1 ${OUTDIR}/
endef

define code-style-define
$(addsuffix _codestyle, $1):
	$(Q) echo CCS $1; $(CCS) $1;
endef

define c-define
$(eval $1-objs		= $(patsubst %.c,${OUTDIR}/.$1/%.o,$($1-source-y)))
#${OUTDIR}/.$1:
#	$(Q)$(MKDIR) $$@
${OUTDIR}/.$1/%.o: %.c
	$(Q) echo CC $$<; $(MKDIR) $$(dir $$@); $(depends_c); $(compile_c)
endef

define cpp-define
$(eval $1-objs		= $(patsubst %.cpp,${OUTDIR}/.$1/%.o,$($1-source-y)))
${OUTDIR}/.$1:
	$(Q)$(MKDIR) $$@
${OUTDIR}/.$1/%.o: %.cpp
	$(Q) echo CPP $$<; $(MKDIR) $$(dir $$@); $(compile_cpp)
endef

define base-define
$(eval $(foreach H,$($1-header-y), $(eval $(call header-define,$H))))
$(eval $(foreach S,$($1-source-y), $(eval $(call code-style-define,$S))))
$(eval $(foreach D,$($1-depends-y),$(eval $(call depends-define,$D))))

$(eval $1-incs		= $(addprefix -I, $($1-include-y)) $(patsubst %,-I ${WSDIR}/%/install/usr/include,$($1-depends-y)))
$(eval $1-libps		= $(addprefix -L, ./ $($1-library-path-y)) $(patsubst %,-L ${WSDIR}/%/install/usr/lib,$($1-depends-y)))

$(eval $(if $(filter $($1-cpp),y),$(eval $(call cpp-define,$1)),$(eval $(call c-define,$1))))

$(addsuffix _all, $1): $(addsuffix _depends, $1) ${OUTDIR}/$1 $(addsuffix _header, $1)
	@true
$(addsuffix _build, $1): $(addsuffix _depends, $1) ${OUTDIR}/$1 $(addsuffix _header, $1)
	@true
$(addsuffix _clean, $1):
	$(RM) ${OUTDIR}
$(addsuffix _header, $1): $(addsuffix _header, $($1-header-y))
	@true
$(addsuffix _codestyle, $1): $(addsuffix _codestyle, $($1-source-y))
	@true
$(addsuffix _depends, $1): $(addsuffix _depend_build_ins, $($1-depends-y))
	@true
endef

define target-define
$(eval $(call base-define,$1))
${OUTDIR}/$1: $($1-objs)
ifneq ($($1-cpp),)
	$(Q) echo link $$@; $(link_cpp)
else
	$(Q) echo link $$(notdir $$@); $(link_c)
endif
endef

define library-define
$(eval $(call base-define,$1))
${OUTDIR}/$1: $($1-objs)
ifneq ($($1-cpp),)
	$(Q) echo link $$@; $(link_so_cpp)
else
	$(Q) echo link $$@; $(link_so_c)
endif
endef

define install-define
$(addsuffix _install, $(subst /,-, $(subst :,-, $1))):
	$(Q) echo INSTALL $(word 1, $(subst :, ,$1)); $(MKDIR) ${DESTDIR}${PREFIX}/$(dir $(word 2, $(subst :, ,$1))); $(if $(wildcard ${OUTDIR}/$(word 1, $(subst :, ,$1))), $(CP) ${OUTDIR}/$(word 1, $(subst :, ,$1)) ${DESTDIR}${PREFIX}/$(word 2, $(subst :, ,$1)), $(CP) ${PROJECT_DIR}/$(word 1, $(subst :, ,$1)) ${DESTDIR}${PREFIX}/$(word 2, $(subst :, ,$1)))
$(addsuffix _uninstall, $(subst /,-, $(subst :,-, $1))):
	$(Q) echo REMOVE $(word 1, $(subst :, ,$1)); $(RM) ${DESTDIR}${PREFIX}/$(word 2, $(subst :, ,$1))/$(word 1, $(subst :, ,$1))
endef

$(eval $(foreach P,$(proj-y),$(eval $(call proj-define,$P))))
$(eval $(foreach D,$(dir-y),$(eval $(call dir-define,$D))))
$(eval $(foreach T,$(target-y), $(eval $(call target-define,$T))))
$(eval $(foreach L,$(library-y), $(eval $(call library-define,$L))))
$(eval $(foreach V,$(install-y), $(eval $(call install-define,$V))))

%_pk-post: %/*.tar.bz2
	$(Q) ${MKDIR} ${DESTDIR}; ${TAR} -xjvf $< --directory=${DESTDIR}

all: $(addsuffix _all, $(proj-y))
all: $(addsuffix _all, $(dir-y))
all: $(addsuffix _all, $(library-y))
all: $(addsuffix _all, $(target-y))
	@true
build: $(addsuffix _build, $(proj-y))
build: $(addsuffix _build, $(dir-y))
build: $(addsuffix _build, $(library-y))
build: $(addsuffix _build, $(target-y))
	@true
clean: $(addsuffix _clean, $(proj-y))
clean: $(addsuffix _clean, $(target-y))
clean: $(addsuffix _clean, $(library-y))
clean: $(addsuffix _clean, $(dir-y))
	@true
install: $(addsuffix _install, $(proj-y))
install: $(addsuffix _install, $(dir-y))
install: $(addsuffix _install, $(subst /,-, $(subst :,-,$(install-y))))
	@true
uninstall: $(addsuffix _uninstall, $(proj-y))
uninstall: $(addsuffix _uninstall, $(dir-y))
unisstall: $(addsuffix _uninstall, $(subst /,-, $(subst :,-,$(install-y))))
	@true
package-pre:
package-post: package-def
package-def: $(addsuffix _package, $(proj-y)) $(addsuffix _pk-post, $(proj-y))
	$(Q) ${TAR} -C ${DESTDIR} -cjvf ${PROJECT_PACKAGE} .

checkstyle: $(addsuffix _codestyle, $(proj-y))
checkstyle: $(addsuffix _codestyle, $(dir-y))
checkstyle: $(addsuffix _codestyle, $(library-y))
checkstyle: $(addsuffix _codestyle, $(target-y))
	@true

%: %-pre %-def %-post
	@true

