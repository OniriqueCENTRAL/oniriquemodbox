# Onirique ModBox

Modulation à ligne à retard : flanger, chorus BBD, dimension. Une seule ligne,
de une à huit voix, qui va du numérique chirurgical au circuit analogique lofi,
et de l'effet imperceptible au mur de son.

VST3 (Windows, macOS, Linux) et Audio Unit (macOS). Entrée mono ou stéréo,
sortie stéréo. Aucune latence.

---

## État du projet

| Partie | État |
|---|---|
| Moteur (ligne à retard sinc, circuit BBD, voix, crossover, deux LFO) | fait, testé hors ligne |
| A/B, presets stock et utilisateur, état sauvegardé | fait, testé hors ligne |
| Plugin VST3, paramètres automatisables | compile sous Windows |
| Interface | écrite, vérifiée sur capture (`modbox_snapshot`) |
| Build macOS (VST3 + AU) | workflow prêt, **jamais lancé** : pas encore de dépôt GitHub |
| Écoute dans un DAW | **pas encore faite** |

Le moteur a été mesuré sur des signaux de synthèse, pas écouté. Les réglages par
défaut et les presets stock ont été posés par raisonnement : ils restent à
ajuster à l'oreille. « ModBox » est un nom provisoire.

---

## Construire

JUCE 9.0.3 est cherché dans `JUCE/`, puis dans `../PitchBox/JUCE` : les plugins
de la gamme partagent la même copie.

```bash
cmake -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Le plugin sort dans
`build/OniriqueModBox_artefacts/Release/VST3/ModBox.vst3` ; à copier dans
`C:\Program Files\Common Files\VST3`.

Outil de développement, sans hôte :

```bash
cmake --build build --config Release --target modbox_snapshot
build/Release/modbox_snapshot.exe snapshot.png "Wall of Sound"   # capture de l'interface, sur un preset
build/Release/modbox_snapshot.exe --selftest                     # A/B, presets, état, mono et stéréo
```

---

## Utilisation

**Partout** : le pointeur laissé immobile deux secondes sur un contrôle en fait
apparaître la description, en français.

**L'écran** montre, en haut, ce que le plugin fait au spectre à cet instant (une
courbe par côté) et, en bas, le retard de chaque voix, la gauche au-dessus de la
droite.

### Circuit — du numérique à l'analogique

| Contrôle | Rôle |
|---|---|
| **Circuit** | À 0 : ligne à retard parfaite (lecture par sinc fenêtré), aucun bruit, réponse plate, feedback neutre. À 100 : circuit BBD. Entre les deux, un mélange continu. |
| **Drive** | Saturation douce (tanh) de ce qui entre dans la ligne, feedback compris. |
| **Drift** | Dérive de l'horloge, propre à chaque voix : lente (wow, sous 1 Hz) et rapide (flutter, quelques Hz). |
| **Noise** | Souffle. À 0, aucun. |

Drive, Drift, Noise et Stages ne comptent qu'à mesure que Circuit monte ; à 0,
ils sont dessinés en retrait.

### Density — du fin au gras

| Contrôle | Rôle |
|---|---|
| **Voices** | De 1 à 8 voix sur la ligne. Leurs LFO sont répartis sur le cycle (2 voix : en opposition, comme une Dimension). |
| **Spread** | Écart entre les retards des voix. |
| **Stages** | Étages du BBD : 256 à 4096. Le filtre passe-bas résonant du circuit est placé au tiers de l'horloge, `étages / (2 × retard)` : il se ferme quand le retard s'allonge, et plus tôt avec moins d'étages. |
| **Bass Keep** | Crossover (Linkwitz-Riley, 24 dB/octave) : sous cette fréquence, le son contourne l'effet. Tout en bas : Off. |

### Modulation

| Contrôle | Rôle |
|---|---|
| **Rate / Depth** | Vitesse et amplitude du balayage autour du retard choisi. |
| **Stereo** | Avance du LFO droit sur le gauche, de 0° (mono, centré) à 180° (côtés opposés). |
| **Rate 2 / LFO 2** | Un second LFO, et la part du balayage qu'il prend. |
| **Shape** | Sinus ou triangle. |

### Intensity

| Contrôle | Rôle |
|---|---|
| **Delay** | Retard au centre du balayage, de 0,3 à 50 ms. |
| **Feedback** | De −100 % (phase inversée : creusé, liquide) à +100 % (métallique, tranchant). La boucle reste stable aux deux bouts. |
| **Cross** | Part du feedback qui change de côté. |
| **Mix** | Entrée seule à effet seul, à puissance constante. |

### Texture — ce qui salit les voix

Appliqué aux voix une fois lues, hors de la boucle de feedback ; le son direct
reste propre. Chacun des trois est entièrement hors circuit à 0.

| Contrôle | Rôle |
|---|---|
| **Crush** | Bit crusher : de 16 à 3 bits. |
| **Downsample** | Fréquence d'échantillonnage abaissée de 40 kHz à 300 Hz (échantillonneur-bloqueur, avec son repliement). |
| **Distortion** | Saturation franche (jusqu'à 36 dB dans une tanh), quel que soit le circuit. |

### Sortie

| Contrôle | Rôle |
|---|---|
| **Auto Gain** | Ramène la sortie au niveau de l'entrée, jusqu'à 12 dB dans un sens ou dans l'autre : les deux niveaux sont suivis sur 0,4 s, avant le réglage Out. Il écoute même éteint, pour être juste dès qu'on l'allume, et ne bouge pas pendant un silence. |
| **Out** | Le niveau de tout ce qui sort, par-dessus l'auto gain. |

### Presets et types

Un clic sur le nom du preset ouvre le navigateur, par-dessus tout ce qui est
sous l'en-tête. À gauche, les filtres : l'origine (All, Factory, User) et les
types d'effet, autant qu'on veut (aucun = tous ; plusieurs = l'un ou l'autre).
À droite, les presets en colonnes, et un champ pour en chercher un par nom.

Un clic charge un preset, qu'on entend aussitôt. OK, Entrée ou un double clic
le gardent ; Cancel ou Échap remettent les réglages d'avant l'ouverture. Les
filtres restent en place, et les flèches `<` `>` de l'en-tête ne parcourent que
ce qu'ils laissent passer.

Types : Flanger, Chorus, Vibrato, Bit Crusher, Distortion. Un preset peut en
avoir plusieurs. Le type n'est pas une étiquette posée à la main, il se lit dans
les réglages, si bien que les presets utilisateur sont triés comme ceux
d'origine :

- **Vibrato** : Mix ≥ 95 %, feedback entre −20 et +20 %, Depth au-dessus de 0 ;
- sinon **Flanger** si Delay < 8 ms, **Chorus** au-delà ;
- **Bit Crusher** dès que Crush ou Downsample est en service ;
- **Distortion** dès que Distortion l'est.

---

## Sources

| Fichier | Contenu |
|---|---|
| `Source/dsp/Modulator.*` | Tout le son, sans dépendance. |
| `Source/PluginProcessor.*` | Paramètres, A/B, presets, état. |
| `Source/PluginEditor.*`, `Source/ui/` | Interface : thème de la gamme, contrôles, écran. |
| `tests/test_modulator.cpp` | Le chemin audio entier, hors ligne. |
| `tools/snapshot.cpp` | Capture de l'interface et autotest du plugin. |
