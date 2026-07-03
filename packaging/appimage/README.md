# AppImage CaraStudio

AppImage = **un seul fichier téléchargeable**, sans installation : on le rend
exécutable et on le lance. Fonctionne sur la plupart des distributions Linux
récentes (glibc ≥ 2.39).

## Construire

```bash
git submodule update --init          # rawspeed (une fois)
packaging/appimage/build-appimage.sh # → ./CaraStudio-x86_64.AppImage
```

Prérequis hôte : `podman`, `git`, `curl` (réseau requis). Rien n'est installé
sur la machine : tout se fait dans un conteneur **Ubuntu 24.04** jetable.

## Pourquoi Ubuntu 24.04 et pas la machine ?

La pile GTK3 de Fedora 44 embarque des composants trop récents
(glycin, tinysparql, cloudproviders…) qui **plantent à l'initialisation**
une fois empaquetés dans une AppImage. Ubuntu 24.04 offre une pile GTK3
stable et « bundlable », et une glibc assez ancienne pour une bonne
portabilité. Le script gère automatiquement les points délicats :

- sous-module **rawspeed** (absent de `git archive`) ;
- `install.sh` retiré (casse `automake` sur Ubuntu) ;
- `-DG_DISABLE_CAST_CHECKS` (casts glib des macros `RS_TYPE_*`) ;
- **exiv2 0.27** géré par `librawstudio/rs-exiv2-compat.h` ;
- libs système-couplées (`libblkid`, `libmount`, `libselinux`, `libsystemd`,
  `libudev`) retirées du bundle ;
- hook `AppRun` réglant `LD_LIBRARY_PATH` (plugins) + base **lensfun**.

## Publier

Déposer le `.AppImage` produit comme *asset* d'une Release GitHub :
lien de téléchargement direct pour tout le monde.
