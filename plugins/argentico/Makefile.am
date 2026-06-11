AM_CFLAGS =\
	-Wall\
	-O4\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""@localedir@"\" \
	@PACKAGE_CFLAGS@ \
	-I$(top_srcdir)/librawstudio/ \
	-I$(top_srcdir)/

lib_LTLIBRARIES = argentico.la

libdir = @RAWSTUDIO_PLUGINS_LIBS_DIR@

argentico_la_LIBADD = @PACKAGE_LIBS@ -lm
argentico_la_LDFLAGS = -module -avoid-version
argentico_la_SOURCES = argentico.c
