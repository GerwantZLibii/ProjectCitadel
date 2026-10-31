# ProjectCitadel — analiza stanu projektu

Analiza dotyczy brancha `claude/rts-pathfinding-units-mxiz6x` (UE 5.3, projekt czysto Blueprintowy).
Logika została odczytana bezpośrednio z binarnych Blueprintów: skompilowany bytecode dekompilowany
do pseudokodu narzędziem z `Tools/BlueprintDump` (opis na końcu dokumentu). Nazwy funkcji, zdarzeń
i zmiennych poniżej odpowiadają tym w edytorze.

---

## 1. Mapa systemów

| Obszar | Asset | Rola |
|---|---|---|
| Jednostka (baza) | `Core/Unit/BP_Worker` (`Character`) | **Faktyczna baza wszystkich jednostek** (Worker/Warrior/Mage/Giant/Boss, obie frakcje). Ruch, zbieranie, walka, wszystko w jednym event graphie (~19 kB bytecode). |
| Komponent jednostki | `Core/Unit/BP_UnitComponent` | HP, `UnitState`, `TeamAttitude`, `DamageToApply`, pasek HP, zaznaczenie, reakcja na obrażenia i śmierć. |
| AI gracza | `Core/Unit/AIController_Unit` | Rodzic: `DetourCrowdAIController` (czyli `CrowdFollowingComponent`). Brak własnej logiki. |
| AI wroga | `Core/Enemy/AICon_EnemyUnit` | Co tick: jeśli jednostka nie atakuje i nie jest zajęta → `AttackThisActor(HQ gracza)`. |
| Gracz | `Core/Player/PC_ProjectCitadel` | Selekcja, rozkazy (ruch / zbieranie / atak / interakcja z budynkiem), stawianie budynków. |
| Budynki | `Core/Building/BP_Building`, `BP_BuildingComponent` | Baza budynków, 6 strzałek `SpawnPoint`, HP, zaznaczenie. |
| Rekrutacja | `Buildings/BuildingsUI/WBP_BuildingUI` | **Cała logika rekrutacji siedzi w widgecie UI.** |
| Fale wrogów | `Core/Enemy/BP_EnemySpawner` | Timer fal, losowy punkt na navmeshu w sferze `BP_EnemySpawnPoints`. |
| Martwe assety | `Core/Unit/BP_Unit`, `BP_Unit_backup`, `BP_Worker_backup` | Nic się do nich nie odwołuje. |
| Redirectory | `Units/Tribe1/BP_Worker_Tribe1`, `Units/Tribe2/BP_Worker_Tribe2`, `Core/Enemy/EnemySpawner` | `ObjectRedirector` — do wyczyszczenia przez *Fix Up Redirectors*. |

Enumy: `E_UnitStates` = None/Idle/Movement/Mining/Attacking, `E_UnitTypes` = None/Worker/Warrior/Mage/Giant/Boss,
`E_BuildingTypes` = None/HeadQuarters/WatchTower/Barracks, `E_ResourceType` = None/Gold/Stone.

### Rzeczywiste rozmiary jednostek

Wszystkie jednostki mają kapsułę `R=55, HalfHeight=60`, ale **skalują komponent root (kapsułę)**:

| Jednostka | Skala kapsuły | Efektywny promień | Średnica | Promień „wzroku” (SightSphere) |
|---|---|---|---|---|
| Worker (T1/T2) | 3.0 | **165** | 330 | 1500 |
| Mage (T1/T2) | 3.0 | **165** | 330 | 2250 |
| Warrior (T1/T2) | 3.5 | **192.5** | 385 | 1750 |
| Giant (T1/T2) | 5.0 | **275** | 550 | 2500 |
| Boss (T2) | 7.5 | **412.5** | 825 | 3750 |

To jest klucz do problemu z pathfindingiem — cała nawigacja była skonfigurowana pod dużo mniejsze postacie.

---

## 2. Pathfinding — dlaczego jednostki się blokują

### 2.1 Detour Crowd był skonfigurowany dla agentów do 100 uu
`DefaultEngine.ini → [/Script/AIModule.CrowdManager]`: `MaxAgentRadius=100`, `MaxAgents=50`.
Najmniejsza jednostka ma promień 165, Boss 412. Crowd manager (siatka sąsiedztwa, zakres szukania
poligonów, unikanie) działał na rozmiarach, które w ogóle nie pasują do jednostek. Dodatkowo limit 50
agentów: każda jednostka ponad limit nie dostaje unikania wcale.

