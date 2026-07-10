#!/usr/bin/env bash
#
# CaraStudio — script d'installation depuis les sources.
#
# Usage :
#   ./install.sh                 # installe les dépendances + compile + installe dans ~/.local
#   ./install.sh --prefix=/usr   # installe dans un autre préfixe (root requis pour /usr)
#   ./install.sh --no-deps       # saute l'installation des dépendances (déjà présentes)
#
# Distributions reconnues pour les dépendances : Fedora/RHEL (dnf),
# Debian/Ubuntu (apt), Arch (pacman). Sur les autres, installez les
# équivalents à la main puis relancez avec --no-deps.

set -euo pipefail

PREFIX="$HOME/.local"
INSTALL_DEPS=1
JOBS="$(nproc 2>/dev/null || echo 2)"

for arg in "$@"; do
	case "$arg" in
		--prefix=*) PREFIX="${arg#*=}" ;;
		--no-deps)  INSTALL_DEPS=0 ;;
		-h|--help)
			grep '^#' "$0" | sed 's/^# \{0,1\}//'
			exit 0 ;;
		*) echo "Option inconnue : $arg" >&2; exit 1 ;;
	esac
done

cd "$(dirname "$0")"

say() { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31mErreur : %s\033[0m\n' "$*" >&2; exit 1; }

# --- Dépendances --------------------------------------------------------------

install_deps() {
	local sudo=""
	[ "$(id -u)" -ne 0 ] && sudo="sudo"

	# Détecter la FAMILLE de distribution via /etc/os-release (ID + ID_LIKE) plutôt
	# que la simple présence d'une commande : certains systèmes Debian/Ubuntu ont
	# dnf installé sans être Fedora, ce qui faisait choisir à tort le chemin dnf
	# (« pas de dépôts activés / impossible de détecter le numéro de version »).
	local osid="" oslike=""
	if [ -r /etc/os-release ]; then
		osid=$(. /etc/os-release 2>/dev/null; echo "${ID:-}")
		oslike=$(. /etc/os-release 2>/dev/null; echo "${ID_LIKE:-}")
	fi

	local mgr=""
	case " $osid $oslike " in
		*fedora*|*rhel*|*centos*|*rocky*|*almalinux*) mgr="dnf" ;;
		*debian*|*ubuntu*|*mint*|*pop*|*elementary*)  mgr="apt" ;;
		*arch*|*manjaro*|*endeavour*)                 mgr="pacman" ;;
	esac

	# Repli si os-release muet : détection par commande (apt AVANT dnf, car un
	# vrai Fedora n'a pas apt alors qu'un Debian peut avoir dnf installé).
	if [ -z "$mgr" ]; then
		if   command -v apt-get >/dev/null 2>&1; then mgr="apt"
		elif command -v dnf     >/dev/null 2>&1; then mgr="dnf"
		elif command -v pacman  >/dev/null 2>&1; then mgr="pacman"
		fi
	fi

	case "$mgr" in
		dnf)
			say "Installation des dépendances (Fedora/dnf)…"
			$sudo dnf install -y \
				gcc gcc-c++ make autoconf automake libtool pkgconf-pkg-config gettext-devel \
				gtk3-devel glib2-devel libxml2-devel libX11-devel \
				libjpeg-turbo-devel libtiff-devel sqlite-devel lensfun-devel lcms2-devel \
				libgphoto2-devel exiv2-devel LibRaw-devel fftw-devel dbus-devel \
				|| die "échec dnf"
			# enblend/enfuse = dépendance RUNTIME optionnelle (fonction Enfuse), pas
			# nécessaire à la compilation → best-effort, on ne bloque pas si absent.
			$sudo dnf install -y enblend-enfuse 2>/dev/null \
				|| say "enblend/enfuse non installé (fonction Enfuse indisponible — optionnel)."
			;;
		apt)
			say "Installation des dépendances (Debian/Ubuntu/apt)…"
			$sudo apt-get update
			$sudo apt-get install -y \
				build-essential autoconf automake libtool pkg-config gettext autopoint intltool \
				libgtk-3-dev libglib2.0-dev libxml2-dev libx11-dev \
				libjpeg-dev libtiff-dev libsqlite3-dev liblensfun-dev liblcms2-dev \
				libgphoto2-dev libexiv2-dev libraw-dev libfftw3-dev libdbus-1-dev \
				|| die "échec apt"
			$sudo apt-get install -y enblend enfuse 2>/dev/null \
				|| say "enblend/enfuse non installé (fonction Enfuse indisponible — optionnel)."
			;;
		pacman)
			say "Installation des dépendances (Arch/pacman)…"
			$sudo pacman -S --needed --noconfirm \
				base-devel autoconf automake libtool pkgconf gettext \
				gtk3 glib2 libxml2 libx11 \
				libjpeg-turbo libtiff sqlite lensfun lcms2 libgphoto2 exiv2 libraw fftw dbus \
				|| die "échec pacman"
			$sudo pacman -S --needed --noconfirm enblend-enfuse 2>/dev/null \
				|| say "enblend-enfuse non installé (fonction Enfuse indisponible — optionnel)."
			;;
		*)
			die "Gestionnaire de paquets non reconnu. Installez les dépendances à la main puis relancez avec --no-deps."
			;;
	esac
}

