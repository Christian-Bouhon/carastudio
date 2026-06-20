<img src="pixmaps/carastudio-logo.png" alt="CaraStudio" width="200"/>

# CaraStudio

**CaraStudio** est un convertisseur RAW open-source pour Linux, fork de [RawStudio](https://github.com/rawstudio/rawstudio), développé et maintenu par [Carafife](https://github.com/carafife).

CaraStudio permet de lire, ajuster et convertir les fichiers RAW de votre appareil photo numérique en JPEG, PNG ou TIFF, avec une interface sombre professionnelle inspirée des logiciels de retouche modernes.

---

## Fonctionnalités

- Interface GTK3 sombre et ergonomique
- Support complet des profils couleur DNG (Color Profile)
- Traitement par lot
- Réglages post-capture : balance des blancs, saturation, exposition, courbes…
- Copier/coller les réglages entre images
- Correction automatique de la distorsion (Lensfun)
- Réduction du bruit avancée
- Correction des aberrations chromatiques et du vignettage
- Masque d'exposition, recadrage, redressement
- Mode plein écran et support multi-moniteur
- Traitement multithread optimisé SSE/SSE2
- Nommage automatique des fichiers basé sur les données EXIF

---

## Installation

### Méthode simple — script automatique

Clonez le dépôt et lancez le script d'installation. Il détecte votre
distribution (**Fedora**/dnf, **Debian·Ubuntu**/apt, **Arch**/pacman),
installe les dépendances, compile et installe CaraStudio dans `~/.local` :

```bash
git clone https://github.com/carafife/carastudio.git
cd carastudio
./install.sh
```

Puis lancez :

```bash
carastudio
```

Options : `./install.sh --prefix=/usr` (installation système, root requis) ·
`./install.sh --no-deps` (sauter l'installation des dépendances).

---

## Construction manuelle

Si vous préférez compiler à la main (ou packager CaraStudio).

### Dépendances (Fedora)

```bash
sudo dnf install gcc gcc-c++ autoconf automake libtool make gettext-devel \
    gtk3-devel glib2-devel libxml2-devel libX11-devel sqlite-devel \
    lensfun-devel lcms2-devel libgphoto2-devel exiv2-devel LibRaw-devel \
    libjpeg-turbo-devel libtiff-devel fftw-devel dbus-devel
```

(Sur Debian/Ubuntu et Arch, voir les équivalents dans `install.sh`.)

### Compiler et installer (dans ~/.local)

```bash
./autogen.sh --prefix="$HOME/.local"
make -j$(nproc)
make install
```

> **Débogage** — pour une build instrumentée AddressSanitizer (préfixe isolé) :
> `./autogen.sh --prefix=/tmp/casan CFLAGS="-fsanitize=address -g -O1 -fno-omit-frame-pointer" CXXFLAGS="-fsanitize=address -g -O1 -fno-omit-frame-pointer" LDFLAGS="-fsanitize=address"`
> puis lancez avec `LD_LIBRARY_PATH=/tmp/casan/lib ASAN_OPTIONS="detect_leaks=0" /tmp/casan/bin/carastudio`.

### Lancer

```bash
carastudio
```

---

## À propos du fork

CaraStudio est un fork de RawStudio (© Anders Brander, Anders Kvist, Klaus Post), distribué sous licence **GNU GPL v3 ou ultérieure**.

Les modifications apportées par Carafife incluent notamment :
- Portage et corrections pour Fedora 44 / Wayland / GTK3
- Thème sombre professionnel
- Corrections de crashes (use-after-free DCP, ROI hors limites)
- Amélioration du navigateur et du zoom
- Renommage et rebranding CaraStudio
