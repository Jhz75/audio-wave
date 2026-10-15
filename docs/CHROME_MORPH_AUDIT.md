# CHROME MORPH — audit et prototype du 15 octobre 2026

Décision : conserver le plugin OBS natif et ajouter une source indépendante. Pour
une sculpture chrome dans une partie de l’image, une géométrie implicite animée
sur GPU apporte les divisions et les fusions recherchées sans simulation de fluide,
sans navigateur et sans transfert de frames par le CPU.

## Sources effectivement examinées

| Branche | Commit examiné | Rôle |
| --- | --- | --- |
| `main` | `50c99e51b9433680c5c8fee113346848e6340c70` | moteur original, inchangé |
| `backup-vfx-v1-8-rendu-clean-1-2026-09-13` | `898d835ea869ab5ff32bc2ab390f07cde444c7f6` | sauvegarde de référence |
| `audio-wave-vfx-v1` | `898d835ea869ab5ff32bc2ab390f07cde444c7f6` | identique à la sauvegarde |
| `vfx-material-engine-v2` | `40b682ffcdee694711bf94adb870760fc22ab1e9` | tentative PBR et textures |

Le prototype part du commit de sauvegarde, dans `chrome-morph-prototype-v1`.
L’implémentation historique `src/audio-shader-source.cpp`, son en-tête, les effets
existants et la branche Material Engine v2 restent inchangés. Deux raccords
ajoutent le nouveau fichier au build et enregistrent son nouveau type de source.

Attention à la portée de la sauvegarde : **Amber Chrome Cell Orb v3.2 HEARTBEAT
n’est pas présent dans les fichiers de cette branche**. Le moteur permettait de
charger des `.effect` externes. Le dépôt seul ne permet donc pas de reconstruire
fidèlement ce rendu antérieur. Les réglages « Rendu Clean 1 » retrouvés dans
l’historique étaient React −63 dB, Peak −20 dB, Attack 27 ms, Release 0 ms,
FFT 4096 et 64 bandes ; ils restent propres à l’ancien moteur.

## Ce qui mérite d’être conservé

- Capture d’une source audio OBS par `obs_source_add_audio_capture_callback`.
- Effets natifs HLSL/OBS, composition transparente et calcul GPU local.
- Réutilisation des buffers FFT dans VFX v1 ; un progrès réel sur les allocations.
- Compilation d’un effet candidat avec maintien du dernier effet valide dans VFX v1.
- Détection de beat courte séparée de l’Attack/Release générale en v1.8.

## Critique technique

| Constat dans les sources | Conséquence | Décision CHROME MORPH |
| --- | --- | --- |
| FFT et enveloppes calculées dans `source_render`, sous `render_mutex` | Le rythme d’analyse dépend du rendu ; travail répété possible si la source est rendue plusieurs fois | Analyser les échantillons dans le callback, publier uniquement des paramètres |
| Fenêtre FFT 4096 à 48 kHz | 85,3 ms d’historique ; ce chiffre n’est pas une mesure de latence totale, mais élargit les transitoires | Filtres IIR et publication toutes les ≈2,67 ms, sans FFT supplémentaire |
| Moyenne `(L+R)/2` avant mesure | Annulation sur signaux stéréo en opposition de phase | Filtrer chaque canal et additionner les puissances |
| Pics des 64 bandes réaffectés à des cellules pseudo-aléatoires | `audio_spectrum_texture` n’est plus un spectre ordonné ; la correspondance musique/mouvement devient difficile à lire | Quatre signaux explicites ; aucun remappage aléatoire |
| Allocation possible du ring dans le callback historique, verrou partagé avec le rendu | Variations de coût et contention possibles | États fixes, file SPSC bornée, aucun verrou applicatif ni allocation dans le callback |
| 16 sliders, 8 couleurs, seuils, FFT, macros et options PBR accumulés | Réglages couplés et calibration laborieuse | Source audio, trois qualités, une intensité ; mode démo explicite |
| V2 déforme une distance radiale `length(q)-radius` | Bonne exploration de matière ; ne fournit pas une vraie architecture de divisions/fusions | Union lissée de quatre volumes et soustraction d’une ouverture |
| Quatre octaves de bruit 3D à huit coins dans chaque évaluation SDF V2, puis six évaluations pour la normale | Coût multiplié par les pas de raymarching et par le nombre de pixels | Déformation large simple, détails fins à l’intersection seulement, normale en quatre évaluations |
| Six textures, triplanaire, height et qualité Ultra | Coût/complexité peu justifiés pour du chrome lisse ; height dans la boucle peut ajouter beaucoup de lectures | Reflets de studio procéduraux et iridescence angulaire sans textures externes |
| V2 atténue l’environnement avec la rugosité sans préfiltrage réel | Ce n’est pas une réflexion PBR rugueuse physiquement cohérente | Assumer un métal stylisé cohérent plutôt que qualifier le rendu de PBR complet |
| Chargement V2 par images ordinaires ; filtre de fichiers sans HDR/EXR | « Environment map » ne garantit pas un véritable pipeline HDRI | Prototype SDR ; ne pas annoncer de HDR ou de ray tracing matériel |
| Temps monotone absolu converti en `float` dans l’ancien shader | Précision temporelle qui baisse avec l’uptime | Horloge locale intégrée à la source |

La RTX 3080 Ti ne dispense pas de garder de la marge pour OBS et éventuellement un
jeu. Le nombre de pixels calculés est ici un levier plus utile qu’un catalogue
de textures ou de réglages. Le prototype ne fait pas appel aux cœurs RT : le
raymarching SDF s’exécute dans un pixel shader classique.