### 2.2 Jednostki „widzą” sąsiadów dopiero, gdy już się dotykają
`CrowdFollowingComponent` ma stały `CollisionQueryRange` (domyślnie **400 uu**) — tylko agenci w tym
promieniu (licząc środek–środek) są brani pod uwagę przy omijaniu. Kontakt kapsuł następuje przy:

- Worker + Worker: 330 → zostaje 70 uu „ostrzeżenia” (przy 600 uu/s to ~0,1 s),
- Warrior + Warrior: 385 → 15 uu,
- Giant + Warrior: 467 → **kolizja zanim w ogóle się zauważą**,
- Giant + Giant: 550, Boss + cokolwiek: >570 → j.w.

Dokładnie to opisany objaw: „próbowały się omijać, ale nie zdawały sobie sprawy z wielkości hitboxa
i zahaczały bokami”. Unikanie Detoura uwzględnia promienie, ale nie może ominąć kogoś, kogo nie ma
na liście sąsiadów. Detour w swoim samplu używa `collisionQueryRange ≈ 12 × radius`,
`pathOptimizationRange ≈ 30 × radius` — dla Workera to ~2000 i ~5000 uu.
Do tego `AvoidanceQuality` jest domyślnie `Low`, a separacja wyłączona.

### 2.3 NavMesh liczony dla agenta o promieniu 75
`[/Script/NavigationSystem.RecastNavMesh] AgentRadius=75`. Ścieżki prowadzą 75 uu od przeszkód, a
jednostki mają 165–412 → ścinają narożniki budynków i skał, wchodzą w przejścia, przez które fizycznie
się nie mieszczą, i tam stają.

### 2.4 Budynki stawiane w trakcie gry nie trafiały do navmesha
Koszary i wieże są spawnowane w runtime (`PC_ProjectCitadel`), a `RuntimeGeneration` nie było ustawione,
więc obowiązywał domyślny tryb `Static` — navmesh nie wie o nowym budynku i jednostki planują trasę
przez niego.

### 2.5 Cała grupa dostaje ten sam punkt docelowy
`PC_ProjectCitadel`: dla każdej zaznaczonej jednostki wywoływane jest
`MoveToThisLocation(HitResult.Location, SelectedActors)` — **ten sam punkt dla wszystkich**,
`AI MoveTo` z `AcceptanceRadius=50`. Do punktu może dojść jedna jednostka, reszta się przepycha.
`BP_Worker` łata to `MoveToSuccessSphere` (r=72×skala): gdy ktoś „dojdzie”, zatrzymuje kolegów z grupy,
którzy go dotkną, a ci łańcuchowo kolejnych (`Test()`). Pozostali kończą dopiero przez wykrycie
blokady path followingu (`Blocked` po kilku sekundach przepychania), które `OnFail` traktuje jak sukces.

### 2.6 Drobniejsze
- `CharacterMovement` całkowicie domyślny: każda jednostka ma `MaxWalkSpeed=600` (Giant tak szybki jak Worker), obrót „przeskakuje” do kierunku ruchu.
- Animacje są ustawiane `PlayAnimation` według `UnitState`, nie według prędkości — zablokowana jednostka „idzie w miejscu”, co optycznie pogłębia wrażenie zacięcia.
- RVO jest wyłączone — dobrze, nie należy go włączać razem z Detour Crowd.

### Co zmieniłem w konfiguracji (ten branch)

`Config/DefaultEngine.ini`:

```ini
[/Script/NavigationSystem.RecastNavMesh]
AgentRadius=170.000000        ; było 75 — dopasowane do Workera/Mage, blisko Warriora
RuntimeGeneration=Dynamic     ; nowe budynki wycinają navmesh w trakcie gry

[/Script/AIModule.CrowdManager]
MaxAgents=200                 ; było 50
MaxAgentRadius=450.000000     ; było 100 — pokrywa Bossa (412.5)
```

Do sprawdzenia po otwarciu w edytorze (nie mam tu Unreala, więc nie testowałem w grze):
- Navmesh przebuduje się z nowym promieniem — wąskie przejścia na `RockyLevel`, przez które Worker i tak się fizycznie nie mieścił, znikną z navmesha (`P` w viewporcie pokazuje navmesh).
- Giant i Boss nadal są większe niż `AgentRadius`; tu docelowo pomoże albo mniejsza kapsuła względem modelu (w RTS kolizja jest zwykle mniejsza niż wizualny obrys), albo osobne klasy agentów.
- Żaden komponent jednostki nie wpływa na navmesh (sfery są overlap-only), więc `Dynamic` nie będzie przebudowywał navmesha od ruchu jednostek.

