## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = librawstudio plugins src po pixmaps profiles

desktopdir = $(datadir)/applications
desktop_DATA = carastudio.desktop

appdatadir = $(datarootdir)/appdata
dist_appdata_DATA = carastudio.appdata.xml

# Manuel HTML de l'aide (F1 / menu Aide). Le code ouvre
# $(datadir)/$(PACKAGE)/help/index.<lang>.html (cf. rs-actions.c) ; ces
# fichiers doivent donc être installés ET distribués dans le tarball.
helpdir = $(datadir)/$(PACKAGE)/help
dist_help_DATA = help/index.en.html help/index.fr.html

EXTRA_DIST = \
	autogen.sh \
	ChangeLog \
	carastudio.desktop \
	gettext.h \
	data/icons

# Hicolor icon theme — installed to $(datadir)/$(PACKAGE)/icons/hicolor/
pkgiconhicolordir = $(datadir)/$(PACKAGE)/icons/hicolor

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(pkgiconhicolordir)/scalable/actions
	$(MKDIR_P) $(DESTDIR)$(pkgiconhicolordir)/48x48/actions
	$(MKDIR_P) $(DESTDIR)$(pkgiconhicolordir)/48x48/apps
	cp data/icons/hicolor/index.theme $(DESTDIR)$(pkgiconhicolordir)/
	cp data/icons/hicolor/scalable/actions/*.svg $(DESTDIR)$(pkgiconhicolordir)/scalable/actions/
	cp data/icons/hicolor/48x48/actions/*.png    $(DESTDIR)$(pkgiconhicolordir)/48x48/actions/
	cp data/icons/hicolor/48x48/apps/*.png       $(DESTDIR)$(pkgiconhicolordir)/48x48/apps/ 2>/dev/null || true

ChangeLog:
	git log >$@
