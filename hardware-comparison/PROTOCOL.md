# Confronto plugin ↔ hardware (Audrey II su Daisy Seed)

Il sistema è un anello di feedback saturante eccitato da rumore: due esecuzioni non sono mai uguali
campione per campione, nemmeno sullo stesso hardware. Si confrontano quindi **statistiche** (bilanciamento
spettrale, centroide, larghezza stereo, stabilità del livello), non le forme d'onda.

## Cosa serve
- Audrey II con il firmware **v3 ufficiale** (`audrey-ii-v3.bin`), uscita L/R in un'interfaccia audio.
- Registrazione **stereo, 48 kHz, 24 bit** (altri formati vanno bene: lo script ricampiona), **senza**
  processing, EQ o compressione. Guadagno d'ingresso fisso per tutte le prese e mai in clip.

## Posizioni delle manopole
`presets.txt` definisce 10 preset con le manopole a quarti di corsa: **0, ¼, ½, ¾, 1**. Per evitare dubbi
sul verso dei potenziometri (il firmware inverte il valore dell'ADC) l'**0 è il minimo del controllo**: la
frequenza più bassa, il feedback a −60 dB, il body più corto, lo send a zero… e **1 è il massimo**. Non conta
il verso in cui ruoti, conta l'effetto: controlla con l'orecchio che 0 sia davvero il minimo.

| Preset | freq | fb gain | body | lpf | hpf | rev mix | rev decay | echo send | echo time | echo fb | volume | switch |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| p01_mid_drone | ½ | ¾ | ½ | ¾ | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p02_low_dark | ¼ | ¾ | ¾ | ½ | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p03_high_bright | ¾ | ¾ | ¼ | 1 | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p04_reverb_wash | ½ | ¾ | ½ | ¾ | ¼ | ¾ | ¾ | 0 | ½ | 0 | ¾ | off |
| p05_echo_swell | ½ | ¾ | ½ | ¾ | ¼ | 0 | ½ | ¾ | ½ | ¾ | ¾ | off |
| p06_echo_halved | ½ | ¾ | ½ | ¾ | ¼ | 0 | ½ | ¾ | ½ | ¾ | ¾ | **on** (tempo dimezzato) |
| p07_long_body | ½ | ¾ | 1 | ¾ | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p08_max_feedback | ½ | 1 | ½ | ¾ | ¼ | 0 | ½ | 0 | ½ | 0 | ¾ | off |
| p09_dark_filtered | ½ | ¾ | ½ | ¼ | ½ | ¼ | ½ | 0 | ½ | 0 | ¾ | off |
| p10_everything_mid | ½ | ½ | ½ | ½ | ½ | ½ | ½ | ½ | ½ | ½ | ½ | off |

Il reverb decay ha una curva speciale nel firmware (tensione −3): il punto ½ è quello della manopola,
l'effetto non è lineare, ed è già gestito dal plugin.

## Registrazione
Per ogni preset: imposta le manopole, **aspetta ~5 s** che il drone si assesti, poi registra **almeno 20 s**
(meglio 25). Un file per preset, nominato come il preset più un suffisso qualsiasi:
`p01_mid_drone_hw.wav`, `p02_low_dark_hw.wav`, … Mettili in una cartella, ad esempio `recordings/`.
Non toccare le manopole durante la registrazione. Attenzione al volume: p04 e p08 sono i più forti.

## Analisi
Dalla cartella `hardware-comparison/` (il Python con numpy/scipy/matplotlib è nell'ambiente
isolato `/private/tmp/claude-501/buildenv`, oppure `pip install numpy scipy matplotlib`):

```bash
python3 compare.py --hw-dir recordings --ref-dir reference -o report
```

Stampa per ogni preset livello, picco, centroide, correlazione L/R, oscillazione del livello e la
**differenza di forma spettrale** (in dB, a livello pareggiato), e salva un grafico PNG per preset in `report/`.
I file in `reference/` sono i render del plugin (stesso motore, blocchi da 4 campioni come sul firmware,
20 s); si rigenerano con `render_cli presets.txt reference 20`.

### Come leggere il verdetto
- **MATCH**: la differenza è entro la variabilità della registrazione stessa (tra la prima e la seconda
  metà del file, +1,5 dB; sempre tra 3 e 6 dB). Non distinguibile da una seconda esecuzione dell'hardware.
- **CLOSE**: fino al doppio della tolleranza. Tipico dei preset che evolvono (reverb wash, echo swell).
- **DIFFERENT**: da indagare. Guarda il grafico: quali bande divergono? un parziale in più o in meno
  indica una differenza di accordatura o di filtri, un'inclinazione generale indica filtri o riverbero.
- Il **livello assoluto dipende dal tuo guadagno** d'ingresso: guarda prima lo spettro.
- Nei render del plugin il livello è quasi lo stesso per quasi tutti i preset (saturazione dell'anello più
  limiter di uscita): non è un segnale utile per distinguerli, conta lo spettro.

## Controllo preliminare del firmware (già fatto)
Ho scaricato `audrey-ii-v3.bin` (92.192 byte, SHA-256
`184f3457fb0c752a2c2e78441cfbd84a1c3aa2777a7e1086509bea13d10723b4`) e l'ho ispezionato **senza eseguirlo**
(non è eseguibile su un Mac: è un'immagine Cortex-M7 per STM32H750, SP iniziale `0x20020000`, reset
`0x08000959`). Nei letterali float/int del binario compaiono le costanti caratteristiche del codice sorgente:
0.7 (limiter), 0.85 (feedback del riverbero), 0.98 (brightness della stringa), 12000 Hz (LPF del riverbero),
18000 Hz, 800 Hz (BPF dell'echo), 8000 Hz (Tone), 0.1447597 (T60), 240000 campioni (echo 5 s), 8192
(delay della stringa). Non compaiono 1.5, 5.0, −3.0, 12000 intero e il −90 dB, ma è atteso: i compilatori
ARM codificano molti valori come immediati o li calcolano a runtime. Questo rende molto plausibile che v3
derivi da questo sorgente, **non lo prova**: solo il confronto audio può dirlo.
