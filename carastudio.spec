
Name:           carastudio
Version:        2026.07
Release:        1%{?dist}
Summary:        Convivial raw photo developer (a beefed-up fork of RawStudio)

License:        GPLv3+
URL:            https://github.com/carafife/CaraStudio
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  libtool
BuildRequires:  pkgconfig
BuildRequires:  gettext
BuildRequires:  gettext-devel
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libxml-2.0)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(lensfun)
BuildRequires:  pkgconfig(lcms2)
BuildRequires:  pkgconfig(libgphoto2)
BuildRequires:  pkgconfig(exiv2)
BuildRequires:  pkgconfig(libraw)
BuildRequires:  pkgconfig(fftw3f)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  libjpeg-turbo-devel
BuildRequires:  libtiff-devel

# Fusion d'expositions (menu Enfuse) : fournie par enblend-enfuse
Recommends:     enblend-enfuse

%description
CaraStudio is a friendly, accessible raw photo developer for Linux — a
beefed-up fork of RawStudio. It develops RAW files (and JPEG/TIFF) with a
curated set of powerful tools: white balance (eyedropper / auto / camera),
basic adjustments, tone equalizer, 3-way colour wheels, per-hue curves,
black & white, film negative, dehaze, soft light and artistic vignette.
It also offers fixed-ratio cropping, exposure blending (Enfuse), an
extended EXIF panel with keyword management, a bilingual French/English
interface and an integrated help manual.

%prep
%autosetup -n %{name}-%{version}

%build
# Le dépôt n'embarque pas le script configure : on le génère.
[ -x ./configure ] || autoreconf -fi
%configure
%make_build

%install
# librawstudio est liée avec un rpath vers %{_libdir} (standard) via libtool :
# on autorise ce rpath pour ne pas faire échouer la vérification QA de rpmbuild.
export QA_RPATHS=$(( 0x0001|0x0002|0x0010 ))

%make_install

# Retrait des fichiers de développement (inutiles pour l'utilisateur final).
rm -rf %{buildroot}%{_includedir}
rm -f  %{buildroot}%{_libdir}/librawstudio.a
rm -f  %{buildroot}%{_libdir}/pkgconfig/rawstudio-*.pc
find   %{buildroot} -name '*.la' -delete

# Icône RawStudio résiduelle (rebranding) : on ne la livre pas.
rm -f  %{buildroot}%{_datadir}/icons/rawstudio.png

# Catalogues de traduction.
%find_lang %{name}

# Validation du fichier .desktop (non bloquante).
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop || :

%files -f %{name}.lang
%license COPYING
%doc README.md
%{_bindir}/%{name}
%{_libdir}/%{name}/
%{_libdir}/librawstudio-%{version}.so
%{_libdir}/librawstudio.so
%{_datadir}/%{name}/
%{_datadir}/rawspeed/
%{_datadir}/icons/%{name}.png
%{_datadir}/applications/%{name}.desktop
%{_datadir}/appdata/%{name}.appdata.xml
%{_datadir}/pixmaps/%{name}/

%changelog
* Mon Jul 06 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-1
- Passage au versionnage par date (CalVer) : « 2026.07 ».
- Barre du haut : nouveau bouton « Enfuse » (fusion d'expositions) avec garde
  anti-plantage quand aucune photo n'est sélectionnée.
- Bloc « Courbes » à 4 onglets (Valeur + courbes RVB par canal), avec courbes
  préréglées et enregistrer/charger/supprimer.
- Réglages de base : boutons Auto-exposition, Auto-niveaux et Réinitialiser.
- Balance des blancs : bouton « Masque d'exposition ».
- Pédagogie du pipeline : bouton « Pipeline » (légende des 5 étapes) et badges
  A–E de couleur sur les modules.
- Aide (F1) enrichie et illustrée (schéma du pipeline, nouvelles fonctions,
  rubrique « À propos & communauté »), en français et en anglais.

* Sun Jul 05 2026 Carafife <carafife@users.noreply.github.com> - 1.0.1-3
- Support des RAW Fujifilm X-Trans (.RAF) : ils s'ouvrent et se développent
  désormais dans l'éditeur (démosaïquage via LibRaw). Signalé sur le forum.

* Sat Jul 04 2026 Carafife <carafife@users.noreply.github.com> - 1.0.1-2
- Onglet Infos : lecture de la compensation d'exposition sur les JPEG
  (fini l'affichage « -999,0 IL »), marque « CaraStudio », masquage des
  champs non renseignés ; correctif de la détection du fabricant Canon.
- Export JPEG : le profil ICC est toujours embarqué (y compris sRGB).
- Aide (F1) : installation du manuel HTML (jusque-là jamais installé).
- Portabilité de build : plancher abaissé à libraw >= 0.19, compat exiv2 0.27/0.28.

* Sun Jun 21 2026 Carafife <carafife@users.noreply.github.com> - 1.0.1-1
- Correctif : le Noir & Blanc (et les autres effets) restaient figés entre les
  instantanés A/B/C et ne se désactivaient pas (sélection A/B/C rendue globale).
- Portabilité : liaison explicite de libm au binaire.

* Sun Jun 21 2026 Carafife <carafife@users.noreply.github.com> - 1.0-1
- Première version stable de CaraStudio (fork bodybuildé de RawStudio).
