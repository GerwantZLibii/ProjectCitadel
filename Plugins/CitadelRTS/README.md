# Citadel RTS — rdzeń RTS w C++

Plugin z czterema niezależnymi systemami. Każdy można wdrażać osobno; Blueprinty dalej trzymają
treść gry (modele, animacje, UI), a C++ robi to, co musi być szybkie i odporne na błędy.

| Klasa | Co rozwiązuje |
|---|---|
| `CitadelUnitAIController` | Omijanie (Detour Crowd) dopasowane do rozmiaru jednostki — koniec zahaczania się bokami. |
| `CitadelFormationLibrary` | Rozkaz grupowy rozkładany na osobne punkty w szyku zamiast jednego punktu dla wszystkich. |
| `CitadelProductionComponent` + `CitadelUnitDefinition` + `CitadelResourceOwner` | Kolejka produkcji w budynku, koszty i czasy w Data Assetach, spawn tylko na wolnym miejscu, rally point. |
| `CitadelCombatComponent` + `CitadelUnitSubsystem` | Walka jako jedna maszyna stanów: zasięg od krawędzi kapsuł, obsługa nieudanych podejść, priorytet rozkazów gracza. |

Analiza, z której to wynika: `Docs/Analiza_ProjectCitadel.md`.

> Kod powstał bez dostępu do Unreala i nie był jeszcze kompilowany. Jeśli pierwsza kompilacja
> zgłosi błędy, wklej log — poprawki powinny być drobne.

---

## Włączenie (jednorazowo)

Projekt jest już projektem C++ (`Source/ProjectCitadel`), a plugin jest włączony w `ProjectCitadel.uproject`.
Pierwsza kompilacja najlepiej w Visual Studio, bo tam widać ewentualne błędy (edytor pokazuje tylko
„could not be compiled”).

1. Wymagany Visual Studio 2022 z pakietem *Game development with C++* (w Visual Studio Installer).
2. Zamknij edytor. PPM na `ProjectCitadel.uproject` → *Generate Visual Studio project files*
   (w Windows 11 może być pod *Pokaż więcej opcji*).
3. Otwórz `ProjectCitadel.sln`, na górze wybierz `Development Editor` i `Win64`, potem *Build → Build Solution* (Ctrl+Shift+B).
4. Po udanej kompilacji otwieraj projekt normalnie przez `ProjectCitadel.uproject`.

---

## 1. Pathfinding — `CitadelUnitAIController`

1. Otwórz `Core/Unit/AIController_Unit` → **Class Settings → Parent Class → CitadelUnitAIController** → Compile, Save.
   `AICon_EnemyUnit` dziedziczy z `AIController_Unit`, więc wrogowie dostają to samo.
2. Gotowe. Przy possess kontroler czyta promień kapsuły jednostki i ustawia:
   - zasięg wykrywania sąsiadów = promień × `CollisionQueryRangeScale` (12) — Worker: ~1980 uu zamiast stałych 400,
   - zasięg optymalizacji ścieżki = promień × `PathOptimizationRangeScale` (30),
   - jakość omijania `High`, separację (waga 2).

   Wartości są w *Class Defaults → Citadel|Crowd*. Działa razem ze zmianami w `Config/DefaultEngine.ini`
   (promień navmesha 170, crowd do promienia 450 i 200 agentów, dynamiczny navmesh).
3. Jeśli jednostka zmienia rozmiar w trakcie gry, wywołaj na kontrolerze `ApplyCrowdSettings`.

Dodatkowo kontroler:
- **Obraca jednostkę płynnie** (*Citadel|Turning*, `TurnRate` = 540°/s) w stronę ścieżki zamiast w stronę chwilowej
  prędkości z omijania (`bFaceVelocity` wyłączone). To usuwa drganie modelu na boki. Ustawia przy tym na Characterze
  `Use Controller Rotation Yaw = false` i `Use Controller Desired Rotation = true`; obracanie przez `SetActorRotation`
  w Blueprintach działa dalej.
- **Ustępuje drogi** (*Citadel|Yield*): stojąca jednostka, w którą wpycha się zablokowana jednostka idąca dalej,
  robi krok w bok (co `YieldCheckInterval`, nie częściej niż co `YieldCooldown`). Nie ustępuje wrogom ani w trakcie walki
  z `CitadelCombatComponent`. Żeby jednostka trzymała miejsce (np. robotnik przy złożu), ustaw na kontrolerze `bCanYield = false`.
  Krok w bok odbywa się w animacji Idle, bo animacje są teraz sterowane stanem, a nie prędkością.

Podgląd w PIE: Gameplay Debugger (klawisz `'`), zaznacz jednostkę — widać ścieżkę i navmesh.

