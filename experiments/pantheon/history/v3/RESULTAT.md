# HUD och dörrskarvar rättade, 2026-10-29

Den vanliga startens `--no-menus` har tagits bort. HUD och menyer visas nu
vid interaktiv start. Dolda benchmarkkörningar behåller flaggan.

Gångarna består nu av ett gemensamt U-format vägg/tak-skal som slutar vid
bågens baksida och rummets verkliga anslutning. Golven är skurna efter de
polygonkonturer som rotundan och sidorummen använder. En låg övergång ansluter
höjdskillnaden till det gamla infällda golvet utan överlapp. Dörrlisterna är
geringsskarvade mot de böjda listerna. Slutna byggdelar använder backface-culling.
Ingen depth bias eller polygon offset har lagts till.

- Kontroll av exponerade koplanära arkitekturytor i dörrzonerna: **69 ytpar före, 0 efter**.
- 16 närbilder från båda håll vid alla åtta gångskarvar: **32/32 pixelidentiska färg/djupjämförelser** med culling på/av.
- De vanliga 15 kameravyerna: **30/30 pixelidentiska färg/djupjämförelser**.
- Materialbevarande, glasrutornas slutenhet, skalor och resursreferenser passerade.
- Den tidigare scenen och dess generator finns i `history/v2`.

Överlappningskontrollen granskar axelparallella ytor i dörrzonerna. Interna,
motstående ensidiga kontaktändar och ensidiga basytor som vetter under golvet
räknas inte som exponerade. Den är inte en allmän självskärningskontroll av
samtliga importerade modeller.

Kör `python experiments/pantheon/verify_doorways.py` för skarvkontrollen.
Nedanstående prestandadata gäller version 2 från den 28 oktober; inga nya
bildtidsmätningar har gjorts som del av den här rättningen. Motorn och
standardscenen har inte ändrats.

![Rättad anslutning, sedd från gången](doorway-preview.png)

## Mätning av version 2, 2026-10-28

Rummen är utbyggda, de gamla modellerna och alla nio bollmaterial är bevarade,
och fyra befintliga byggnader har skalats till meter och fått geometriska glasfasader.
Motorkod, shaders och standardscen är oförändrade. Föregående prototyp finns i `history/v1`.

![Materialrummet](materials-preview.png)

## Verifierat

- 15 kameravyer × färg och djup: **30/30 pixelidentiska jämförelser** med culling på/av.
- Drakens pose hålls vid exakt 0,5 s i samma befintliga Application under varje jämförelse. Prestandakörningarna har levande animation.
- Alla nio ursprungliga bollmaterials egenskaper matchar originalscenens XML. Alla ursprungliga modelltyper finns kvar.
- Spegel, drake och bil har sina ursprungliga interaktionsnamn. Bilens ljus och drakens klipp återanvänds.
- **2 268 slutna 12 mm glasrutor** verifierade med svetsad kanttopologi. IOR 1,52; roughness 0,055.
- Samtliga fyra torn använder uniform skala **0,01** och får **67,3–70,7 %** glasandel av sin rektangulära fasadarea.
- HDRI:ns checksumma, sex CC0-materialfiler och alla XML-resurser kontrollerades.
- Befintliga tester `scenegraph`, `culling`, `portals`: **3/3 passerade**.

## Prestanda

Intel Core i5-10600K, GTX 1080, drivrutin 560.94; 1280×800, deferred, Cook–Torrance,
FXAA, AO, skuggor, HDRI och fullupplöst spegel. VSync av; profileraren inklusive
prestandavyn aktiv. 600 bildrutor per vy, de sista 120 redovisas. Inga skärmbilder
togs under mätkörningarna.

| Vy | Medel, ms | P95, ms | FPS |
| --- | ---: | ---: | ---: |
| Rotunda | 15,96 | 17,07 | 62,67 |
| Kupol | 13,44 | 15,35 | 74,40 |
| Materialrum | 13,67 | 15,62 | 73,13 |
| Spegelvy | 19,09 | 22,76 | 52,40 |
| Stad | 16,24 | 17,36 | 61,59 |
| Galleri | 16,69 | 18,03 | 59,90 |
| Drake / stjärnhimmel | 16,64 | 18,29 | 60,11 |

Den utbyggda scenen ligger omkring **52–74 FPS** i dessa korta vyer. Den stora
spegelvyn och vissa P95-värden överskrider 16,67 ms; detta är ingen garanti om
60 FPS. Spegelupplösning, drakens animation och objektmaterial har behållits.
Spegelns entréplacering gör att den inte fångar en spegelbild från rotundan.
Rådata och CPU/GPU-delsteg finns i [benchmark.csv](benchmark.csv).

## Omfattning och gränser

Materialrum och planetarium är 48 m i diameter; arkivgalleriet är 42 × 44 m.
Staden är 64 × 78 m med endast fyra torn, cirka 47–105 m höga. Porschen är
cirka 1,98 × 4,55 m. Rummen ligger bakom separata anslutningsgångar och överlappar inte.
Spegelväggen är 36 × 7,28 m och lämnar en 6,48 m bred central passage.

Glas och metall är separata materialmeshar. Två våningsresurser delas mellan
alla fasadvåningar. Source-FBX och dess fönsterytor behålls i respektive
byggnadsgrupp. Glasets tjocklek och BSDF-parametrar finns i
`scene-manifest.json`. Byggnadernas inre rum är inte modellerade; denna version
är rasterrenderad och innehåller ingen raytracer eller separat IBL för varje rum.

Den separata portalvarianten har kvar det tidigare hittade närplansfelet.
Den nya auditen gav 229–5 478 ändrade pixlar vid trösklarna. Portaler är
avstängda i den vanliga scenen; frustumculling och LOD används.

[Fullständiga kontrollresultat](verification.json) · [Start och reproduktion](README.md)

![Staden och glasfasaderna](city-preview.png)

![Draken under stjärnhimlen](astra-preview.png)

![Spegelvägg med genomgång](mirror-preview.png)
