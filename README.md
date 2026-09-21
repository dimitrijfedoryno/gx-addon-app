# 🪙 GX — Gold Export

Sleduje zlato **všech postav na účtu** (včetně sdílené Warband banky) a umí ho
zobrazit ve hře nebo naživo exportovat do textového souboru pro **OBS**
overlay. Skládá se ze dvou částí:

- 🎮 **[`GX/`](GX/)** — samotný WoW addon (vanilla Lua + pár vendorovaných
  knihoven), který zlato sbírá a ukládá.
- 🖥️ **[`App/`](App/)** — **GX Monitor**, nativní Windows aplikace (C++/Win32),
  která hlídá uložená data a přepisuje textový soubor pro OBS. Alternativa k
  ručnímu spouštění Python skriptu.

---

## 📁 Struktura repozitáře

| Cesta | Popis |
| --- | --- |
| `GX/GX.toc` | Metadata addonu (Interface: 120100, SavedVariables: `GX_DB`). |
| `GX/GX_UI.xml` | Frame šablony zděděné z oficiálních Blizzard šablon. |
| `GX/GX.lua` | Veškerá logika addonu. |
| `GX/Libs/` | Vendorované knihovny `LibStub`, `CallbackHandler-1.0`, `LibDataBroker-1.1`, `LibDBIcon-1.0` (public domain). |
| `App/` | 🖥️ **GX Monitor** — Windows GUI app (viz [App/README](#-gx-monitor-windows-aplikace)). Zdrojáky v `App/src`, projekt pro Visual Studio `App/GXMonitor.sln`. |
| `watcher.py`, `start_watcher.bat` | 🐍 Lehčí alternativa k GX Monitoru — Python skript bez GUI. |
| `GX.zip`, `GX_default.zip` | Zabalené distribuční verze addonu (obsah `GX/`). |
| `wow-ui-source/` | Referenční (needitovaný) zdroj Blizzard UI — jen dokumentace šablon, do addonu se nekopíruje. |
| `CHANGELOG.md` | Historie změn addonu i GX Monitoru. |

---

## 📦 Instalace addonu

1. Zkopíruj složku **`GX/`** (obsahující `GX.toc`) do
   `World of Warcraft\_retail_\Interface\AddOns\`.
   Výsledná cesta musí být `...\Interface\AddOns\GX\GX.toc`.
   *(Nebo rozbal `GX.zip` přímo do `AddOns\` — udělá to samé.)*
2. Spusť hru — na přihlašovací obrazovce musí být addon **GX** zaškrtnutý
   (po čisté instalaci se zapíná automaticky).
3. ✅ Hotovo. Zlato se začne ukládat při prvním přihlášení na postavu.

---

## 🎮 Použití ve hře

Slash příkaz: `/gx`

| Příkaz | Význam |
| --- | --- |
| `/gx` nebo `/gx help` | Vypíše nápovědu. |
| `/gx show` | Otevře kompaktní okno s celkovým zlatem za celý účet. |
| `/gx export` | Otevře okno s kopírovatelným textem aktuálního součtu. |
| `/gx autosave` | Zapne/vypne automatický reload (výchozí interval 60 min). |
| `/gx autosave <min>` | Nastaví interval v minutách (minimálně 1 minuta). |
| `/gx autosave off` | Vypne automatický reload. |
| `/gx settings` | Otevře nastavení (Esc → Options → AddOns). |
| `/gx minimap` | Zobrazí / skryje kruhovou ikonu u minimapy. |
| `/gx characters` | Vypíše všechny uložené postavy, jejich zlato a datum posledního přihlášení. |
| `/gx forget <č.\|Jméno-Server>` | Smaže zlato jedné (např. smazané/přejmenované/přesunuté) postavy podle čísla z `/gx characters` nebo klíče. |
| `/gx reset` | Smaže **všechna** uložená data o zlatě (vyžaduje potvrzení v dialogu). |

Okna `/gx show` i `/gx export` se zavírají tlačítkem **✕** i klávesou **ESC**.

### 🖱️ Minimap ikona a nastavení

- **Minimap ikona** — postavená na LibDBIcon-1.0 (kruhová ikona zlata vedle
  minimapy; pravé tlačítko → nastavení, levé → otevře/zavře okno se zlatem,
  tažením se přesouvá a pozice se ukládá do `GX_DB.minimap`). Stejná data
  (přes LibDataBroker-1.1) jde zobrazit i v Titan Panelu, ElvUI DataBars,
  Details! nebo XIV Databar, pokud je máš nainstalované.
- **Nastavení** — `Esc` → `Options` → `AddOns` → **GX - Gold Export** (nebo
  `/gx settings`). Panel umožňuje: zapnout/vypnout minimap ikonu, nastavit
  interval autosave posuvníkem (0 = vypnuto), tlačítko „Save & reload now",
  „Show gold window" a „Reset saved gold" (s potvrzovacím dialogem). Vše se
  ukládá okamžitě do `GX_DB`.

### 🧠 Jak addon sbírá data

Každá postava, se kterou se přihlásíš, si uloží svoje aktuální zlato do tabulky
`GX_DB` pod klíčem `Jméno-Server` (SavedVariables). Název serveru se před
uložením normalizuje (bez mezer a apostrofů), takže spojené realmy nebo
klienty hlásící mírně odlišný název serveru nevytvoří duplicitní záznam pro
stejnou postavu; staré klíče se při načtení addonu jednorázově sloučí do
nového formátu. Údaj se aktualizuje při přihlášení (`PLAYER_LOGIN`) a při
každé změně peněz (`PLAYER_MONEY`), s krátkým debounce (0,5 s), aby hromadné
lootování nebo rychlý prodej u vendora nevyvolávaly přepočet a překreslení
při každém jednotlivém eventu.

Zároveň addon automaticky sleduje a započítává **zlato z Warband banky**
(`C_Bank.FetchDepositedMoney`), a to při přihlášení, otevření banky
(`BANKFRAME_OPENED`), zavření banky (`BANKFRAME_CLOSED`) nebo při jakékoli
změně peněz na účtu (`ACCOUNT_MONEY`) — pokaždé s krátkým doměřením o 0,2 s
později, protože `FetchDepositedMoney` může o zlomek snímku zaostávat za
vkladem/výběrem provedeným přímo v okně banky.

Protože SavedVariables jsou **per-account** (ne per-character), addon vidí zlato
všech postav i Warband banky, které na účtu máš, a `/gx show` i export skript to
sečte.

Soubor na disku vznikne na cestě:

```
WTF\Account\<NázevÚčtu>\SavedVariables\GX.lua
```

> ⚠️ **Upozornění:** Klient zapíše SavedVariables na disk **pouze při
> `/reload` nebo při odhlášení postavy**. Nikdy za běhu živě. To je dáno WoW
> Lua sandboxem (viz níže).

---

## 💾 `/gx autosave` — průběžné ukládání

`/gx autosave` zapne opakovaný `ReloadUI()` v nastaveném intervalu. Reload
přiměje klienta zapsat SavedVariables na disk, takže GX Monitor (nebo
`watcher.py`) dostane čerstvá data i bez ručního reloadu.

**Důležité varování:**

- Reload na ~1 sekundu zmrazí hraní (UI se znovu načte).
- Interval **nastavuj minimálně 60 sekund**; příliš časté reloady ruší hraní.
  Výchozí hodnota je 60 minut.
- Autosave se po každém reloadu sám znovu zapne (nastavení je uložené
  v `GX_DB.autosaveMinutes`). Vypnout: `/gx autosave off`.

---

## 📤 Export do OBS

### Proč to nejde „jen" z addonu?

WoW addon běží v **Lua sandboxu bez přístupu k souborům** – neexistuje žádné
`io`/`os` file API. Addon nemůže napsat ani přepisovat `.txt` soubor. Jediné,
co umí „zapsat", je SavedVariables, a to klient fyzicky uloží na disk pouze při
`/reload` nebo odhlášení – **nikdy živě za běhu**. Proto je potřeba **externí
pomocník mimo hru**, který `GX.lua` hlídá a přepisuje čistý textový soubor,
který OBS umí číst. Na výběr jsou dvě varianty:

### 🖥️ Možnost A: GX Monitor (doporučeno)

**GX Monitor** (`App/`) je nativní Windows aplikace bez závislostí (žádný
Python) s tmavým grafitovo-zlatým UI v duchu TradeSkillMaster:

- 📊 **Dashboard** — velký souhrn celkového zlata + samostatné karty pro
  postavy a Warband banku, živý stavový řádek („Watching GX.lua…").
- 🔍 **Auto-detekce** cesty k `GX.lua` — hledá `WTF\Account\...` směrem nahoru
  od umístění aplikace, není potřeba nic ručně vypisovat.
- ⚙️ **Nastavení v appce** — cesta k `GX.lua`, filtr účtu, výstupní soubor,
  interval hlídání, přepínač raw/formátovaného výstupu — vše jako přepínače
  místo needitovatelného configu.
- 🧳 **Systémová lišta (tray)** — levý klik otevře okno, pravý klik nabídku
  (Pause/Resume, Settings, Exit); appka může startovat rovnou minimalizovaná.
- 🚀 **Spouštění s Windows** — přepínač „Run at Windows startup" (zapisuje se
  do registru `HKCU\...\Run`, žádná admin práva).
- ⏸️ **Pause/Resume, Refresh, Open output** přímo z dashboardu.
- 🖼️ **Vlastní title bar** — žádný výchozí Windows rám, jen ikona, název a
  minimalizace/zavření (okno se úmyslně nedá maximalizovat na celou
  obrazovku); layout, fonty i odsazení se responzivně škálují podle DPI i
  velikosti okna.

**Sestavení** (Windows, MSVC):

```bat
cd App
build.bat
```

nebo otevři `App/GXMonitor.sln` ve Visual Studiu 2022 (C++ desktop workload)
a sestav přímo z IDE. Výsledné `GXMonitor.exe` nemá žádné externí závislosti
(jen systémové DLL).

**Použití:** spusť `GXMonitor.exe`, v **Settings** zkontroluj/doplň cestu k
`GX.lua` (obvykle se najde sama) a cestu k výstupnímu textovému souboru,
klikni **Save & Apply**. Appka od té chvíle sama hlídá `GX.lua` a přepisuje
výstupní soubor — pokračuj sekcí [🎬 Napojení v OBS](#-napojení-v-obs).

### 🐍 Možnost B: `watcher.py` (Python skript)

Lehčí alternativa bez GUI — vhodná např. na Linux/macOS přes Wine, nebo když
nechceš spouštět .exe.

Vyžaduje **Python 3.6+**, žádné třetí strany knihovny (čistý standardní
balíček).

**Nejjednodušší spuštění (doporučeno)** – stačí poklepat na soubor
**`start_watcher.bat`**.
- Otevře malé, čisté terminálové okno zobrazující pouze celkové zlato a čas
  aktualizace.
- Pokud na počítači Python chybí, dávkový soubor jej **automaticky
  nainstaluje z Microsoft Store** (přes winget nebo otevře Store).

**Ruční spuštění přes příkazovou řádku**:
```bat
python watcher.py --compact
```
(případně bez `--compact` pro detailní ladicí výpis).

Pokud máš na instanci víc účtů a chceš sledovat konkrétní (jinak se použije
první nalezený abecedně):

```bat
python watcher.py --account 410566417#1
```

Alternativně lze cestu zadat explicitně:

```bat
python watcher.py --file "C:\World of Warcraft\_retail_\WTF\Account\<NázevÚčtu>\SavedVariables\GX.lua"
```

Nebo nech cestu najít podle instalační složky WoW (složka obsahující `WTF`):

```bat
python watcher.py --wow-root "C:\World of Warcraft\_retail_"
```

Další parametry:

```
--output <cesta>   cílový soubor (default: totalgold.txt ve složce skriptu)
--poll <sekundy>   interval hlídání (default: 2 s)
--raw              do totalgold.txt zapsat jen číslo (raw copper) místo formátu "1,234g 56s 78c"
```

**Pokud skript hlásí „cannot locate GX.lua":** soubor `GX.lua` ve
`WTF\Account\...\SavedVariables\` vytvoří klient až poté, co addon jednou
naběhl a hra zapsala SavedVariables. Přihlas se na postavu, ve hře spusť
`/gx show` a pak `/reload` – soubor se objeví. Skript (i GX Monitor) ho poté
už najde a při každém `/reload` (nebo autosave) přepíše `totalgold.txt`.

**Jak zjistit správnou cestu k `GX.lua` na Windows:** najeď do složky
`WTF\Account\` v instalaci WoW. Podsložky = názvy účtů. Pro tvůj účet pak
`SavedVariables\GX.lua` je přesně ten soubor, který se sleduje. Pokud nevíš,
kde WoW je: klikni pravým na ikonu hry (Battle.net) → Otevřít v Průzkumníku.

---

## 🎬 Napojení v OBS

Platí stejně pro GX Monitor i pro `watcher.py` — oba zapisují do stejného
typu textového souboru (`totalgold.txt` podle nastavení).

1. Ve scéně přidej zdroj **Text (GDI+)**.
2. Otevři **Vlastnosti** zdroje.
3. Zaškrtni **„Read from file"** (v češtině „Číst ze souboru").
4. Vyber výstupní soubor (cestu vidíš v GX Monitoru na dashboardu, nebo ji
   vypíše `watcher.py` při startu).
5. Volitelně nastav font, barvu, pozadí.
6. OBS obnoví text při každé změně souboru automaticky. Pokud by se text
   neobnovil, zdroj na chvilku skryj a zase zobraz (toggle visibility).

---

## 🧩 Edge cases, které addon řeší

- **Nový účet / čistá instalace:** `GX_DB` se inicializuje na prázdnou
  tabulku, `/gx show` zobrazí `0g` bez chyb v `/reload`.
- **Nově přidaná postava:** zlato se uloží při prvním přihlášení.
- **Spojené realmy / apostrof v názvu serveru:** klíč postavy se normalizuje
  (bez mezer a apostrofů), takže se stejná postava nezdvojí kvůli
  nekonzistentnímu názvu serveru mezi API voláními.
- **Smazaná/přejmenovaná/přesunutá postava:** starý záznam po ní zůstává v
  `GX_DB.characters` a zkresluje součet. Spusť `/gx characters` (vypíše
  všechny uložené postavy s datem posledního přihlášení) a smaž jen tu
  jednu přes `/gx forget <číslo nebo Jméno-Server>` — `/gx reset` teď maže
  úplně vše (s potvrzením) a používej ho jen když chceš začít od nuly.
- **Chybějící soubor SavedVariables:** GX Monitor i `watcher.py` počkají, než
  WoW soubor vytvoří (stav „waiting for SavedVariables…" / „waiting").

---

## 📚 Reference na použité Blizzard šablony (wow-ui-source)

Při vývoji byl použit extrahovaný zdroj Blizzard UI (`Gethe/wow-ui-source`,
verze 12.1.0) pouze jako **dokumentace oficiálních šablon a API**. Klíčové
použité položky:

| Šablona / soubor | Použití |
| --- | --- |
| `Blizzard_SharedXML/Mainline/SharedUIPanelTemplates.xml` — `PortraitFrameFlatTemplate` | Hlavní okno `/gx show` (portrét, titulek, close button, flat pozadí). |
| `Blizzard_SharedXML/Shared/Dialog/DialogTemplates.xml` — `DialogBorderTemplate` a `DialogHeaderTemplate` | Okno `/gx export` (zaoblený rámeček + DiamondMetal header; border je použitý jako child `BG`, stejně jako u `CreateChannelPopup`). |
| `.../SharedUIPanelTemplates.xml` — `UIPanelCloseButtonDefaultAnchors` | Zavírací tlačítko. |
| `Blizzard_SharedXML/Shared/InputBox/InputBoxTemplates.xml` — `InputBoxTemplate` | Kopírovatelný EditBox. |
| `Blizzard_SharedXML/FormattingUtil.lua` — `GetMoneyString` | Nativní formátování měny (ikony mincí). |
| Nativní `BreakUpLargeNumbers` (engine funkce) | Oddělovače tisíců v `/gx export` textu místo vlastní regex implementace. |
| `Blizzard_UIParentPanelManager/Shared/UIParentPanelManager.lua` — `UISpecialFrames` | Zavírání okna klávesou ESC. |

Díky dědění z oficiálních šablon okno vypadá jako nativní UI aktuálního klienta
a přežije vizuální úpravy Surface – a posteriori. `wow-ui-source` se do addonu
nekopíruje, je to pouze referenční materiál.

Minimap ikona a DataBroker feed jsou postavené na standardních, veřejně
dostupných (public domain) knihovnách vendorovaných v `GX/Libs/`: `LibStub`,
`CallbackHandler-1.0`, `LibDataBroker-1.1` a `LibDBIcon-1.0`. Nahradily
původní ruční trigonometrii kolem pozice tlačítka na minimapě.

---

## ✅ Otestované scénáře

- **Nová instalace:** addon se načte, `/gx show` zobrazí `0g` (prázdná DB),
  žádné errory v `/reload`.
- **Více postav:** dvě postavy na účtu, každá uloží vlastní zlato; `/gx show`
  zobrazí součet obou.
- **Reload UI:** `/reload` proběhne čistě, SavedVariables se zapíšou, okna se
  po reloadu dají znovu otevřít.
- **`/gx show`:** okno s titulkem, mincemi zlata, tlačítkem ✕, ESC i tažením.
- **`/gx export`:** EditBox obsahuje součet (formát + raw copper), text jde
  označit a zkopírovat.
- **`/gx autosave`:** zapnutí/vypnutí, krátký interval (záměrně 1 min)
  → WoW provede reload → `GX.lua` se objeví/zaktualizuje na disku.
- **End-to-end test:** `/gx show` ve hře → `/reload` → změna `GX.lua`
  zachycena GX Monitorem/`watcher.py` → výstupní soubor se přepíše novým
  součtem → hodnota se zobrazí v testovacím OBS zdroji „Text (GDI+)" s
  „Read from file".

---

## 📝 Changelog

Historie změn (addon i GX Monitor) je v [`CHANGELOG.md`](CHANGELOG.md).