## 2. Formacje — automatycznie w `CitadelUnitAIController`

**Nie wymaga zmian w Blueprintach.** Kontroler przechwytuje każdy ruch do punktu (także `AI MoveTo` z Blueprintów)
i oddaje go `CitadelUnitSubsystem::ClaimMoveSlot`:
- jednostki wysłane w tej samej klatce w ten sam punkt (rozkaz dla zaznaczenia) dostają osobne miejsca w pierścieniach
  wokół niego, od środka na zewnątrz, z odstępem = średnica największej z nich × 1.15; każda bierze z bieżącego pierścienia
  miejsce najbliższe sobie, więc ścieżki mało się krzyżują,
- miejsce poza navmeshem albo zajęte przez jednostkę spoza rozkazu jest pomijane — nikt nie próbuje dojść „w” stojącą jednostkę,
  także pojedyncza.
Ruchy do aktora (atak, złoże) nie są zmieniane. Wyłączenie: `bSpreadMoves` na kontrolerze (*Citadel|Formation*).

Kontroler przy possess sprawdza też, czy jednostka nie zaczyna w środku innej (np. zrespiona na zajętym spawn poincie);
jeśli tak, przestawia ją na wolne miejsce obok (`bResolveSpawnOverlap`).

Opcjonalnie: szyk z rzędami ustawionymi w kierunku marszu daje **Compute Formation Destinations** (`CitadelFormationLibrary`),
wołane w `PC_ProjectCitadel` przed pętlą rozkazów — `MoveToThisLocation(Destinations[Array Index], SelectedActors)`.
Łańcuch `MoveToSuccessSphere` w `BP_Worker` (zatrzymywanie kolegów, którzy dotkną tego, kto już doszedł) można zostawić;
przy odstępach 1.15 × średnica praktycznie nie przeszkadza, ale da się go odłączyć dla czystości.

## 3. Produkcja — `CitadelProductionComponent`

**Data Assety jednostek** — Content Browser → *Miscellaneous → Data Asset → CitadelUnitDefinition*, po jednym na typ.
Obecne wartości z `WBP_BuildingUI`:

| Asset | UnitClass | Cost | BuildTime (do ustalenia) |
|---|---|---|---|
| `DA_Unit_Worker` | `BP_Worker_Tribe1` | `Gold = 50` | np. 3 |
| `DA_Unit_Warrior` | `BP_Warrior_Tribe1` | `Gold = 120` | np. 5 |
| `DA_Unit_Mage` | `BP_Mage_Tribe1` | `Gold = 150` | np. 6 |
| `DA_Unit_Giant` | `BP_Giant_Tribe1` | `Gold = 200` | np. 8 |

Klucze kosztu to nazwy surowców (`Gold`, `Stone`), tak jak tagi złóż.

**Budynek** — dodaj `CitadelProductionComponent` do `BP_Building` (dziedziczą HQ i koszary) i w każdym budynku wypełnij `ProducibleUnits`.
`ExitPointTag = SpawnPoint` pasuje do istniejących strzałek; to tylko punkty startowe szukania — komponent sprawdza
pierścienie wokół nich (`SearchRings`), rzutuje na navmesh i testuje kapsułę danej jednostki przeciwko innym jednostkom.
Gdy nie ma miejsca, gotowa jednostka czeka w kolejce (`OnProductionBlocked`) i próbuje co `SpawnRetryInterval`.

**Płatność** — `PC_ProjectCitadel` → *Class Settings → Implemented Interfaces → Add → CitadelResourceOwner*, potem zaimplementuj:
- `TrySpendResources(Cost)`: dla każdej pary z `Cost` zamień nazwę na `E_ResourceType` (`Gold` → Gold, `Stone` → Stone) i sprawdź `StoredResources`;
  tylko jeśli starcza na wszystko, odejmij przez `ReceiveResources(typ, -ilość)` i zwróć `true`.
- `RefundResources(Amount)`: `ReceiveResources(typ, +ilość)`.

Bez tej implementacji produkcja jest darmowa (przydatne do testów).

**UI** — UI musi wiedzieć, który budynek jest zaznaczony: w `BP_Building.SelectThis` obok `SpawnPoints` ustaw np. `BuildingHUDRef.SelectedBuilding = self`.
Przyciski w `WBP_BuildingUI`: `SelectedBuilding → Get Component by Class (CitadelProductionComponent) → Enqueue(DA_Unit_…)`.
Do usunięcia: `HandleRecruitmentCost`, `RecruitUnit`, `CanRecruit`, `index`, timer `RecruitmentDelay`.
Postęp: `GetCurrentProgress` (0–1), kolejka: `GetQueue`, odświeżanie: event `OnQueueChanged`, anulowanie: `CancelOrder(indeks)` (zwraca koszt).

