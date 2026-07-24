
Name:           carastudio
Version:        2026.07
Release:        5%{?dist}
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
* Fri Jul 24 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-5
- Couleur des boîtiers récents sans profil DCP (ex. Canon EOS R10) : rendu
  correct via la matrice couleur LibRaw (issue #10).
- Correction du cast magenta apparaissant sur les RAW après navigation
  (espace couleur d'entrée fixé par photo).
- CR3 : lecture de l'EXIF complet et de l'orientation via LibRaw — panneau
  Infos renseigné, vignettes orientées (issue #11).
- Lisibilité du contenu des dialogues (Préférences) sur thèmes système clairs.

* Thu Jul 23 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-4
- Rebuild : le correctif de dominante verte sur les RAW Fujifilm X-Trans (.raf),
  annoncé en 2026.07-3, était absent du binaire par erreur. Il est bien inclus
  cette fois.

* Wed Jul 22 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-3
- Couleur : correction d'une dominante verte apparue en 2026.07 sur les RAW
  Fujifilm X-Trans (.raf), en particulier les ciels et la mer. La balance des
  blancs boîtier lue via LibRaw ne s'applique plus à tort à ces fichiers.

* Wed Jul 22 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-2
- Traduction anglaise complétée pour les ajouts récents (styles/CaraStyles,
  boutons « Tout replier / déplier », DynaComp, Color scalpel, masque
  d'exposition, Auto niveaux, Réinitialiser, courbes…), qui s'affichaient encore
  en français lorsque l'interface était en anglais.

* Tue Jul 21 2026 Carafife <carafife@users.noreply.github.com> - 2026.07-1
- Passage au versionnage par date (CalVer) : « 2026.07 ». Le numéro de version
  s'affiche désormais dans le titre et l'écran de démarrage.
- Couleur & boîtiers récents : lecture de la balance des blancs boîtier via
  LibRaw (toutes marques), application de la WB même quand l'espace d'entrée
  est inconnu, rejet des multiplicateurs cam_mul non crédibles (fini les TIFF/
  JPEG exportés verdis à la relecture), et correction du cast rouge sur les
  8 bits à profil ICC exotique (PQ/HDR).
- Profils DCP : les profils importés par l'utilisateur (« Ajouter profile »)
  sont proposés pour tout boîtier, avec réglages par défaut par boîtier ;
  correction d'une contamination de la chaîne partagée entre vignettes.
- Vignettes : effets CaraStudio visibles dès l'ouverture + rafraîchissement
  live ; extraction de la miniature embarquée des RAW via LibRaw (boîtiers
  récents) ; repli gdk-pixbuf pour les fichiers sans miniature (TIFF exporté) ;
  orientation EXIF appliquée aux fichiers non-RAW ; correction de la sauvegarde
  JPEG des vignettes avec effets.
- Nouvel effet « DynaComp » : compresseur de plage dynamique local
  (tone mapping bidirectionnel), dans l'onglet Effets.
- « Color scalpel » : courbes de teinte lissées (spline), atténuation des halos
  dans les hautes lumières, bande de travail rehaussée et raccourcis
  (Ctrl+clic ajouter, Ctrl+Maj+clic supprimer, clic droit remettre à plat).
- Correction d'objectif (lensfun) : correction du plantage de sélection, cases
  « Activer » / « Defish » fonctionnelles, bouton d'assignation toujours
  disponible (mode forcé) ; objectifs manuels : focale/ouverture invalides non
  transmises à lensfun.
- Styles (CaraStyles) : capture sélective de réglages et copier/coller entre
  photos, avec choix fin de ce qu'on garde (cases) et boutons dédiés.
- Barre du haut : boutons « Enfuse » (fusion d'expositions) et « GIMP »
  (ouverture avec orientation et effets), garde anti-plantage sur Enfuse.
- Bloc « Courbes » à 4 onglets (Valeur + courbes RVB par canal), courbes
  préréglées et enregistrer/charger/supprimer.
- Réglages de base : boutons Auto-exposition, Auto-niveaux et Réinitialiser.
- Balance des blancs : bouton « Masque d'exposition ».
- Recadrage : le bouton « OK » applique enfin le recadrage.
- Boîte à outils : boutons « Tout replier / Tout déplier » fixes au-dessus du
  défilement, sur les 3 onglets.
- Fenêtre : tient dans l'écran (onglets Effets/Tonalité défilants, bornage sur
  Wayland), maximisée au premier lancement (petits écrans).
- Export : durcissement PNG/JPEG/TIFF (un plantage devient un échec propre),
  respect du dossier de destination choisi, « Exporter sous » applique bien les
  effets CaraStudio.
- Fujifilm X-Trans (.RAF) : ouverture et développement (démosaïquage LibRaw).
- Pédagogie du pipeline : bouton « Pipeline » (légende des 5 étapes) et badges
  A–E de couleur sur les modules.
- Aide (F1) enrichie et illustrée (pipeline, DCP, correction d'objectif,
  Styles, DynaComp, Color scalpel), en français et en anglais.
- install.sh : initialisation des sous-modules Git, détection de distribution
  via /etc/os-release, dépendances apt complétées, compat GLib récente,
  enblend/enfuse optionnel.
- AppImage : LibRaw récente compilée depuis les sources (boîtiers modernes),
  construction sur Ubuntu 20.04 pour la portabilité glibc.

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