## Architecture développée

1. OBS fournit le son de la source DJ sélectionnée par son UUID (les renommages
   n’imposent pas de la sélectionner à nouveau).
2. Quatre filtres passe-bande, constitués de sections Butterworth en cascade,
   mesurent l’énergie : kick 40–140 Hz, basses 30–220 Hz, médiums 220–2800 Hz,
   aigus 2800–14000 Hz. Les limites hautes respectent la fréquence d’échantillonnage.
3. La mesure stéréo utilise l’énergie des canaux. Les états sont préalloués pour
   jusqu’à huit canaux ; mono/stéréo sont le cas principal et celui validé.
4. Référence de niveau adaptative, seuil de bruit fixe, attaques et relâchements
   prédéfinis évitent les seuils manuels React/Peak. Le détecteur de kick compare
   une enveloppe rapide à un fond lent, avec 130 ms de réfractaire.
5. Une file SPSC de paramètres relie l’audio à la vidéo. Si pleine, elle refuse
   les nouvelles publications plutôt que bloquer l’audio. Le lecteur vide la file
   et rejette les impacts trop anciens ; le compteur d’événements est cumulatif.
6. `video_tick` lit le signal récent, amortit les pertes d’audio et avance la
   morphologie. Les divisions/fusions ont un cycle lent **modulé par l’énergie**,
   pas une division complète à chaque kick ni une synchronisation au BPM.
7. Un raymarching SDF avec sphère englobante calcule la sculpture dans une texture
   carrée RGBA locale. Les reflets et les détails sont calculés une fois la surface trouvée.
8. La texture, à alpha prémultiplié, est composée par OBS. Le calcul coûteux est
   mis en cache jusqu’au prochain tick vidéo ; aucun readback GPU en production.
9. Masquer la source détache la capture ; une source audio supprimée déclenche
   une attente/reconnexion. La dernière réaction décroît si aucun bloc récent n’arrive.

| Signal | Réaction visible |
| --- | --- |
| Kick | Compression verticale brève, attaque distincte |
| Basses | Étirement de la masse et permission de séparation |
| Médiums | Ouverture traversante et vitesse d’évolution organique |
| Aigus | Petites ondulations de normale et nuance d’iridescence |

## Budgets et réglage de départ

| Profil | Texture interne | Limite de pas | Mémoire de la texture RGBA seule |
| --- | --- | --- | --- |
| Économe | 576 × 576 | 64 | 1,27 Mio |
| Équilibré, défaut | 768 × 768 | 88 | 2,25 Mio |
| Détaillé | 1024 × 1024 | 112 | 4 Mio |

À 768², le shader traite 71,6 % de pixels de moins qu’en 1920×1080 ; cela **ne
signifie pas** une baisse identique du temps GPU. Les rayons hors de la sphère
englobante sont éliminés avant la boucle. La zone de sortie reste fixe et la
sculpture est contenue dans cette zone. Déplacer/redimensionner la source dans
OBS détermine son emplacement à l’écran.

Pour Ryzen 9 5900X / RTX 3080 Ti / 32 Go : commencer en Équilibré, intensité 1,
OBS 60 i/s. Passer en Détaillé seulement si la taille réelle à l’écran l’exige
et si les statistiques OBS conservent assez de marge avec le set complet.

Ne pas annoncer de latence « 2,67 ms » : c’est la période de publication de
l’analyse. Les filtres ont un délai de groupe, les blocs audio du périphérique
et OBS ajoutent leur délai, puis l’affichage attend une frame. Les offsets de
synchro et filtres de retard OBS doivent également être pris en compte.

## Vérifications et limites

Voir [CHROME_MORPH_VALIDATION.md](CHROME_MORPH_VALIDATION.md) pour les résultats
mesurés, et [CHROME_MORPH_INSTALLATION.md](CHROME_MORPH_INSTALLATION.md) pour
installer et comparer sans remplacer une scène de production.

Le rendu est un **prototype fonctionnel de sculpture procédurale**, pas une
simulation de mercure. Les reflets représentent un studio virtuel, pas les objets
présents dans la scène OBS. La division ne conserve pas physiquement le volume.
L’iridescence est artistique, pas un solveur spectral de couche mince. Les
ombres entre lobes et leurs réflexions mutuelles ne sont pas simulées. Ces choix
bornent le coût ; une amélioration visuelle ultérieure doit être jugée sur le
gain effectivement visible dans le stream.

Une basse très percussive peut déclencher le détecteur de kick : ce n’est pas
une séparation d’instruments. L’autocalibration reste limitée par le seuil de
bruit et par un signal source extrêmement faible ou écrêté. Aucun morceau réel
du set de l’utilisateur n’a été fourni pour cette première calibration.

Le prototype cible SDR. HDR, rendu sur la RTX 3080 Ti, latence de bout en bout
et compatibilité avec la version OBS installée chez l’utilisateur nécessitent
une validation sur sa machine. Les tests logiciels ou un build Windows réussi
ne remplacent pas cette mesure.

Références primaires : [rendu natif OBS](https://docs.obsproject.com/graphics),
[sources et capture audio](https://docs.obsproject.com/reference-sources),
[effets OBS](https://docs.obsproject.com/reference-libobs-graphics-effects),
et les sources libobs 31.1.1, notamment `obs-source.c` (sérialisation des callbacks).