**Rally point** — np. prawy klik na ziemię przy zaznaczonym budynku → `SetRallyPoint(lokacja)`. W `BP_Building` podepnij `OnUnitProduced`:
jeśli `GetRallyPoint` zwraca `true`, wywołaj na jednostce `MoveToThisLocation(Rally, [Unit])`, żeby poszła własną logiką ruchu.
`bMoveToRallyPoint` (zwykły AI move z C++) jest dla jednostek bez własnej logiki ruchu.

## 4. Walka — `CitadelCombatComponent`

Największa zmiana: zastępuje logikę walki z `BP_Worker`. Rób to w całości, bo dwie logiki walki naraz będą sobie przerywać ruchy.

**Ustawienia**

1. Dodaj `CitadelCombatComponent` do `BP_Worker`. W klasach jednostek ustaw:
   - `TeamId`: Tribe1 = 0, Tribe2 = 1,
   - `Damage` = obecne `DamageToApply` (Worker 10/20, Warrior 30, Mage 40, Giant 35, Boss 75),
   - `AttackRange` (od krawędzi do krawędzi): walczący wręcz ~100, Mage np. 1200,
   - `SightRadius` = obecny zasięg wzroku (1500 / 1750 / 2250 / 2500 / 3750),
   - `AttackInterval` = 1.
2. Do `BP_Building` dodaj komponent z `bCanAttack = false`, `TargetPriority = 0` (jednostki mają 1, więc są atakowane chętniej niż budynki).
   Wieża: `bCanAttack = true`, `AttackRange` i `SightRadius` = jej zasięg — bez kontrolera AI nie goni, tylko strzela do tego, co w zasięgu.

**Zmiany w Blueprintach**

3. W `BP_Worker` usuń: eventy BeginOverlap/EndOverlap `SightSphere`, `ActorsToAttack`, `DistanceToActor`, `SearchMapByValue`,
   `Search for the nearest`, `FoundKey`, `Attack` + `AttackTimer`, sprawdzanie `IsAttacking` w `ReceiveTick`, `RangeToActorToAttack`.
   Samą `SightSphere` można usunąć — to koniec kwadratowej liczby overlapów.
4. `AttackThisActor` (z `BPI_UnitActions`) → `CitadelCombat.AttackTarget(ActorToAttack, Lock Target = true)`. `StopAttack` → `StopAttacking`.
5. Animacje: podepnij `CitadelCombat.OnStateChanged` → `BP_UnitComponent.HandleNewUnitState`: Idle → Idle, Chasing → Movement, Attacking → Attacking.
6. Rozkaz ruchu (`MoveToThisLocation`): na początku `StopAttacking` i `SetAutoTargetingSuppressed(true)`; w `OnSuccess` **i** `OnFail`
   (każdy wynik) `SetAutoTargetingSuppressed(false)` i `IsBusy = false`. Jednostka idąca na rozkaz nie daje się wtedy wciągnąć w walkę.
7. `BP_UnitComponent.OnOwnerTakeAnyDamage`: usuń gałąź wołającą `AttackThisActor(DamageCauser)` — odwet robi komponent
   (jednostka bez rozkazu odpowiada atakującemu, jednostka z rozkazem ataku trzyma się celu).
   Przy `Health == 0` przed `DestroyActor` możesz wywołać `NotifyDied`.
8. Mag: `bApplyDamageOnAttack = false`, `OnAttack(Victim)` → spawn `BP_Projectile` jak teraz. W `BP_Projectile.TargetHit`
   zamień zahardkodowane 15 na `Damage` z `CitadelCombatComponent` twórcy.
9. `AICon_EnemyUnit`: zamiast pętli po HQ co tick — co ~0,5 s: jeśli `CitadelCombat.GetTarget` jest niepoprawny,
   `AttackTarget(HQ gracza, Lock Target = false)`. Nieprzypięty cel sprawia, że wróg po drodze zajmie się jednostkami gracza
   (wyższy priorytet), a potem wróci do HQ.

**Zachowanie**

- `Idle → Chasing → Attacking`; zasięg liczony od krawędzi kolizji, więc 100 znaczy to samo dla Workera i Giganta.
- Po wejściu w zasięg cel może odejść o `RangeTolerance` zanim zacznie się pościg — bez szarpania.
- Jeśli `MaxChaseAttemptsWithoutProgress` kolejnych podejść kończy się bez zbliżenia (zablokowana droga),
  jednostka odpuszcza cel na `UnreachableIgnoreTime` sekund i szuka innego — nie zostaje zawieszona.
- Cel wybrany automatycznie jest porzucany poza `SightRadius × AutoTargetLeash`; cele z rozkazu nie mają smyczy.
