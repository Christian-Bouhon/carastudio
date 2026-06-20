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

	if command -v dnf >/dev/null 2>&1; then
		say "Installation des dépendances (Fedora/dnf)…"
		$sudo dnf install -y \
			gcc gcc-c++ make autoconf automake libtool pkgconf-pkg-config gettext-devel \
			gtk3-devel glib2-devel libxml2-devel libX11-devel \
			libjpeg-turbo-devel libtiff-devel sqlite-devel lensfun-devel lcms2-devel \
			libgphoto2-devel exiv2-devel LibRaw-devel fftw-devel dbus-devel \
			enblend-enfuse || die "échec dnf"
	elif command -v apt-get >/dev/null 2>&1; then
		say "Installation des dépendances (Debian/Ubuntu/apt)…"
		$sudo apt-get update
		$sudo apt-get install -y \
			build-essential autoconf automake libtool pkg-config gettext \
			libgtk-3-dev libglib2.0-dev libxml2-dev libx11-dev \
			libjpeg-dev libtiff-dev libsqlite3-dev liblensfun-dev liblcms2-dev \
			libgphoto2-dev libexiv2-dev libraw-dev libfftw3-dev libdbus-1-dev \
			enblend || die "échec apt"
	elif command -v pacman >/dev/null 2>&1; then
		say "Installation des dépendances (Arch/pacman)…"
		$sudo pacman -S --needed --noconfirm \
			base-devel autoconf automake libtool pkgconf gettext \
			gtk3 glib2 libxml2 libx11 \
			libjpeg-turbo libtiff sqlite lensfun lcms2 libgphoto2 exiv2 libraw fftw dbus \
			enblend-enblend || die "échec pacman"
	else
		die "Gestionnaire de paquets non reconnu. Installez les dépendances à la main puis relancez avec --no-deps."
	fi
}

# --- Compilation --------------------------------------------------------------

[ "$INSTALL_DEPS" -eq 1 ] && install_deps || say "Dépendances ignorées (--no-deps)."

if [ ! -x ./configure ]; then
	say "Génération du script configure (autoreconf)…"
	autoreconf -fi || die "autoreconf a échoué"
fi

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
