## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = librawstudio plugins src po pixmaps profiles

desktopdir = $(datadir)/applications
desktop_DATA = carastudio.desktop

appdatadir = $(datarootdir)/appdata
dist_appdata_DATA = carastudio.appdata.xml

EXTRA_DIST = \
	autogen.sh \
	ChangeLog \
	carastudio.desktop \
	gettext.h

ChangeLog:
	git log >$@
