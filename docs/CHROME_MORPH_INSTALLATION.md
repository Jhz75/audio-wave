# Tester CHROME MORPH dans OBS sous Windows

Le prototype ajoute **CHROME MORPH · Prototype** au plugin Audio Wave. Les sources
Audio Shader Engine déjà présentes dans les scènes gardent leur type et leurs
réglages. La branche de travail est `chrome-morph-prototype-v1`.

## Obtenir le plugin

Dans GitHub → Actions → **CHROME MORPH prototype**, ouvrir un run réussi sur
cette branche, puis télécharger l’artifact **chrome-morph-windows-x64**. Il
contient l’archive Windows avec `obs-plugins/64bit/audio-wave.dll` et
`data/obs-plugins/audio-wave/`, dont `effects/chrome-morph.effect`.

Un fichier `.effect` seul n’installe pas le nouveau type de source : il faut
le plugin compilé correspondant. Ne pas présenter l’archive de sources comme
un plugin Windows prêt à installer.

## Essai isolé recommandé

1. Faire une copie de l’installation OBS ou utiliser une installation OBS
   portable de test. Reprendre les paramètres audio et la capture DJ nécessaires.
2. OBS fermé, extraire le contenu de l’archive Windows à la racine de cette
   installation, à côté de `bin`, `data` et `obs-plugins`.
3. Ouvrir OBS et une scène de test. Ajouter une source → **CHROME MORPH · Prototype**.
4. Choisir **Source audio du set DJ**, **Équilibré**, **Intensité 1**.
5. Garder **Démonstration sans audio** désactivé pour le set. L’activer seulement
   pour voir les transformations sans son ; la démo n’est pas de l’analyse audio.
6. Redimensionner et placer la source dans la zone souhaitée du stream. Le fond
   est transparent : aucun chroma key ni mode de fusion spécial à ajouter.
7. Mettre derrière une couleur claire puis sombre pour vérifier les contours.

Pour tester dans l’OBS habituel, fermer OBS et conserver une copie de l’ancienne
DLL **et de tout son dossier de données** avant remplacement. Un ajout de type
de source n’empêche pas que le module Audio Wave complet soit remplacé sur disque.
L’installation portable évite ce remplacement pour le premier essai.

## Calibration automatique

Pas de seuil React/Peak, FFT, couleurs ou macros à régler. L’adaptation agit sur
les mouvements visuels et ne modifie pas le signal audio envoyé au stream.
Choisir l’entrée du contrôleur DJ évite que la voix, les notifications ou les
autres sons du PC pilotent la sculpture. La capture utilise le signal brut fourni
par OBS ; un offset de synchronisation audio ne constitue pas un délai visuel
automatique de CHROME MORPH.

Si la source vient d’être créée après CHROME MORPH, rouvrir les propriétés pour
rafraîchir la liste. Les sources sont identifiées par UUID, ce qui conserve le
lien lors d’un renommage. Le texte d’état est une photographie au moment de
l’ouverture des propriétés, pas un oscilloscope actualisé en continu.

Le silence laisse une dérive lente de la matière ; les réactions sonores et les
séparations reviennent progressivement au repos. Une source mise en sourdine ne
doit pas continuer à provoquer de nouveaux impacts.

## Vérifier le coût et le délai sur le PC de stream

1. Ouvrir Affichage → Statistiques, avec le set, la caméra et l’encodage habituels.
2. Enregistrer 60 secondes source masquée, puis 60 secondes source visible,
   avec les mêmes autres éléments. Noter temps moyen de rendu et images perdues
   par retard de rendu, ainsi que GPU 3D dans le Gestionnaire des tâches.
3. À 60 i/s, le budget total d’une frame est de 16,67 ms. Garder une marge
   suffisante ; l’objectif initial du module est de rester dans quelques ms GPU,
   **objectif non mesuré sur la 3080 Ti à ce stade**.
4. Rejouer un enregistrement contenant des kicks isolés, puis le passage dense
   du set. Comparer l’attaque audio à la compression de la sculpture dans un
   enregistrement local, sans confondre analyse et démo.
5. Tester une pause, une mise en sourdine, un masquage de source, un renommage
   audio et une réouverture OBS. Repasser en Économe si la scène complète approche
   sa limite ; Détaillé sert à une sculpture affichée plus grande.

## Retour à l’état précédent

Dans une installation portable, fermer l’OBS de test suffit pour reprendre
l’installation habituelle. Si le module de l’installation principale a été
remplacé, fermer OBS et remettre ensemble sa DLL et son dossier de données
sauvegardés. Les sources historiques conservent leurs paramètres. Une source
CHROME MORPH reste inconnue pour l’ancienne DLL et doit rester dans la scène de
test tant que le prototype n’est pas adopté.

## Compiler soi-même

Le workflow utilise le SDK et les scripts déjà présents dans le dépôt :

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
```

Il faut Visual Studio 2022 (C++), le SDK Windows demandé dans `CMakePresets.json`
et CMake récent (le format de presets est version 8). Le build doit inclure
`src/chrome-morph-source.cpp` et installer le nouveau `.effect`.