**Brakujący element, którego nie da się ustawić w `.ini`**: parametry `CrowdFollowingComponent`
(`CollisionQueryRange`, `PathOptimizationRange`, `AvoidanceQuality`, separacja) są per-komponent.
Trzeba je ustawić w `AIController_Unit` zależnie od promienia kapsuły possessowanej jednostki — patrz §6.

---

## 3. Walka — dlaczego „czasem się psuje”

Przepływ w `BP_Worker`: `SightSphere` (overlap na kanale Pawn) → `ActorsToAttack` →
`SearchMapByValue` (mapa dystansów) → `FoundKey` → `AttackThisActor` → `AI MoveTo(cel, 400 / 2000 dla maga)`
→ `OnSuccess`: zapamiętanie dystansu w `RangeToActorToAttack`, timer `Attack` co 1 s.

Znalezione błędy, od najpoważniejszych:

1. **Puste `OnFail` przy podejściu do celu.** Oba `AI MoveTo` w `AttackThisActor` (melee i mag) mają pusty
   `OnFail`. Jeśli ruch zostanie zablokowany/przerwany (a przy problemach z §2 to częste), jednostka zostaje
   z `IsAttacking=true`, stanem `Movement` i bez timera ataku. Odblokuje ją tylko śmierć celu (`ReceiveTick`
   sprawdza wyłącznie `IsValid(Actor to Attack)`). U wrogów to samo blokuje `AICon_EnemyUnit`, bo ten
   przydziela cel tylko gdy `!IsAttacking && !IsBusy`.
2. **Wyciek flagi `IsBusy`.** `IsBusy=true` ustawiają: rozkaz ruchu, zbieranie, droga do HQ. Czyszczą je
   tylko: sukces/`Blocked`/`OffPath`/`Invalid` ruchu, oddanie surowca, brak surowca do kopania.
   Nie czyści go: `Aborted` (np. nowy rozkaz przerwał stary), nieudane dojście do surowca, nieudane dojście
   do HQ (`"AIMOVETO HQ FAILED"`). Jednostka z zawieszonym `IsBusy` **przestaje automatycznie atakować
   i nie oddaje** — warunek w `AttackThisActor` to
   `(!IsAttacking && !IsBusy) || IsManualAttack || (nowy cel to inny budynek)`.
3. **Zasięg ataku to przypadkowy snapshot.** `RangeToActorToAttack` = dystans w chwili, gdy `AI MoveTo`
   zgłosił sukces. `Attack` zadaje obrażenia tylko gdy aktualny dystans `<=` tego snapshotu — cel cofnie się
   o centymetr → `StopAttack` → nowe wyszukiwanie → nowe podejście. Stąd „szarpanie” w walce.
   Jednostki nie mają statystyki zasięgu, a dystans jest liczony środek–środek, bez promieni kapsuł.
4. **Retaliacja działa tylko gdy jednostka jest wolna.** `OnOwnerTakeAnyDamage` woła `AttackThisActor(DamageCauser)`,
   ale ten sam warunek co wyżej odrzuca to, jeśli jednostka już coś atakuje albo ma `IsBusy`.
5. **`FoundKey` nigdy nie jest czyszczony**, a minimum jest szukane porównaniem floatów
   (`MinOfFloatArray` == wartość z mapy) — przy pustej liście zostaje stary cel (np. spoza zasięgu wzroku).
6. **Pocisk maga ma zahardkodowane 15 obrażeń** (`BP_Projectile.TargetHit`), `DamageToApply=40` maga jest ignorowane.
7. **Zbieranie surowców zostawia złoża „zajęte”.** Nieudane dojście do złoża (`OnFail` pusty) zostawia
   `IsOccupied=true` — inni robotnicy już go nie wybiorą. Dodatkowo pętla szukania wolnego złoża nie ma
   `Break` (flaga break nigdy nie jest ustawiana), więc wywołuje `GatherThisResource` dla każdego wolnego złoża po kolei.
