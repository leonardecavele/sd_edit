## 1. Goal

Faire en sorte que l’exécutable affiche un message d’erreur CLI clair et actionnable quand la runtime libsndfile n’est pas disponible, au lieu de dépendre de l’erreur opaque du loader Windows.

## 2. Approach

Le point technique déterminant est dans `src/main.c:10` et `makefile:18-26` : le programme est actuellement lié implicitement contre `sndfile.dll`, ce qui fait échouer le process avant `main()` si la DLL n’est pas trouvée. Pour pouvoir contrôler l’erreur, il faut retirer cette dépendance de chargement au démarrage, charger `sndfile.dll` explicitement depuis le binaire au runtime, d’abord via le bundle du repo documenté dans `README.md:69-108`, puis afficher un message propre si la DLL reste introuvable.

Je garderais le changement limité à `src/main.c`, `makefile` et `README.md`. `include/preset.h:4-26` et `include/effects.h:4-33` utilisent `sndfile.h` pour les types, mais n’appellent aucun symbole `sf_*`, donc ils n’ont pas besoin d’être restructurés pour ce correctif.

## 3. File Changes

- **Modify** `src/main.c:1-20, 398-495, 715-752, 945-954`
  - Ajouter le chargement explicite de `sndfile.dll` via l’API Windows.
  - Remplacer les appels directs à `sf_open`, `sf_read_short`, `sf_write_short` et `sf_close` dans le flux audio par des wrappers/fonctions pointeurs initialisés au démarrage.
  - Initialiser la runtime audio avant `run_cli()` et afficher un message d’erreur unique, clair et actionnable si `sndfile.dll` n’est pas trouvée.
  - Réutiliser la même résolution de chemin “relative à l’exécutable” pour `sndfile-play.exe`, qui est aujourd’hui résolu via le répertoire courant dans `src/main.c:732-744`.

- **Modify** `makefile:12-26`
  - Supprimer le lien implicite `-lsndfile` de l’édition de liens pour éviter l’import Windows de `sndfile.dll` au chargement du process.
  - Conserver uniquement les headers `libsndfile` via `INCLUDE = -I$(LIBSNDFILE_DIR)/include -Iinclude`, car le code continuera à utiliser les types déclarés par `sndfile.h`.
  - Garder `make run` compatible ; il pourra continuer à préfixer `PATH`, mais cela ne sera plus la seule façon d’obtenir un démarrage propre.

- **Modify** `README.md:61-126`
  - Mettre à jour la section compilation/exécution pour expliquer que `main.exe` essaie d’abord de charger la DLL bundlée du repo, puis tombe en erreur avec un message clair si elle reste introuvable.
  - Ajuster la section “Runtime PATH behavior” pour refléter que `make run` reste pratique, mais n’est plus l’unique protection contre un crash de chargement avant `main()`.

## 4. Implementation Steps

### Task 1: Retirer la dépendance de chargement implicite à `sndfile.dll`

1. Dans `makefile:17-26`, enlever `-lsndfile` de `LIB` et de la ligne de link de `$(OUT)` afin que `main.exe` n’importe plus `sndfile.dll` au démarrage.
2. Dans `src/main.c:1-20`, ajouter les includes Windows nécessaires (`windows.h` ou équivalent minimal) et définir des typedefs de fonctions pointeurs correspondant exactement aux signatures `sf_open`, `sf_read_short`, `sf_write_short`, `sf_close` et, si utilisé pour le détail d’erreur, `sf_strerror`.
3. Dans `src/main.c` juste après les helpers utilitaires du début de fichier, ajouter un petit bloc runtime `sndfile` qui :
   - récupère le chemin absolu de l’exécutable ;
   - construit le chemin vers `libsndfile-1.2.2-win64\\bin\\sndfile.dll` relativement à cet exécutable ;
   - tente `LoadLibrary` sur ce chemin ;
   - retombe éventuellement sur `LoadLibrary("sndfile.dll")` pour conserver la compatibilité avec un `PATH` déjà configuré ;
   - résout chaque symbole nécessaire via `GetProcAddress`.

### Task 2: Afficher une erreur propre avant toute commande utilisateur

4. Dans `src/main.c:945-954`, appeler l’initialisation runtime avant `init_session(&session)` et avant `run_cli(&session, argc, argv)`.
5. Si l’initialisation échoue, écrire sur `stderr` un message unique du type : DLL manquante, chemin bundlé attendu, et action recommandée (`make run` ou ajout de `libsndfile-1.2.2-win64\\bin` au `PATH`), puis quitter avec code 1.
6. Faire en sorte que le message soit spécifique au problème runtime `sndfile.dll`, et qu’il ne masque pas les erreurs applicatives existantes déjà gérées plus bas dans `src/main.c:398-495`.

### Task 3: Router les opérations audio via la runtime chargée