# --- Compilation --------------------------------------------------------------

[ "$INSTALL_DEPS" -eq 1 ] && install_deps || say "Dépendances ignorées (--no-deps)."

# Sous-modules Git (rawspeed). Un « git clone » simple, sans --recursive, laisse
# plugins/load-rawspeed/rawspeed/ VIDE → la compilation du plugin load-rawspeed
# s'arrête sur « StdAfx.h: Aucun fichier ou dossier de ce nom ». Si on est dans un
# dépôt git pourvu de sous-modules, on les initialise (opération idempotente). Un
# tarball de dist n'a ni .git ni .gitmodules → cette étape est simplement sautée.
if [ -e .git ] && [ -f .gitmodules ] && command -v git >/dev/null 2>&1; then
	say "Initialisation des sous-modules Git (rawspeed)…"
	git submodule update --init --recursive \
		|| die "échec de l'initialisation des sous-modules (git submodule update --init --recursive)"
fi

# On (re)génère si configure manque OU si les fichiers auxiliaires (build-aux/)
# ne sont pas là — cas d'un clone git où un autoreconf précédent a été interrompu
# (ex. autopoint manquant) : un configure a pu rester sans que build-aux/ soit
# peuplé, ce qui fait échouer configure avec « cannot find required auxiliary
# files ». Un tarball de dist, lui, embarque configure ET build-aux → on saute.
if [ ! -x ./configure ] || [ ! -f build-aux/install-sh ]; then
	say "Génération du script configure (autoreconf)…"
	rm -f configure
	autoreconf -fi || die "autoreconf a échoué"
fi

# CaraStudio : désactiver les vérifications de cast de la GLib. Avec les GLib
# récentes (Ubuntu notamment), les macros de cast GObject font échouer la
# compilation avec « too few arguments to function '..._get_type' » (le plugin
# effects tombe en premier). Le flag règle ça sans impact fonctionnel (seul un
# contrôle de type au runtime est désactivé) ; c'est déjà ce que fait le build
# de l'AppImage.
export CFLAGS="${CFLAGS:--O2} -DG_DISABLE_CAST_CHECKS"
export CXXFLAGS="${CXXFLAGS:--O2} -DG_DISABLE_CAST_CHECKS"

say "Configuration (préfixe : $PREFIX)…"
./configure --prefix="$PREFIX" || die "configure a échoué"

say "Compilation ($JOBS tâches)…"
make -j"$JOBS" || die "la compilation a échoué"

say "Installation…"
if [ -w "$PREFIX" ] || [ "$(id -u)" -eq 0 ]; then
	make install
else
	sudo make install
fi

# --- Fin ----------------------------------------------------------------------

BIN="$PREFIX/bin/carastudio"
say "Terminé ! CaraStudio est installé : $BIN"
case ":$PATH:" in
	*":$PREFIX/bin:"*) printf 'Lancez simplement : \033[1mcarastudio\033[0m\n' ;;
	*) printf 'Ajoutez à votre PATH (\033[1m%s/bin\033[0m) ou lancez : \033[1m%s\033[0m\n' "$PREFIX" "$BIN" ;;
esac