8. **Brak priorytetu rozkazów.** Nie ma rozróżnienia „move” vs „attack-move”; ręczny rozkaz ruchu i auto-aggro
   walczą o te same flagi.

Wydajność / skalowalność walki: każda jednostka ma sferę overlap o promieniu 1500–3750 uu na kanale Pawn,
więc liczba par overlapów rośnie kwadratowo z liczbą jednostek; `SearchMapByValue` przebudowuje mapę
dystansów przy każdym zdarzeniu; `AICon_EnemyUnit` co tick sprawdza stan jednostki i (gdy jest wolna) iteruje po HQ.

---

## 4. Rekrutacja

`WBP_BuildingUI`:

- Przyciski `Recruit_Worker/Warrior/Mage/Giant` → `HandleRecruitmentCost(UnitType)` → koszt z zahardkodowanego
  `Select` (50/120/150/200, zawsze Gold) → `RecruitUnit` → po spawnie odjęcie surowca.
- „Timer” to globalny cooldown widgetu: `CanRecruit=false` na 1 s. **Brak kolejki i czasu produkcji** —
  klik w trakcie cooldownu jest po prostu ignorowany.
- `RecruitUnit`: klasa z zahardkodowanego `Select` po `UnitType`, spawn na `SpawnPoints[index]`, gdzie `index`
  to zmienna **widgetu** (wspólna dla wszystkich budynków), zawijana na sztywno przy `>= 5`.
- `SpawnPoints` to 6 strzałek z tagiem `SpawnPoint` z `BP_Building`, rozstawionych co ~480–530 uu.
  Giant ma średnicę 550, więc dwa sąsiednie spawny Giantów nachodzą na siebie nawet na pustym placu.
- Spawn z `ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn` → **gdy spot jest zajęty,
  jednostka i tak się pojawia**, często w środku innej. Wchodzące w siebie kapsuły to kolejne źródło zakleszczeń z §2.
- Brak punktu zbiórki (rally point) — nowe jednostki stoją na spawnie i blokują kolejne.

Docelowo rekrutacja powinna należeć do budynku, nie do UI (patrz §6).

---

## 5. Skalowalność / architektura — główne punkty

- **Twarde `Switch`/`Select` po enumach w wielu miejscach** (koszty, klasy jednostek, typy budynków → osobne funkcje
  HUD `UpdateHeadQuartersHealthBar`/`UpdateWatchTowerHealthBar`/`UpdateBarracksHealthBar`, mesh brewery z offsetem
  `-1330` w `BP_BuildingSystemComponent`). Dodanie jednostki/budynku wymaga edycji kilku Blueprintów.
  Rozwiązanie: Data Assety (`UnitDefinition`, `BuildingDefinition`) z klasą, kosztem, czasem produkcji, ikoną, statystykami.
- **`BP_Worker` jako god-object**: ruch, zbieranie, walka, łańcuch zatrzymywania grupy, animacje — w jednym grafie,
  sterowane zestawem bool-i (`IsBusy`, `IsAttacking`, `IsManualAttack`, `IsMoveToSuccess`) zamiast jednego stanu.
  Każda nowa akcja mnoży kombinacje flag. Lepiej: komponenty (Command/Movement, Combat, Gathering) i jedna maszyna stanów
  z jawnie obsłużonym sukcesem i porażką każdego kroku.
- **Rzutowania na konkretne klasy** (`Cast<BP_Worker>` w komponencie i kontrolerze, `GetComponentByClass` wszędzie)
  zamiast interfejsów (`BPI_UnitActions` istnieje, ale jest używany tylko częściowo).
- **Drużyny** jako `ETeamAttitude` na komponencie — dla więcej niż 2 frakcji lepszy `TeamId` + `IGenericTeamAgentInterface`.
- **Wykrywanie wrogów przez overlapy** zamiast okresowego zapytania przestrzennego (np. subsystem z rejestrem jednostek i gridem).
- Debugowe `PrintString` w logice gry (`"kurwa"`, `"konkordat"`, `"DOTKNAL SIURA"`, `"Hello"`…).

---

## 6. Proponowany plan

### Krok 1 — szybkie poprawki w Blueprintach (bez C++)

