# Validation CHROME MORPH — 15 octobre 2026

## Résultats locaux

| Vérification exécutée | Résultat |
| --- | --- |
| Build CMake du plugin complet, libobs 30.0.2 Ubuntu, GCC 13.3 | Réussi |
| Compilation de la nouvelle source contre les en-têtes OBS 31.1.1, `-Wall -Wextra -Werror` | Réussie |
| Tests DSP mono/stéréo (principalement stéréo), 44,1 / 48 / 96 kHz | Réussis |
| Sons isolés 65 / 1100 / 8200 Hz : dominance de la bonne bande | Réussie aux trois fréquences d’échantillonnage |
| Stéréo en opposition de phase | Énergie conservée, identique au signal en phase |
| Blocs 73 et 480 échantillons | Résultats identiques à la précision testée |
| Gain d’entrée 0,015 / 0,08 / 0,4 | Réaction utile, sans saturation permanente |
| Train de kicks avec rumble de fond, 8 secondes à 120 / 150 / 190 BPM | 16/16, 20/20 et 26/26 détections |
| Première détection sur ces kicks synthétiques | 5,33 ms à 48 kHz ; mesure algorithmique sur ce signal, pas latence de bout en bout |
| Basse soutenue et ton aigu isolé | Pas de train de faux kicks ; zéro kick sur le ton aigu |
| Silence, entrée muted, échantillon non fini | Retour au repos et pas de valeur non finie publiée |
| File SPSC : saturation, ordre, 100 000 publications concurrentes | Réussis |
| ASan et UBSan sur DSP et file | Réussis ; LeakSanitizer désactivé localement à cause de l’accès `/proc` limité du runtime |
| Évolution de la morphologie et retour au repos | Séparation >0,95 atteinte puis retour <0,0001 |
| Chargement du module et compilation du `.effect` **par libobs/OpenGL** | Réussis |
| Rendu natif dans OBS, lecture RGBA de contrôle | 82 177 pixels opaques et 507 463 transparents dans le snapshot 768² |
| Type historique Audio Shader Engine encore enregistré | Vérifié |
| Changement de qualité d’une source active | Vérifié après application différée par OBS ; la zone logique reste 768² |
| Capture native d’une source OBS synthétique, sélection par UUID | Vérifiée, dernier bloc âgé de 8 ms dans le snapshot de test |
| Source muette/arrêt de publication et détection de signal périmé | Arrêt de publication vérifié en intégration ; mise en sourdine vérifiée dans les tests DSP |
| Masquage et destruction | Vérifiés, arrêt propre de la capture et déchargement du module |

Le benchmark DSP optimisé a traité 10 secondes de stéréo en **33,9 ms** sur
l’hôte de test Xeon, sans sanitizers. C’est un repère de coût CPU sur cet hôte,
pas une mesure sur le Ryzen 9 5900X ni le coût total d’OBS.

## Contrôle visuel et topologique

`tools/render-chrome-morph.py` traduit uniquement l’arithmétique du fichier
`.effect` en GLSL pour un contrôle reproductible hors OBS ; il ne remplace pas
le test natif ci-dessus. Rendu vérifié sur Mesa llvmpipe, en calcul logiciel.

- Repos et masse déformée : un volume connecté dans les états examinés.
- Ouverture : le centre laisse réellement voir le fond.
- Division : quatre composantes séparées dans l’état examiné, puis fusion
  continue via le même champ de distance.
- Pixels opaques, transparents et partiellement transparents présents.
- RGB prémultiplié et zone de bord entièrement transparente vérifiés.
- Dix-huit états supplémentaires aux trois qualités, deux rotations extrêmes,
  trois séparations et intensité maximale : absence de découpe aux bords vérifiée.
- La planche [CHROME_MORPH_preview.png](CHROME_MORPH_preview.png) montre les
  pixels du shader, composés sur un damier ; ce n’est pas une image générée.
- [CHROME_MORPH_demo.mp4](CHROME_MORPH_demo.mp4) force une séquence de formes pour
  les montrer rapidement. Elle est étiquetée comme démo sans audio réel.

Les temps d’un rendu llvmpipe varient selon compilation et charge du runtime.
Ils ne permettent pas d’estimer les ms GPU d’une RTX 3080 Ti.

## À vérifier sous Windows et sur le PC utilisateur

Le workflow dédié compile le pixel shader avec le compilateur HLSL Windows,
puis le plugin x64 avec les outils existants du dépôt et prépare un ZIP de test.
L’état effectif du run GitHub fait foi ; le dossier de sources seul n’est pas
un binaire Windows validé.

Restent à mesurer sur le PC cible : coût GPU 3D en scène complète, tenue à 60 i/s,
latence du matériel et de la chaîne OBS, réaction sur un set réel dense, et
contours à la taille finale du stream. Les tests de topologie portent sur des
états représentatifs, pas sur toutes les configurations possibles. Le pipeline
HDR n’est pas couvert.

## Reproduction

```sh
c++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread tests/chrome-morph-dsp-test.cpp -o /tmp/chrome-test
/tmp/chrome-test
python tools/render-chrome-morph.py --output /tmp/chrome-qa --animation
```

Le workflow documente également les commandes du test natif libobs/Xvfb et du
build Windows. Les lectures GPU présentes dans le test sont uniquement des
contrôles ; le plugin n’effectue pas ces copies en fonctionnement normal.
