# Drosera – Misure per il sound design

Data: 2026-11-03, sul commit `ec6e150` (fix della revisione inclusi). Motore a 48 kHz tramite `DroseraCore`
(`review/experiments/sd_measure.cpp`), VST3 caricato in un host JUCE per il confronto fra sample rate
(`review/experiments/sr_probe.cpp`). Tutto automatico: nessun ascolto. Le letture qui sotto dicono dove vale
la pena ascoltare.

## Cosa dicono le misure

1. **I preset non sono allineati di livello.** Da −9,2 dBFS (Cathedral Bloom) a −22,5 dBFS (Quiet Room): 13 dB di
   escursione cambiando preset. Glass Wind e Cathedral Bloom sono i più caldi e hanno la cresta più bassa
   (il limiter lavora). Glass Wind oscilla di 11,5 dB fra finestre di 2 s e ci mette 3,6 s a nascere: è il
   preset più instabile. Razor Feedback e Chord Keys sono istantanei e fermi (spread 0,3–0,4 dB).

2. **Tessitura sotto MIDI.** A secco tutte le note da 18 a 84 sono entro ±17 cent a ogni Body, con una sola
   eccezione (nota 80, Body 0,7: −36 cent). La nota 16 suona sempre 11 semitoni sopra (+1104 cent): va
   esclusa dal range MIDI o documentata come oggi. Nessuna nota muta in queste impostazioni (Lowpass 0,8,
   Highpass 0,2). **Con Reverb Mix al 35 %** l'intonazione si rompe sotto la nota 48: +46 a 18, −25 a 24,
   −27 a 36, −25 a 43, e un +105 a 52 con Body 0,9. Sopra 48 resta entro ±20. Il riverbero nell'anello
   stona i bassi: è il limite più serio per un uso melodico, e suggerisce un avviso nel manuale ("sotto
   il Do 3 tenere il riverbero basso") o la compensazione di fase del riverbero.

3. **Lo stesso progetto non suona sempre uguale.** Un host applica lo stato *dopo* `prepareToPlay`, così i
   parametri scivolano dai default al preset con lo smoothing (fino a 1 s per Body). Simulando l'arrivo
   dello stato dopo 0, 1, 10 o 100 blocchi: 9 preset su 14 si assestano sulla stessa parziale, ma Doppler
   Void (704 o 198 Hz), Dark Tide (195, 124 o 315 Hz, con 4 dB di differenza di livello), Echo Drift
   (1142 o 832 Hz), Ember Pong (832 o 766 Hz) e Sub Cellar (668 o 721 Hz) cambiano "nota" a seconda del
   momento. Per chi salva un brano è un problema reale. Rimedio piccolo: al ripristino dello stato far
   saltare i parametri al valore (come fa `first_` dopo `Init`) invece di farli scivolare; i preset
   caricati dalla finestra possono continuare a scivolare.

4. **Mono.** Tutti i preset perdono fra 0,5 e 4,4 dB sommati a mono. I due canali sono quasi indipendenti
   (correlazione ~0: Echo Drift, Razor, Sympathetic, Ember Pong, Keys), per via dello sfasamento di 4
   campioni fra i delay di sinistra e destra: −3 dB è la somma di due segnali scorrelati. Chord Keys ha
   correlazione negativa (−0,27) e perde 4,4 dB: in mono si svuota. Glass Wind, Cathedral Bloom e Open
   Fifths sono invece quasi mono (correlazione 0,6–0,8). Da decidere se è la larghezza voluta; un controllo
   di Width sulle voci, come quello del PONG, lo renderebbe scelta e non caso.

5. **Durata.** Nessuna deriva di livello, nessun DC, nessun recupero in 30 minuti complessivi ai valori
   estremi. Un comportamento da conoscere: con Reverb Decay a 1,0 il centroide scende da 1130 a 160 Hz in
   dieci minuti (già a 300 Hz dopo due): il drone affonda in un rombo grave e non torna. È il riverbero
   infinito che accumula il basso che il DC blocker lascia passare. Udibile e forse voluto, ma vale un
   ascolto lungo.

6. **Combinazioni estreme.** Highpass sopra Lowpass dà silenzio assoluto (−171 dBFS): la finestra non lo
   segnala. Le altre combinazioni restano limitate (picco ≤ 0,58) e suonano, compreso il runaway del PONG.
   Il Resonator con una sinusoide in ingresso si aggancia all'ingresso (220 Hz), e con raffiche
   intermittenti risuona sulla nota (110 Hz) fra una raffica e l'altra.

7. **Sample rate.** Nel VST3 a 44,1, 48 e 96 kHz livelli identici al decimo di dB e stesse parziali in 7 preset
   su 9; le due eccezioni (Echo Drift a 96 kHz, Chord Keys a 48 kHz) hanno RMS identico e cadono su parziali
   vicine di livello (per Chord Keys sono il Mi e il Sol dell'accordo): la finestra d'analisi è a numero fisso
   di campioni, a 96 kHz dura la metà. Letto come artefatto di misura, non verificato all'ascolto.

## Cosa resta all'orecchio

Ascolto dei 16 preset con queste tabelle davanti; fraseggio MIDI (glide fisso di 200 ms, velocity ignorata,
nessun sustain); movimento delle manopole; sorgenti reali nel Resonator; giudizio sull'aliasing del PONG a
Drive e Tone massimi.

## Tabelle

### A. Preset: livello, dinamica e tempo di nascita (20 s da Init)

| Preset | RMS 5-20 s | Picco | Cresta | -3 dB dopo | Spread 2 s | Dominante | Centroide |
|---|---|---|---|---|---|---|---|
| Default | -18.3 dBFS | 0.33 | 8.8 dB | 0.95 s | 3.8 dB | 500 Hz | 3398 Hz |
| Sub Cellar | -17.5 dBFS | 0.47 | 11.1 dB | 0.85 s | 3.5 dB | 668 Hz | 1051 Hz |
| Glass Wind | -11.7 dBFS | 0.62 | 7.6 dB | 3.60 s | 11.5 dB | 294 Hz | 2066 Hz |
| Cathedral Bloom | -9.2 dBFS | 0.76 | 6.8 dB | 3.20 s | 4.3 dB | 175 Hz | 1154 Hz |
| Echo Drift | -19.8 dBFS | 0.26 | 8.1 dB | 0.45 s | 7.6 dB | 1142 Hz | 2433 Hz |
| Doppler Void | -19.3 dBFS | 0.36 | 10.4 dB | 0.60 s | 2.6 dB | 704 Hz | 1730 Hz |
| Dark Tide | -13.1 dBFS | 0.68 | 9.7 dB | 1.35 s | 5.1 dB | 195 Hz | 397 Hz |
| Razor Feedback | -16.8 dBFS | 0.26 | 5.1 dB | 0.05 s | 0.3 dB | 512 Hz | 7329 Hz |
| Quiet Room | -22.5 dBFS | 0.27 | 11.1 dB | 2.05 s | 3.8 dB | 543 Hz | 1117 Hz |
| Keys (play notes) | -18.3 dBFS | 0.33 | 8.8 dB | 0.40 s | 2.5 dB | 133 Hz | 765 Hz |
| Resonator (needs input) | (richiede ingresso: vedi F) | | | | | | |
| Init (silent) | (richiede ingresso: vedi F) | | | | | | |
| Open Fifths | -14.9 dBFS | 0.48 | 8.5 dB | 0.65 s | 2.9 dB | 151 Hz | 1075 Hz |
| Sympathetic | -17.5 dBFS | 0.52 | 11.8 dB | 0.75 s | 1.5 dB | 597 Hz | 1980 Hz |
| Chord Keys | -19.2 dBFS | 0.28 | 8.3 dB | 0.15 s | 0.4 dB | 165 Hz | 1253 Hz |
| Ember Pong | -21.3 dBFS | 0.23 | 8.4 dB | 0.35 s | 1.4 dB | 832 Hz | 3492 Hz |

### B. Mappa della tessitura sotto MIDI Pitch (scarto in cent della parziale dominante; `mute` = sotto -40 dBFS; `*` = oltre 25 cent)

#### Reverb Mix 0.00

| Body \ nota | 16 | 18 | 20 | 24 | 28 | 31 | 36 | 40 | 43 | 48 | 52 | 55 | 60 | 64 | 67 | 72 | 76 | 79 | 80 | 84 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0.1 | +1104* | +1 | +1 | +1 | +0 | +1 | +0 | +1 | -3 | +2 | +2 | +2 | +5 | +7 | +0 | +0 | +0 | +16 | +17 | +0 |
| 0.3 | +1104* | +1 | +1 | +1 | +0 | +1 | +0 | +1 | -3 | +2 | +2 | +2 | -0 | +0 | +6 | +0 | +0 | +0 | +0 | +0 |
| 0.5 | +1104* | +1 | +1 | +1 | +0 | +1 | +0 | +1 | +0 | +2 | +2 | +2 | +3 | -0 | +4 | +0 | +4 | +0 | +0 | +0 |
| 0.7 | +1104* | +1 | +1 | +1 | +0 | +1 | +0 | +1 | +1 | +3 | +1 | +1 | +1 | -0 | +1 | +3 | +3 | +0 | -36* | +0 |
| 0.9 | +1104* | +1 | +1 | +1 | -0 | +1 | +0 | +1 | +1 | +1 | +2 | +2 | +1 | +1 | +1 | +1 | +1 | +2 | +0 | +0 |

#### Reverb Mix 0.35

| Body \ nota | 16 | 18 | 20 | 24 | 28 | 31 | 36 | 40 | 43 | 48 | 52 | 55 | 60 | 64 | 67 | 72 | 76 | 79 | 80 | 84 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0.1 | +1125* | +46* | -15 | -25* | +0 | +15 | -27* | -7 | -25* | +20 | +13 | +8 | -1 | -9 | +10 | +3 | +9 | -1 | -1 | +19 |
| 0.3 | +1125* | +46* | -15 | -25* | +0 | +15 | -27* | -7 | -25* | +20 | +13 | +8 | -8 | -15 | +10 | +4 | +7 | +2 | +11 | +10 |
| 0.5 | +1125* | +46* | -15 | -25* | +0 | +15 | -27* | -7 | -9 | +19 | +11 | +7 | +14 | +2 | +9 | -0 | +5 | +1 | +8 | -1 |
| 0.7 | +1125* | +46* | -15 | -25* | +0 | +15 | -24 | -5 | -8 | +10 | +9 | +7 | +6 | -1 | +7 | +3 | +4 | +3 | +0 | +32* |
| 0.9 | +1125* | +46* | -15 | -28* | -5 | +10 | +18 | -5 | -7 | +5 | +105* | +3 | -1 | -0 | +6 | -0 | +32* | +2 | -0 | +21 |

### C. Ripetibilita: lo stesso preset raggiunto subito da Init, o dopo 1 / 10 / 100 blocchi ai valori di default (come un host che carica lo stato dopo prepareToPlay)

| Preset | Dominante subito | dopo 1 blocco | dopo 10 | dopo 100 | RMS subito / 1 / 10 / 100 |
|---|---|---|---|---|---|
| Default | 500 Hz | 500 Hz | 500 Hz | 501 Hz | -19 / -19 / -19 / -19 |
| Sub Cellar | 668 Hz | 668 Hz | 668 Hz | 721 Hz | -17 / -17 / -17 / -17 |
| Glass Wind | 294 Hz | 294 Hz | 294 Hz | 296 Hz | -12 / -12 / -12 / -12 |
| Cathedral Bloom | 175 Hz | 175 Hz | 175 Hz | 176 Hz | -10 / -10 / -9 / -10 |
| Echo Drift | 1142 Hz | 832 Hz | 1142 Hz | 832 Hz | -20 / -21 / -21 / -18 |
| Doppler Void | 704 Hz | 198 Hz | 703 Hz | 198 Hz | -20 / -18 / -20 / -18 |
| Dark Tide | 195 Hz | 124 Hz | 124 Hz | 315 Hz | -14 / -16 / -17 / -18 |
| Razor Feedback | 512 Hz | 512 Hz | 512 Hz | 512 Hz | -17 / -17 / -17 / -17 |
| Quiet Room | 543 Hz | 543 Hz | 543 Hz | 544 Hz | -23 / -23 / -23 / -22 |
| Keys (play notes) | 133 Hz | 133 Hz | 133 Hz | 133 Hz | -18 / -18 / -18 / -18 |
| Open Fifths | 151 Hz | 151 Hz | 151 Hz | 151 Hz | -15 / -15 / -15 / -15 |
| Sympathetic | 597 Hz | 597 Hz | 597 Hz | 597 Hz | -18 / -17 / -17 / -18 |
| Chord Keys | 165 Hz | 165 Hz | 165 Hz | 165 Hz | -19 / -19 / -19 / -19 |
| Ember Pong | 832 Hz | 766 Hz | 766 Hz | 766 Hz | -21 / -20 / -20 / -20 |

### D. Compatibilita mono (5-20 s): somma mono contro media dei canali, energia del lato, correlazione L/R

| Preset | Mono - stereo | Side - Mid | Corr. L/R |
|---|---|---|---|
| Default | -1.9 dB | -2.6 dB | +0.29 |
| Sub Cellar | -2.7 dB | -0.6 dB | +0.07 |
| Glass Wind | -0.6 dB | -8.4 dB | +0.75 |
| Cathedral Bloom | -0.5 dB | -9.6 dB | +0.80 |
| Echo Drift | -3.2 dB | +0.4 dB | -0.04 |
| Doppler Void | -2.3 dB | -1.6 dB | +0.18 |
| Dark Tide | -2.0 dB | -2.3 dB | +0.26 |
| Razor Feedback | -3.2 dB | +0.4 dB | -0.04 |
| Quiet Room | -2.6 dB | -0.9 dB | +0.10 |
| Keys (play notes) | -3.5 dB | +0.9 dB | -0.11 |
| Open Fifths | -0.9 dB | -6.3 dB | +0.62 |
| Sympathetic | -3.1 dB | +0.1 dB | -0.01 |
| Chord Keys | -4.4 dB | +2.4 dB | -0.27 |
| Ember Pong | -3.1 dB | +0.2 dB | -0.03 |

### F. Combinazioni estreme e ingresso (20 s)

| Caso | RMS 5-20 s | Picco | Dominante | Centroide | Note |
|---|---|---|---|---|---|
| Highpass 4 kHz sopra Lowpass 100 Hz | -171.0 dBFS | 0.00 | 2319 Hz | 3467 Hz | i filtri si escludono |
| Sympathetic, LOCK off, Coupling 100 % | -16.2 dBFS | 0.58 | 597 Hz | 1888 Hz | voci libere accoppiate |
| Ember Pong, Echo Feedback 150 %, Drive max, Tone 3.2 kHz | -18.1 dBFS | 0.29 | 832 Hz | 6777 Hz | runaway saturato |
| Default, Feedback +12 dB, Body minimo | -20.3 dBFS | 0.27 | 1076 Hz | 6036 Hz | anello corto al massimo |
| Resonator, sinusoide 220 Hz in ingresso, nota MIDI 57 | -16.9 dBFS | 0.40 | 220 Hz | 642 Hz | ingresso continuo |
| Resonator, 110 Hz a raffiche di 0.5 s, nota 45 | -25.5 dBFS | 0.19 | 110 Hz | 254 Hz | ingresso intermittente |

### E. Dieci minuti ai valori estremi: livello e centroide per minuto

| Caso | RMS per minuto (dBFS) | Centroide per minuto (Hz) | Picco | DC | Recuperi |
|---|---|---|---|---|---|
| Reverb Decay 1.0, Feedback max | -11 -9 -9 -9 -10 -10 -11 -11 -11 -11 | 1130 300 226 199 177 167 165 159 157 159 | 0.74 | -0.00013 | 0 |
| Echo feedback 150 %, PONG drive max | -16 -16 -17 -16 -16 -17 -17 -16 -17 -17 | 2452 2721 2555 2526 2557 2563 2490 2578 2526 2775 | 0.45 | +0.00000 | 0 |
| Poly, Coupling max, Feedback max | -19 -19 -20 -19 -19 -19 -20 -19 -20 -19 | 4862 4999 4882 5237 5057 5259 5222 5005 4832 5114 | 0.41 | -0.00000 | 0 |

### G. VST3 ai tre sample rate (15 s, RMS 5-15 s, spettro da 8 s)

| Preset | 44.1 kHz: RMS / dominante / centroide | 48 kHz | 96 kHz |
|---|---|---|---|
| Default | -20.6 dBFS / 500 Hz / 3176 Hz | -20.6 dBFS / 501 Hz / 3156 Hz | -20.6 dBFS / 501 Hz / 2889 Hz |
| Sub Cellar | -19.4 dBFS / 721 Hz / 998 Hz | -19.4 dBFS / 721 Hz / 994 Hz | -19.4 dBFS / 721 Hz / 1011 Hz |
| Glass Wind | -12.4 dBFS / 294 Hz / 2081 Hz | -12.4 dBFS / 294 Hz / 1885 Hz | -12.4 dBFS / 292 Hz / 1521 Hz |
| Echo Drift | -23.2 dBFS / 832 Hz / 2354 Hz | -23.2 dBFS / 832 Hz / 2349 Hz | -23.2 dBFS / 1142 Hz / 2243 Hz |
| Razor Feedback | -20.0 dBFS / 511 Hz / 7083 Hz | -20.0 dBFS / 512 Hz / 7195 Hz | -20.0 dBFS / 511 Hz / 7061 Hz |
| Open Fifths | -15.4 dBFS / 151 Hz / 987 Hz | -15.4 dBFS / 151 Hz / 976 Hz | -15.4 dBFS / 151 Hz / 860 Hz |
| Sympathetic | -20.6 dBFS / 592 Hz / 2036 Hz | -20.6 dBFS / 592 Hz / 2035 Hz | -20.6 dBFS / 593 Hz / 2058 Hz |
| Ember Pong | -24.5 dBFS / 1981 Hz / 3575 Hz | -24.5 dBFS / 1981 Hz / 3566 Hz | -24.5 dBFS / 1981 Hz / 3452 Hz |
| Chord Keys (C E G) | -23.8 dBFS / 197 Hz / 1241 Hz | -23.5 dBFS / 165 Hz / 1235 Hz | -23.8 dBFS / 197 Hz / 1217 Hz |