1. **Parametry crowd zależne od rozmiaru** — `AIController_Unit`, `Event On Possess`:
   pobierz promień kapsuły pionka (`Get Scaled Capsule Size`), weź `Get Path Following Component` →
   `Cast To CrowdFollowingComponent` i ustaw: `CollisionQueryRange ≈ 10–12 × R`, `PathOptimizationRange ≈ 30 × R`,
   `AvoidanceQuality = High`, separację z wagą ~2–3. Jeśli te węzły/właściwości nie są widoczne w BP
   (zależy od eksportu w danej wersji silnika), to dokładnie to miejsce, w którym potrzebny jest mały
   kontroler w C++ (~40 linii).
2. **Formacja zamiast jednego punktu** — w `PC_ProjectCitadel` przed pętlą rozkazów policz siatkę slotów wokół
   kliknięcia: `cols = ceil(sqrt(N))`, odstęp `= 2 × największy promień × 1.15`, siatka obrócona w kierunku ruchu
   (od środka grupy do celu), każdy slot przez `Project Point To Navigation`. Przypisanie: dla każdego slotu od
   przodu najbliższa jeszcze nieprzypisana jednostka. Wtedy `MoveToSuccessSphere` i łańcuch `Test()` przestają być potrzebne.
3. **Obsłużyć każdy `OnFail`** — przy podejściu do celu: wyczyść `IsAttacking`, ponów po krótkim czasie lub
   wybierz kolejny cel; przy ruchu `Aborted` też czyść `IsBusy`; przy złożu zwalniaj `IsOccupied`.
4. **Zasięg ataku jako statystyka** w `BP_UnitComponent` (np. melee 50, mag 1500), liczony od krawędzi kapsuł
   (`dystans − R1 − R2`), z histerezą: atak gdy `<= range`, pościg dopiero gdy `> range × 1.2`.
5. **Spawn**: `AdjustIfPossibleButDontSpawnIfColliding` + szukanie wolnego punktu (pierścienie wokół wyjścia z budynku,
   `Project Point To Navigation` + test kapsuły danej klasy). Brak miejsca → jednostka czeka w kolejce budynku.
   Rally point → każda nowa jednostka od razu odchodzi ze spawnu.

### Krok 2 — fundament pod skalowanie (rekomendacja: C++)

Przy RTS z dziesiątkami/setkami jednostek rdzeń warto mieć w C++, a Blueprinty zostawić do treści i UI:

- `CitadelUnitAIController` + własny `CrowdFollowingComponent`: parametry crowd liczone automatycznie z kapsuły.
- `CitadelFormationLibrary`: sloty formacji z projekcją na navmesh i przypisaniem jednostek.
- `CitadelProductionComponent` (na budynku) + `CitadelUnitDefinition` (Data Asset): kolejka, czas produkcji,
  koszt w wielu surowcach, szukanie wolnego miejsca, rally point, eventy dla UI.
- `CitadelCombatComponent`: jawna maszyna stanów (Idle → Chase → Attack → …), zasięg od krawędzi, ponawianie
  ścieżki, obsługa porażek, priorytet rozkazów gracza nad auto-aggro.
- `CitadelUnitSubsystem`: rejestr jednostek per drużyna + zapytania przestrzenne zamiast sfer overlap.

Zaimplementowane w `Plugins/CitadelRTS` (domyślnie wyłączony plugin). Włączenie i wdrożenie
krok po kroku w Blueprintach: `Plugins/CitadelRTS/README.md`.

### Sprzątanie
Usunąć `BP_Unit`, `BP_Unit_backup`, `BP_Worker_backup`; *Fix Up Redirectors* w `Content/ProjectCitadel`;
usunąć debugowe `PrintString`.

---

## Narzędzie: `Tools/BlueprintDump/bpdump.py`

Czyta pliki `.uasset` (UE 5.3, nieskookowane) bez edytora i wypisuje: komponenty i CDO z niedomyślnymi
właściwościami oraz skompilowane funkcje Blueprintów zdekompilowane do pseudokodu (z oznaczonymi punktami
wejścia eventów). Wymaga tylko Pythona 3.

```bash
git lfs pull
python3 Tools/BlueprintDump/bpdump.py code  Content/ProjectCitadel/Core/Unit/BP_Worker.uasset
python3 Tools/BlueprintDump/bpdump.py props Content/ProjectCitadel/Core/Unit/BP_Worker.uasset
python3 Tools/BlueprintDump/bpdump.py tree  Content/ProjectCitadel /tmp/bp_dump
```

`tree` zrzuca wszystkie assety do plików tekstowych — przydatne do porównywania zmian w Blueprintach
zwykłym `diff` (np. przed i po commicie).