7. Dans `src/main.c:398-463`, remplacer `sf_open`, `sf_read_short` et `sf_close` par les wrappers/runtime handles chargés au Task 1, sans changer la logique métier de validation des métadonnées ni l’allocation du buffer.
8. Dans `src/main.c:465-494`, remplacer `sf_open`, `sf_write_short` et `sf_close` par les mêmes wrappers/runtime handles.
9. Conserver les messages existants sur les erreurs d’ouverture/lecture/écriture de fichiers, avec éventuellement un complément venant de `sf_strerror` seulement si ce détail est disponible proprement après le chargement de la DLL.

### Task 4: Aligner les chemins runtime associés

10. Dans `src/main.c:19` et `src/main.c:715-752`, remplacer la résolution `_fullpath(..., PLAY_COMMAND_PATH, ...)` basée sur le répertoire courant par une résolution basée sur le dossier de l’exécutable, en réutilisant le helper ajouté au Task 1.
11. Garder le comportement actuel de `play <file.wav>` inchangé côté CLI ; seul le mécanisme de localisation de `sndfile-play.exe` doit devenir robuste au même titre que `sndfile.dll`.

### Task 5: Documenter le nouveau comportement

12. Dans `README.md:79-126`, mettre à jour les exemples d’exécution directe (`main.exe`, `main.exe help`, `main.exe do ...`) pour expliquer qu’une absence de `PATH` ne doit plus provoquer un échec opaque avant le démarrage.
13. Dans `README.md:99-108` et `README.md:126`, documenter précisément la nouvelle règle : le programme tente le bundle local, puis le `PATH`, puis affiche une erreur claire si aucun runtime n’est trouvable.

## 5. Acceptance Criteria

- L’exécutable construit après changement n’importe plus `sndfile.dll` au chargement du process ; l’absence de runtime ne peut donc plus tuer le programme avant `main()`.
- Depuis un shell où le dossier `libsndfile-1.2.2-win64\\bin` n’a pas été ajouté au `PATH`, lancer `main.exe help` depuis le repo ne produit plus d’échec loader opaque ; soit le programme démarre en chargeant la DLL bundlée, soit il imprime un message d’erreur CLI unique et actionnable.
- Si `sndfile.dll` n’est trouvable ni dans le bundle du repo ni via `PATH`, le message d’erreur mentionne explicitement `sndfile.dll`, le chemin bundlé attendu, et la correction attendue (`make run` ou ajout du dossier `bin` au `PATH`).
- Les commandes audio existantes continuent à fonctionner avec la DLL disponible : `help`, `do audio\\test.wav`, et `play audio\\test.wav` conservent leur interface actuelle.
- `play` ne dépend plus implicitement du répertoire courant pour retrouver `sndfile-play.exe`; le binaire est résolu relativement à l’exécutable.

## 6. Verification Steps

1. Recompiler avec `make clean` puis `make`.
2. Vérifier l’absence d’import loader direct avec une commande du type `objdump -p main.exe` et confirmer que `sndfile.dll` n’apparaît plus dans la table des DLL importées.
3. Ouvrir un shell sans préfixage manuel du dossier `libsndfile-1.2.2-win64\\bin` dans `PATH`, puis lancer `main.exe help` depuis la racine du repo.
   - Résultat attendu : démarrage normal via la DLL bundlée, ou message d’erreur clair si la DLL est indisponible.
4. Vérifier le flux nominal via le wrapper documenté : `make run ARGS="help"`.
5. Vérifier le flux audio direct sans PATH préconfiguré : `main.exe do audio\\test.wav`.
6. Vérifier la lecture : `main.exe play audio\\test.wav`.
7. Vérifier le cas d’échec contrôlé en rendant temporairement la DLL bundlée indisponible dans un environnement de test isolé, puis relancer `main.exe help` et confirmer que le message applicatif apparaît au lieu d’un échec loader Windows.

## 7. Risks & Mitigations

- **Risque :** laisser `-lsndfile` dans `makefile:18-26` annulerait tout le bénéfice du correctif, car Windows continuerait d’échouer avant `main()`.
  - **Mitigation :** vérifier explicitement la table d’import de `main.exe` après build.

- **Risque :** utiliser des chemins relatifs au répertoire courant reproduirait le problème sous une autre forme pour `sndfile.dll` ou `sndfile-play.exe`.
  - **Mitigation :** construire tous les chemins runtime à partir du chemin absolu de l’exécutable, pas du `cwd`.

- **Risque :** une signature erronée de fonction pointeur `sf_*` provoquerait des crashs subtils à l’exécution.
  - **Mitigation :** copier exactement les signatures depuis `sndfile.h` et centraliser leur résolution dans un seul bloc runtime dans `src/main.c`.

- **Risque :** élargir ce correctif à d’autres modules créerait du bruit sans valeur, alors que seuls `src/main.c` et l’édition de liens utilisent réellement la runtime `libsndfile` aujourd’hui.
  - **Mitigation :** garder `include/preset.h`, `include/effects.h`, `src/preset.c` et `src/effects.c` inchangés pour ce ticket, sauf besoin imprévu découvert pendant l’implémentation.