AM_CFLAGS =\
	-Wall\
	-O4\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""@localedir@"\" \
	@PACKAGE_CFLAGS@ \
	-I$(top_srcdir)/librawstudio/ \
	-I$(top_srcdir)/

lib_LTLIBRARIES = effects.la

libdir = @RAWSTUDIO_PLUGINS_LIBS_DIR@

effects_la_LIBADD = @PACKAGE_LIBS@ -lm
effects_la_LDFLAGS = -module -avoid-version
effects_la_SOURCES = effects.c
