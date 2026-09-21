# Changelog

Tento soubor sleduje dvě samostatně verzované části repozitáře: WoW addon
(`GX/`) a Windows aplikaci **GX Monitor** (`App/`).

---

# GX Monitor (Windows aplikace)

## 1.0.0

### Novinky

- **První verze GX Monitoru** — nativní Windows aplikace (C++/Win32, bez
  závislostí), náhrada za ruční spouštění `watcher.py` pro uživatele, kteří
  nechtějí instalovat Python.
  - Dashboard s celkovým zlatem, samostatnými kartami pro postavy a Warband
    banku a živým stavovým řádkem.
  - Auto-detekce cesty k `GX.lua` (hledá `WTF\Account\...` směrem nahoru od
    umístění aplikace).
  - Panel nastavení: cesta k `GX.lua`, filtr účtu, výstupní soubor, interval
    hlídání, přepínač raw/formátovaného výstupu.
  - Tlačítka Pause/Resume, Refresh a Open output přímo na dashboardu.
  - Ikona v systémové liště (tray) — levý klik otevře okno, pravý klik
    nabídku (Pause/Resume, Settings, Exit); volitelný start rovnou
    minimalizovaně.
  - Přepínač „Run at Windows startup" (zápis do `HKCU\...\Run`, bez potřeby
    admin práv).

### Vizuální redesign

- **Tmavé grafitovo-zlaté téma** v duchu TradeSkillMaster — karty s jemným
  okrajem, jednotný zlatý akcent (status tečka i text, obroubená tlačítka).
- **Přepínače (toggle switch)** v Nastavení nahradily hranaté checkboxy;
  vykreslují se přes GDI+ s antialiasingem (dřív byly hrany viditelně
  pixelované).
- **Vlastní title bar** — žádný výchozí rám Windows: vlastní ikona, název,
  jen minimalizace a zavření (okno se záměrně **nedá maximalizovat** — tlačítko
  chybí a maximalizace je zablokovaná i přes dvojklik na lištu, Win+↑ nebo
  přetažení na horní okraj obrazovky). Lišta se dá tahat, okno jde stále
  měnit velikost za okraje, zachovává nativní stín a zaoblené rohy (DWM).
- **Responzivní škálování** — layout, fonty i odsazení se přepočítávají podle
  DPI *i* podle aktuální velikosti okna, takže text zůstává čitelný od
  minimální velikosti okna až po velká/vysoko-DPI okna.
- **Oprava:** přetažení okna mezi monitory s různým rozlišením (např. 4K →
  2K) občas nechalo v Nastavení text starého (špatného) DPI přes nově
  přepočítaný layout. Písmo ovládacích prvků se teď při změně DPI vždy znovu
  vytvoří a přiřadí.
- **Oprava:** i přes vlastní `WM_NCCALCSIZE` si okno ponechávalo systémový
  styl `WS_CAPTION` (kvůli stínu a zaobleným rohům), takže DWM přes vlastní
  titulkový pruh appky ještě kreslil svůj nativní — vypadalo to jako dva
  title bary nad sebou. Opraveno explicitním potlačením non-client
  vykreslování (`WM_NCPAINT` + `DWMWA_NCRENDERING_POLICY`).

### Aktualizace aplikace

- **Verze v title baru** — vedle názvu okna se teď zobrazuje aktuální verze
  (`v1.0.0`).
- **Automatický updater** — když je na GitHubu (`dimitrijfedoryno/gx-addon-app`)
  dostupný novější release, objeví se pod titulkovým pruhem vpravo nahoře
  malý zlatý odznak „Update". Klik stáhne a nainstaluje nový build **bez
  zásahu uživatele** — appka se sama restartuje do nové verze (žádné ruční
  stahování ani rozbalování souborů). Kontrola běží na pozadí při startu a
  pak jednou za 6 hodin.
  > Aby updater něco našel, je potřeba na GitHubu vytvořit Release s tagem
  > `vX.Y.Z` a přiloženým `GXMonitor.exe` jako asset.

### Technické změny

- Přidány projektové soubory pro Visual Studio (`App/GXMonitor.sln`,
  `App/GXMonitor.vcxproj`) vedle stávajícího `build.bat` pro příkazovou řádku
  (MSVC).
- Nové soubory `App/src/update.cpp` / `update.h` (WinHTTP dotaz na GitHub
  Releases API, stažení assetu, výměna běžícího `.exe` přes odpojený dávkový
  skript) a `App/src/version.h` (`GX_APP_VERSION` + repo pro updater).

---

# GX addon

## 1.1.0

### Novinky

- **Minimap ikona přes LibDataBroker-1.1 + LibDBIcon-1.0.** Nahradila ruční
  trigonometrii kolem pozicování tlačítka na minimapě. Stejná data o zlatě
  teď jde zobrazit i v Titan Panelu, ElvUI DataBars, Details! nebo XIV
  Databaru — cokoliv, co umí číst standardní LDB data source.
- **`/gx characters`** — vypíše všechny uložené postavy s jejich zlatem a
  datem posledního přihlášení.
- **`/gx forget <číslo nebo Jméno-Server>`** — smaže uložené zlato jedné
  konkrétní postavy (např. smazané, přejmenované nebo přesunuté na jiný
  server), aniž by bylo nutné mazat celou databázi.

### Vylepšení

- **`/gx reset` teď vyžaduje potvrzení** v dialogovém okně, než nevratně
  smaže všechna uložená data (dřív mazal okamžitě bez dotazu).
- **Robustnější klíčování postav.** Název serveru se před uložením
  normalizuje (bez mezer a apostrofů přes `GetNormalizedRealmName`), takže
  spojené realmy nebo drobně odlišný název serveru mezi klienty už nevytvoří
  duplicitní záznam pro tu samou postavu. Staré klíče ve stávající databázi
  se při prvním načtení addonu automaticky sloučí (bez ztráty dat).
- **Kompletnější sledování Warband banky.** Kromě otevření banky a
  `ACCOUNT_MONEY` teď addon reaguje i na zavření banky (`BANKFRAME_CLOSED`)
  a po každé události dělá krátké doměření o 0,2 s později, aby zachytil
  vklad/výběr provedený přímo v UI banky i v případě, že `C_Bank.
  FetchDepositedMoney` o zlomek snímku zaostává.
- **Debounced přepočet po `PLAYER_MONEY`.** Hromadné lootování nebo rychlý
  prodej u vendora spouští jeden přepočet + překreslení UI 0,5 s po
  poslední změně místo přepočtu při každém jednotlivém eventu.
- **Nativní formátování čísel.** Oddělovače tisíců v `/gx export` teď
  používají klientskou funkci `BreakUpLargeNumbers` místo vlastní regex
  implementace.
- **Bezpečný fallback při 0 copper.** `GetStyledMoneyString` vrátí `"0g"`,
  pokud by `GetMoneyString` v nějaké verzi klienta vrátila prázdný řetězec.

### Technické změny

- Přidána složka `Libs/` s vendorovanými knihovnami (public domain):
  `LibStub`, `CallbackHandler-1.0`, `LibDataBroker-1.1`, `LibDBIcon-1.0`.
- `GX.toc` načítá tyto knihovny před `GX_UI.xml` / `GX.lua`.
- Odstraněn nepoužívaný `GXMinimapButtonTemplate` z `GX_UI.xml`.

## 1.0.0

- První verze: sledování zlata všech postav na účtu, Warband banka,
  `/gx show`, `/gx export`, `/gx autosave`, minimap ikona, options panel.
