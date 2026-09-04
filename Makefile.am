# This file will be processed with automake-1.7 to create Makefile.in

AUTOMAKE_OPTIONS = 1.7
ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

NULL =

DISTCHECK_CONFIGURE_FLAGS = \
	--without-systemd \
	--enable-compile-warnings=no \
	CFLAGS='-Wno-deprecated-declarations'

SUBDIRS =
if BUILTIN_HELPER
SUBDIRS += helper
endif

SUBDIRS +=	\
	po 			\
	src			\
	savers			\
	data			\
	doc			\
	$(NULL)

EXTRA_DIST =			\
	autogen.sh		\
	COPYING			\
	COPYING.LIB		\
	AUTHORS			\
	INSTALL			\
	README			\
	ChangeLog		\
	its			\
	NEWS			\
	$(NULL)

DISTCLEANFILES =		\
	$(NULL)

if ENABLE_BUNDLED_LIBWLEMBED

# The stamp marks that libwlembed has been built and staged. It is created by
# the src subdirectory (the sole driver, via its BUILT_SOURCES hook); savers
# only waits for it order-only. This target exists so nondiscoverable re-entry
# (e.g. "make libwlembed.stamp") still produces it.
libwlembed.stamp: libwlembed-build
	@touch $@

libwlembed-build:
	@if test ! -f $(LIBWLEMBED_SUBMODULE_BUILDDIR)/build.ninja; then \
		echo "Configuring bundled libwlembed..."; \
		$(MESON) setup \
			--prefix=$(prefix) \
			--libdir=$(libdir) \
			--buildtype=release \
			--wrap-mode=nodownload \
			-Dintrospection=false \
			-Dexamples=false \
			$(LIBWLEMBED_SUBMODULE_BUILDDIR) \
			$(top_srcdir)/submodules/libwlembed; \
	else \
		$(MESON) setup --reconfigure \
			--prefix=$(prefix) \
			--libdir=$(libdir) \
			--buildtype=release \
			--wrap-mode=nodownload \
			-Dintrospection=false \
			-Dexamples=false \
			$(LIBWLEMBED_SUBMODULE_BUILDDIR) \
			$(top_srcdir)/submodules/libwlembed; \
	fi
	$(NINJA) -C $(LIBWLEMBED_SUBMODULE_BUILDDIR)
	@if test ! -f "$(LIBWLEMBED_SUBMODULE_STAGE)$(libdir)/libwlembed-0.so.0" -o \
		"$(LIBWLEMBED_SUBMODULE_BUILDDIR)/libwlembed/libwlembed-0.so.0.0.0" -nt \
			"$(LIBWLEMBED_SUBMODULE_STAGE)$(libdir)/libwlembed-0.so.0"; then \
		rm -rf $(LIBWLEMBED_SUBMODULE_STAGE); \
		DESTDIR="$(LIBWLEMBED_SUBMODULE_STAGE)" \
			$(MESON) install -C $(LIBWLEMBED_SUBMODULE_BUILDDIR) --no-rebuild; \
	fi

install-exec-hook:
	$(MESON) install -C $(LIBWLEMBED_SUBMODULE_BUILDDIR) \
		--destdir "$(DESTDIR)" --no-rebuild

distclean-local:
	rm -f libwlembed.stamp
	rm -rf $(LIBWLEMBED_SUBMODULE_BUILDDIR)

# Do not ship the submodule build directory in tarballs.
dist-hook:
	rm -rf $(distdir)/submodules/libwlembed/build

endif

MAINTAINERCLEANFILES =		\
	*~			\
	compile			\
	configure		\
	INSTALL			\
	COPYING			\
	install-sh		\
	missing			\
	mkinstalldirs		\
	config.guess		\
	ltmain.sh		\
	config.sub		\
	depcomp			\
	Makefile.in		\
	config.h.*		\
	aclocal.m4		\
	acinclude.m4		\
	$(NULL)

# Build ChangeLog from GIT  history
ChangeLog:
	$(AM_V_GEN) if test -d $(top_srcdir)/.git; then \
		GIT_DIR="$(top_srcdir)/.git" git log --stat > $@; \
	fi

dist: ChangeLog

.PHONY: ChangeLog

-include $(top_srcdir)/git.mk
