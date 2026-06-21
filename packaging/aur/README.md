# Paquet AUR (Arch Linux)

Recette pour publier CaraStudio sur l'[AUR](https://aur.archlinux.org/).

## Pré-requis : une *Release* GitHub

Le `PKGBUILD` télécharge le **tarball de release** :

```
https://github.com/carafife/carastudio/releases/download/carastudio-1.0/carastudio-1.0.tar.gz
```

Il faut donc d'abord créer la Release `carastudio-1.0` sur GitHub et y
**attacher le tarball** produit par `make dist` (`carastudio-1.0.tar.gz`).
Le `sha256sums` du `PKGBUILD` doit correspondre à ce fichier exact
(`sha256sum carastudio-1.0.tar.gz`). Si vous régénérez le tarball, mettez
à jour le `sha256sums`.

## Publier sur l'AUR

Nécessite un compte [AUR](https://aur.archlinux.org/) avec une clé SSH
enregistrée.

```bash
# 1. Cloner le dépôt AUR (vide au départ) du nouveau paquet
git clone ssh://aur@aur.archlinux.org/carastudio.git
cd carastudio

# 2. Copier la recette
cp /chemin/vers/packaging/aur/PKGBUILD .
cp /chemin/vers/packaging/aur/.SRCINFO .
# (ou régénérer : makepkg --printsrcinfo > .SRCINFO  — sur une machine Arch)

# 3. Tester (sur Arch) puis publier
makepkg -si            # construit et installe localement pour vérifier
git add PKGBUILD .SRCINFO
git commit -m "Initial import: carastudio 1.0"
git push
```

Les utilisateurs installeront ensuite avec un assistant AUR :

```bash
yay -S carastudio      # ou : paru -S carastudio
```

## Mises à jour

À chaque nouvelle version : bump `pkgver`/`pkgrel`, nouvelle Release GitHub,
mettre à jour `sha256sums`, régénérer `.SRCINFO`, commit + push.
